#include "../include/rpcclient.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "../NetStruct/include/netstruct_endian.h"

#include "../NetStruct/src/netstruct.c" // Build with netstruct

#define RPCCLIENT_LOG_FMT(Fmt, ...) printf("[RpcClient]: " Fmt "\n", __VA_ARGS__)
#define RPCCLIENT_LOG(Msg) puts("[RpcClient]: " Msg)

#if !defined(WIN32)
#define strcpy_s(dest, destz, src) strcpy(dest, src)
#endif

void* _RpcClient_Alloc(size_t Size) { return malloc(Size); }
void _RpcClient_Free(void* Memory) { return free(Memory); }

RemoteClass* _RpcClient_class = 0;
int _RpcClient_hedrlen = 0;

void RpcClient_Open()
{
	_RpcClient_class = (RemoteClass*)_RpcClient_Alloc(sizeof(*_RpcClient_class));
	memset(_RpcClient_class, 0, sizeof(*_RpcClient_class));

	_RpcClient_hedrlen = NetStruct_FmtLen("li", (int16_t)0, (int)0);
	assert(_RpcClient_hedrlen > 0);
}

void RpcClient_Close()
{
	for (RemoteMethod* meth = _RpcClient_class->head; meth;)
	{
		RemoteMethod* next = meth->next;
		_RpcClient_Free(meth->name);
		_RpcClient_Free(meth);
		meth = next;
	}
	_RpcClient_Free(_RpcClient_class);
	_RpcClient_class = 0;
}

int RpcClient_Call(const char* Method, const char* FmtArgs, ...)
{
	va_list va;
	uint8_t* argsbuf, * buf;
	int	argslen = 0, total;
	int	hedrlen = _RpcClient_hedrlen; // Hash, len
	uint64_t hash = _RpcClient_HashString(Method);

	if (hedrlen <= 0)
		return RpcCode_InternalError;

	if (FmtArgs)
	{
		va_start(va, FmtArgs);

		argslen = NetStruct_FmtLenVa(FmtArgs, va);
		if (argslen <= 0)
		{
			va_end(va);
			RPCCLIENT_LOG_FMT("Bad call to \"%s\", (with args \"%s\")", Method, FmtArgs);
			return RpcCode_BadCall;
		}

		va_end(va);
	}

	total = argslen + hedrlen;
	buf = _RpcClient_Alloc(total);
	argsbuf = buf + hedrlen;

	if (!buf)
		return RpcCode_InternalError;
	if (NetStruct_PackFmtBuffer(buf, hedrlen, "li", hash, argslen) <= 0)
		return _RpcClient_Free(buf), RpcCode_InternalError;

	va_start(va, FmtArgs);

	if (NetStruct_PackFmtBufferVa(argsbuf, argslen, FmtArgs, va) <= 0)
	{
		va_end(va);
		return _RpcClient_Free(buf), RpcCode_BadCall;
	}

	va_end(va);

	if (RpcClient_Send_Impl(buf, total) != total)
		return _RpcClient_Free(buf), RpcCode_InternalError;

	_RpcClient_Free(buf);
	return 0;
}

int RpcClient_Recv()
{
	uint8_t _stack[16];
	uint8_t* buf = _stack;
	int buflen = sizeof(_stack);
	int argslen;
	int error;
	uint64_t hash;
	RemoteMethod* meth;

	assert(_RpcClient_class && "RpcClient_Recv: Call RpcClient_Open() before using any other methods");

	if (!_RpcClient_class)
		return RpcCode_BadCall;
	if (_RpcClient_hedrlen <= 0 || buflen < _RpcClient_hedrlen)
		return RpcCode_InternalError;

	if (!RpcClient_Read_Impl((char*)buf, _RpcClient_hedrlen))
	{
		RPCCLIENT_LOG("RpcClient_Recv() couldn't receive arg header");
		return RpcCode_BadConnection;
	}

	if (NetStruct_UnpackFmt(buf, buflen, "li", &hash, &argslen) <= 0 ||
		argslen <= 0)
		return RpcCode_BadRemoteCall;

	for (meth = _RpcClient_class->head; meth; meth = meth->next)
		if (meth->hash == hash) // Find method by hash
			break;

	if (!meth)
		return RpcCode_BadRemoteCall;

	buf = (uint8_t*)_RpcClient_Alloc(argslen);
	if (!RpcClient_Read_Impl(buf, argslen))
	{
		RPCCLIENT_LOG("RpcClient_Recv() failed to receive call args");
		return RpcCode_BadConnection;
	}

	error = meth->call(buf, argslen);
	if (error != RpcCode_Ok)
		error = RpcCode_BadRemoteCall;

	_RpcClient_Free(buf);
	return error;
}

void RpcClient_AddMethod(RpcClient_Callback Callback, const char* Name)
{
	assert(_RpcClient_class && "RpcClient_Recv: Call RpcClient_Open() before using any other methods");

	size_t slen = strlen(Name) + 1;
	RemoteMethod* meth = (RemoteMethod*)_RpcClient_Alloc(sizeof(*meth));

	memset(meth, 0, sizeof(*meth));

	meth->hash = _RpcClient_HashString(Name);
	meth->call = Callback;
	meth->name = (char*)_RpcClient_Alloc(slen);
	memcpy(meth->name, Name, slen);

	if (!_RpcClient_class->head)
		_RpcClient_class->head = _RpcClient_class->tail = meth;
	else
		_RpcClient_class->tail->next = meth;
	_RpcClient_class->count++;
}

uint64_t _RpcClient_HashString(const char* szStr)
{
	// 64-bit FNV1-a: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param

	const uint64_t default_offset_basis = 0xCBF29CE484222325;
	const uint64_t prime = 0x100000001B3;
	uint64_t hash = default_offset_basis;

	for (; *szStr; szStr++)
	{
		hash ^= (uint64_t)*szStr;
		hash *= prime;
	}
	return hash;
}
