#include "rpcclient.h"
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(_WIN32)
#include <winsock2.h>
#elif defined(__OpenBSD__)
#include <sys/types.h>
#define ntohs(x) betoh16
#define ntohl(x) betoh32(x)
#define ntohll(x) betoh64(x)
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/endian.h>
#define ntohs(x) betoh16(x)
#define ntohl(x) betoh32(x)
#define ntohll(x) betoh64(x)
#elif defined(__linux__)
#include <endian.h>
#define ntohs(x) be16toh(x)
#define ntohl(x) be32toh(x)
#define ntohll(x) be64toh(x)
#define htons(x) htobe16(x)
#define htonl(x) htobe32(x)
#define htonll(x) htobe64(x)
#endif

#if !defined(WIN32)
#define strcpy_s(dest, destz, src) strcpy(dest, src)
#endif

RemoteClass* _RpcClient_class = 0;

void RpcClient_Open()
{
	_RpcClient_class = (RemoteClass*)malloc(sizeof(*_RpcClient_class));
	memset(_RpcClient_class, 0, sizeof(*_RpcClient_class));
}

void RpcClient_Close()
{
	for (RemoteMethod* meth = _RpcClient_class->head; meth;)
	{
		RemoteMethod* next = meth->next;
		free(meth->name);
		free(meth->fmt);
		free(meth);
		meth = next;
	}
	free(_RpcClient_class);
}

int RpcClient_Call(const char* Method, const char* FmtArgs, ...)
{
	va_list args;
	va_start(args, FmtArgs);

	NetList* list = NetList_Create();
	NetList_Add(list, &NetInt_Create(_RpcClient_HashString(Method))->arg);

	if (FmtArgs)
	{
		for (const char* fmt = FmtArgs; *fmt; fmt++)
		{
			NetArg* arg = NetArg_FromFmt(*fmt, &args);
			if (!arg)
			{
				assert(arg && "RpcClient_Call: Invalid NetArg_FromFmt call");
				return 0;
			}
			NetList_Add(list, arg);
		}
	}

	NetArgHeader* hedr = _NetArg_Pack(&list->arg);
	RpcClient_Send_Impl((char*)hedr, _NetArg_PackedSize(hedr));
	free(hedr);

	for (NetItem* item = list->head; item; item = item->next)
		NetArg_Destroy(item->arg);
	NetArg_Destroy(&list->arg);

	va_end(args);
	return 0;
}

int RpcClient_Recv()
{
	assert(_RpcClient_class && "RpcClient_Recv: Call RpcClient_Open() before using any other methods");

	char _stack[sizeof(NetArgHeader)];
	char* buf = _stack;

	if (!RpcClient_Read_Impl(buf, sizeof(NetArgHeader)))
	{
		printf("RpcClient_Recv() couldn't recieve arg header!\n");
		return -1;
	}

	if (buf[0] != NetArgType_List)
	{
		printf("RpcClient_Recv() expected NetArgType_List (%d) but got %d!\n", (int)NetArgType_List, (int)buf[0]);
		return -1;
	}

	int nextsize = ntohl(*(int*)(buf + 1));
	if (nextsize <= 0)
	{
		printf("RpcClient_Recv() recieved invalid size %d!\n", nextsize);
		return -1;
	}

	int pos = sizeof(NetArgHeader), size = pos + nextsize;
	buf = (char*)malloc(pos + nextsize);
	memcpy(buf, _stack, pos);

	if (!RpcClient_Read_Impl(buf + pos, nextsize))
	{
		puts("RpcClient_Recv() failed to recieve full arg!\n");
		return -1;
	}

	NetArg* arg = _NetArg_Unpack((NetArgHeader*)buf, size);
	if (!arg)
	{
		puts("RpcClient_Recv() failed to unpack arg!\n");
		return -1;
	}

	NetList* args = (NetList*)arg;
	if (args->count < 1 || args->head->arg->type != NetArgType_Int)
	{
		puts("RpcClient_Recv() expected function hash!\n");
		return -1;
	}

	NetInt* nethash = (NetInt*)args->head->arg;
	uint64_t hash = nethash->i_value;

	RemoteMethod* meth;
	for (meth = _RpcClient_class->head; meth; meth = meth->next)
		if (meth->hash == hash)
			break;

	if (!meth)
	{
		printf("RpcClient_Recv() failed to find function hash %u!\n", hash);
		return 1;
	}

	NetList tempargs = *args;
	tempargs.head = tempargs.head->next, tempargs.count--; // Lop off remote call header, leaving only method args

	const char* fmt = NetList_ToArgsFmt(&tempargs);
	if (strcmp(fmt, meth->fmt))
	{
		printf("RemoteMethod '%s': Invalid args. Expected '%s' but got '%s'", meth->name, meth->fmt, fmt);
		if (strlen(fmt) != strlen(meth->fmt))
			return RemoteError_BadArgsCount;
		return RemoteError_BadArgsType;
	}

	return meth->call(&tempargs) == RemoteError_Ok ? 0 : 1; // RemoteMethod handles errors from here
}

