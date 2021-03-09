#include "../include/rpcclient.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "../NetStruct/include/netstruct_endian.h"

#include "../NetStruct/src/netstruct.c" // Build with netstruct

#define _RPCCLIENT_HEADERLEN (sizeof(int64_t) + sizeof(int))
#define _RPCCLIENT_HEADER "li"

#define RPCCLIENT_LOG_FMT(Fmt, ...) printf("[RpcClient]: " Fmt "\n", __VA_ARGS__)
#define RPCCLIENT_LOG(Msg) puts("[RpcClient]: " Msg)

#if !defined(WIN32)
#define strcpy_s(dest, destz, src) strcpy(dest, src)
#endif

void* _RpcClient_Alloc(size_t Size) { return malloc(Size); }
void _RpcClient_Free(void* Memory) { return free(Memory); }

void RpcClient_Create(RpcClient* Client, const RemoteClass* Class, void* Socket)
{
	memset(Client, 0, sizeof(*Client));
	Client->klass = Class;
	Client->socket = Socket;
}

void RpcClient_Destroy(RpcClient* Client) { }

void RemoteClass_Create(RemoteClass* Class) {
	memset(Class, 0, sizeof(*Class));
}

void RemoteClass_Destroy(RemoteClass* Class)
{
	for (RemoteMethod* meth = Class->head; meth;)
	{
		RemoteMethod* next = meth->next;
		_RpcClient_Free(meth->name);
		_RpcClient_Free(meth);
		meth = next;
	}
}

void RemoteClass_AddMethod(RemoteClass* Class, RpcClient_Callback Callback, const char* Name)
{
	size_t slen = strlen(Name) + 1;
	RemoteMethod* meth = (RemoteMethod*)_RpcClient_Alloc(sizeof(*meth));

	memset(meth, 0, sizeof(*meth));

	meth->hash = _RpcClient_HashString(Name);
	meth->call = Callback;
	meth->name = (char*)_RpcClient_Alloc(slen);
	memcpy(meth->name, Name, slen);

	if (!Class->head)
		Class->head = Class->tail = meth;
	else
		Class->tail->next = meth;
	Class->count++;
}

int RpcClient_Call(RpcClient* Client, const char* Method, const char* FmtArgs, ...)
{
	va_list va;
	uint8_t* argsbuf, * buf;
	int	argslen = 0, total;
	int	hedrlen = _RPCCLIENT_HEADERLEN;
	uint64_t hash = _RpcClient_HashString(Method);

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
	buf = (uint8_t*)_RpcClient_Alloc(total);
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

	if (RpcClient_Send_Impl(Client, (char*)buf, total) != total)
		return _RpcClient_Free(buf), RpcCode_InternalError;

	_RpcClient_Free(buf);
	return 0;
}

int RpcClient_Recv(RpcClient* Client)
{
	uint8_t _stack[_RPCCLIENT_HEADERLEN];
	uint8_t* buf = _stack;
	int buflen = sizeof(_stack);
	int argslen;
	int error;
	uint64_t hash;
	RemoteMethod* meth;

	if (!RpcClient_Read_Impl(Client, (char*)buf, _RPCCLIENT_HEADERLEN))
	{
		RPCCLIENT_LOG("RpcClient_Recv() couldn't receive arg header");
		return RpcCode_BadConnection;
	}

	if (NetStruct_UnpackFmt(buf, buflen, _RPCCLIENT_HEADER, &hash, &argslen) <= 0 ||
		argslen <= 0)
		return RpcCode_BadRemoteCall;

	for (meth = Client->klass->head; meth; meth = meth->next)
		if (meth->hash == hash) // Find method by hash
			break;

	if (!meth)
		return RpcCode_BadRemoteCall;

	buf = (uint8_t*)_RpcClient_Alloc(argslen);
	if (!RpcClient_Read_Impl(Client, (char*)buf, argslen))
	{
		RPCCLIENT_LOG("RpcClient_Recv() failed to receive call args");
		return RpcCode_BadConnection;
	}

	error = meth->call(Client, buf, argslen);
	if (error != RpcCode_Ok)
		error = RpcCode_BadRemoteCall;

	_RpcClient_Free(buf);
	return error;
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