void RpcClient_AddMethod(RpcClient_Callback Callback, const char* Name, const char* FmtArgs)
{
	assert(_RpcClient_class && "RpcClient_Recv: Call RpcClient_Open() before using any other methods");

	RemoteMethod* meth = (RemoteMethod*)malloc(sizeof(*meth));
	memset(meth, 0, sizeof(*meth));

	size_t size = strlen(Name) + 1;
	meth->name = (char*)malloc(size);
	strcpy_s(meth->name, size, Name);

	size = strlen(FmtArgs) + 1;
	meth->fmt = (char*)malloc(size);
	strcpy_s(meth->fmt, size, FmtArgs);

	meth->hash = _RpcClient_HashString(meth->name);
	meth->call = Callback;

	if (!_RpcClient_class->head || !_RpcClient_class->tail)
		_RpcClient_class->head = _RpcClient_class->tail = meth;
	else
		_RpcClient_class->tail->next = meth;
	_RpcClient_class->count++;
}

NetBytes* RpcClient_AllocBytes(int Len)
{
	NetBytes* bytes = (NetBytes*)malloc(sizeof(NetBytes) + Len);
	bytes->len = Len;
	return bytes;
}

void RpcClient_FreeBytes(NetBytes* Bytes) {
	free(Bytes);
}

char NetArg_Fmt(const NetArg* Arg)
{
	switch (Arg->type)
	{
	case NetArgType_Int: return 'i';
	case NetArgType_Float: return 'f';
	case NetArgType_String: return 's';
	case NetArgType_Blob: return 'b';
	case NetArgType_List: return 'l';
	}
	assert(0 && "NetArg_Fmt does not support this Arg->type! Maybe you forgot to add a format case?");
	return 0;
}

NetArg* NetArg_FromFmt(char Fmt, va_list* VaList)
{
	switch (Fmt)
	{
	case 'i': return &NetInt_Create(va_arg(*VaList, int))->arg;
	case 'f': return &NetFloat_Create(va_arg(*VaList, double))->arg;
	case 's': return &NetString_Create(va_arg(*VaList, const char*), 0)->arg;
	case 'b': return &va_arg(*VaList, NetBlob*)->arg;
	case 'l': return &va_arg(*VaList, NetList*)->arg;
	}
	assert(0 && "NetArg_FromFmt does not support this fmt! Maybe you forgot to add a format case?");
	return 0;
}

const char* NetList_ToArgsFmt(NetList* List)
{
	char* fmt;
	if (List->_fmtCache && strlen(List->_fmtCache) == List->count)
		fmt = List->_fmtCache;
	else
		fmt = (char*)malloc(List->count + 1);

	char* ch = fmt;
	for (NetItem* item = List->head; item; item = item->next, ch++)
		*ch = NetArg_Fmt(item->arg);
	fmt[List->count] = 0;

	List->_fmtCache = fmt;
	return fmt;
}

NetInt* NetInt_Create(int64_t Value)
{
	NetInt* arg = (NetInt*)_NetArg_Template(NetArgType_Int, sizeof(NetInt), &_NetInt_MakeBytes, 0);
	arg->i_value = Value;
	return arg;
}

NetFloat* NetFloat_Create(double Value)
{
	NetFloat* arg = (NetFloat*)_NetArg_Template(NetArgType_Float, sizeof(NetFloat), &_NetInt_MakeBytes, 0);
	arg->f_value = Value;
	return arg;
}

NetString* NetString_Create(const char* String, int opt_Len)
{
	size_t len = opt_Len ? opt_Len + 1 : strlen(String) + 1;
	NetString* arg = (NetString*)_NetArg_Template(NetArgType_String, sizeof(NetString) + len, &_NetString_MakeBytes, 0);
	memcpy(arg->s_value, String, len - 1);
	arg->s_value[len - 1] = 0;
	return arg;
}

NetBlob* NetBlob_Create(const char* Data, int Len)
{
	NetBlob* arg = (NetBlob*)_NetArg_Template(NetArgType_Blob, sizeof(NetBlob) + Len, &_NetBlob_MakeBytes, 0);
	arg->bytes.len = Len;
	memcpy(arg->bytes.data, Data, Len);
	return arg;
}

NetList* NetList_Create() {
	return (NetList*)_NetArg_Template(NetArgType_List, sizeof(NetList), &_NetList_MakeBytes, &_NetList_Destroy);
}

void NetList_Add(NetList* List, NetArg* Arg)
{
	NetItem* item = _NetItem_Create(Arg);
	if (!List->count)
		List->head = List->tail = item;
	else
		List->tail = List->tail->next = item;
	List->count++;
}

NetList* NetList_FromFmt(const char* FmtArgs, ...)
{
	va_list args;
	va_start(args, FmtArgs);

	NetList* list = NetList_Create();
	for (const char* fmt = FmtArgs; *fmt; fmt++)
	{
		NetArg* arg = NetArg_FromFmt(*fmt, &args);
		if (!arg)
		{
			assert(arg && "NetList_FromFmt: Invalid NetArg_FromFmt call");
			return 0;
		}
		NetList_Add(list, arg);
	}

	va_end(args);
	return list;
}

int NetList_GetArgs(NetList* List, const char* FmtArgs, ...)
{
	int result = 1;
	va_list args;
	va_start(args, FmtArgs);

	if (strcmp(FmtArgs, NetList_ToArgsFmt(List)))
		result = 0; // Mismatched args
	else
		for (NetItem* item = List->head; item; item = item->next)
			*va_arg(args, void**) = item->arg;

	va_end(args);
	return result;
}

void NetArg_Destroy(NetArg* Arg) {
	Arg->Destroy(Arg);
}

NetInt* NetInt_FromBytes(const char* Bytes, int Len)
{
	assert(Len == sizeof(int64_t) && "NetInt_FromBytes: Incorrect buffer size");
	return NetInt_Create(ntohll(*(int64_t*)Bytes));
}

NetFloat* NetFloat_FromBytes(const char* Bytes, int Len)
{
	NetFloat* flt = NetInt_FromBytes(Bytes, Len); // Do the work for me
	flt->arg.type = NetArgType_Float;
	return flt;
}

NetString* NetString_FromBytes(const char* Bytes, int Len) {
	return NetString_Create(!Len ? "" : Bytes, Len);
}

NetBlob* NetBlob_FromBytes(const char* Bytes, int Len) {
	return NetBlob_Create(Bytes, Len);
}

NetList* NetList_FromBytes(const char* Bytes, int Len)
{
	NetList* list = NetList_Create();
	int off = 0;
	while (off < Len)
	{
		NetArgHeader* hdr = (NetArgHeader*)(Bytes + off);
		NetArg* arg = _NetArg_Unpack(hdr, Len - off);
		if (!arg)
		{
			printf("NetList_FromBytes() failed to unpack arg");
			break;
		}

		NetList_Add(list, arg);
		off += _NetArg_PackedSize(hdr);
	}
	return list;
}

NetArg* _NetArg_Template(char Type, size_t TypeSize, NetArg_MakeBytes_t MakeBytes, NetArg_Destroy_t opt_Destroy)
{
	NetArg* arg = (NetArg*)malloc(TypeSize);
	memset(arg, 0, TypeSize);
	arg->type = Type;
	arg->_MakeBytes = MakeBytes;
	arg->Destroy = opt_Destroy ? opt_Destroy : &_NetArg_GenericDestroy;
	return arg;
}

void _NetArg_GenericDestroy(NetArg* Arg) {
	free(Arg);
}

NetBytes* _NetInt_MakeBytes(const NetInt* Arg)
{
	NetBytes* bytes = RpcClient_AllocBytes(sizeof(Arg->i_value));
	*(int64_t*)&bytes->data = htonll(Arg->i_value);
	return bytes;
}

NetBytes* _NetString_MakeBytes(const NetString* Arg)
{
	size_t len = strlen(Arg->s_value);
	NetBytes* bytes = RpcClient_AllocBytes(strlen(Arg->s_value));
	memcpy(bytes->data, Arg->s_value, len);
	return bytes;
}

NetBytes* _NetBlob_MakeBytes(const NetBlob* Arg)
{
	NetBytes* bytes = RpcClient_AllocBytes(Arg->bytes.len);
	memcpy(bytes->data, Arg->bytes.data, Arg->bytes.len);
	return bytes;
}

NetBytes* _NetList_MakeBytes(const NetList* Arg)
{
	NetArgHeader** args = (NetArgHeader**)malloc(sizeof(*args) * Arg->count);
	int total = 0, i = 0, off = 0;
	for (NetItem* item = Arg->head; item; item = item->next, total += _NetArg_PackedSize(args[i++]))
		args[i] = _NetArg_Pack(item->arg);

	NetBytes* bytes = RpcClient_AllocBytes(total);
	for (i = 0; i < Arg->count; i++)
	{
		int size = _NetArg_PackedSize(args[i]);
		memcpy(bytes->data + off, args[i], size);
		off += size;
		free(args[i]);
	}
	free(args);

	return bytes;
}

NetItem* _NetItem_Create(NetArg* Arg)
{
	NetItem* item = (NetItem*)malloc(sizeof(*item));
	memset(item, 0, sizeof(*item));
	item->arg = Arg;
	return item;
}

void _NetItem_Destroy(NetItem* Item) {
	free(Item);
}

void _NetList_Destroy(NetArg* Arg)
{
	assert(Arg->type == NetArgType_List && "_NetList_Destroy: Given arg that was not of NetList type");
	NetList* list = (NetList*)Arg;
	for (NetItem* item = list->head; item;)
	{
		NetItem* next = item->next;
		_NetItem_Destroy(item);
		item = next;
	}
	free(list);
}

NetArgHeader* _NetArg_Pack(const NetArg* Arg)
{
	NetBytes* bytes = Arg->_MakeBytes(Arg);
	int len = sizeof(NetArgHeader) + bytes->len;

	NetArgHeader* hdr = (NetArgHeader*)malloc(len);
	memcpy(hdr->bytes.data, bytes->data, bytes->len);
	hdr->type = Arg->type;
	hdr->bytes.len = htonl(bytes->len);

	RpcClient_FreeBytes(bytes);
	return hdr;
}

NetArg* _NetArg_Unpack(const NetArgHeader* Arg, int Len)
{
	int len = ntohl(Arg->bytes.len);
	assert(len >= 0 && "_NetArg_Unpack: Arg->bytes.len was invalid");

	if (len > Len - sizeof(*Arg))
	{
		printf("_NetArg_Unpack() received NetArgHeader with bytes.len outside valid range\n");
		return 0;
	}

	switch (Arg->type)
	{
	case NetArgType_Int:
		return &NetInt_FromBytes(Arg->bytes.data, len)->arg;
	case NetArgType_Float:
		return &NetFloat_FromBytes(Arg->bytes.data, len)->arg;
	case NetArgType_String:
		return &NetString_FromBytes(Arg->bytes.data, len)->arg;
	case NetArgType_Blob:
		return &NetBlob_FromBytes(Arg->bytes.data, len)->arg;
	case NetArgType_List:
		return &NetList_FromBytes(Arg->bytes.data, len)->arg;
	}
	assert(0 && "_NetArg_Unpack does not support this Arg->type! Maybe you forgot to add a type case?");
	return 0;
}

int _NetArg_PackedSize(NetArgHeader* Arg) {
	return sizeof(*Arg) + ntohl(Arg->bytes.len);
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
