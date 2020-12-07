#pragma once
#include <stdlib.h>
#include <stdint.h>

typedef struct _NetArg NetArg;
typedef struct _NetInt NetInt;
typedef struct _NetFloat NetFloat;
typedef struct _NetList NetList;
typedef struct _NetBlob NetBlob;
typedef struct _NetString NetString;
typedef int(*RpcClient_Callback)(NetList* Args);

// TODO: Migrate everything to 64-bit values
enum ENetArgType
{
	NetArgType_Int = 0,
	NetArgType_Float,

	/*// - Marks end of primitive values.
	// - The types below are will be returned by reference instead
	NetArgType_EndPrimitiveTypes,*/
	NetArgType_String,
	NetArgType_Blob,

	/*// - Marks end of simple values.
	// - The types below are unique with many fields, requiring their "Net.." struct to be returned directly for the user's use
	NetArgType_EndSimpleTypes,*/
	NetArgType_List,
};

enum ERemoteError
{
	RemoteError_Ok,
	RemoteError_BadArgsCount,
	RemoteError_BadArgsType,
	RemoteError_BadMethod,
	RemoteError_UserError,
	RemoteError_InternalError,
	RemoteError_OtherError
};

typedef struct _NetBytes
{
	int len;
	char data[];
} NetBytes;

void RpcClient_Open(); // - Initialize necessary RPC Client variables
void RpcClient_Close(); // - Clean up RPC Client variables

int RpcClient_Call(const char* Method, const char* FmtArgs, ...);
int RpcClient_Recv();
void RpcClient_AddMethod(RpcClient_Callback Callback, const char* Name, const char* FmtArgs);

// - Must fill 'Buffer' up to 'Len' bytes
// - Return 0 if no more data is available from the connection
extern int RpcClient_Read_Impl(char* Buffer, int Len);
extern int RpcClient_Send_Impl(char* Buffer, int Len);

NetBytes* RpcClient_AllocBytes(int Len);
void RpcClient_FreeBytes(NetBytes* Bytes);

char NetArg_Fmt(const NetArg* Arg);
NetArg* NetArg_FromFmt(char Fmt, va_list* VaList); // - NetBlob or NetList must be existing reference
const char* NetList_ToArgsFmt(NetList* List);

NetInt* NetInt_Create(int64_t Value);
NetFloat* NetFloat_Create(double Value);
NetString* NetString_Create(const char* String, int opt_Len); // - 'opt_Len' = 0 for null terminated strings
NetBlob* NetBlob_Create(const char* Data, int Len);
NetList* NetList_Create();
void NetList_Add(NetList* List, NetArg* Arg);
NetList* NetList_FromFmt(const char* FmtArgs, ...);
int NetList_GetArgs(NetList* List, const char* FmtArgs, ...);
void NetArg_Destroy(NetArg* Arg);

NetInt* NetInt_FromBytes(const char* Bytes, int Len);
NetFloat* NetFloat_FromBytes(const char* Bytes, int Len);
NetString* NetString_FromBytes(const char* Bytes, int Len);
NetBlob* NetBlob_FromBytes(const char* Bytes, int Len);
NetList* NetList_FromBytes(const char* Bytes, int Len);

typedef struct _NetArgHeader NetArgHeader;
typedef struct _NetItem NetItem;
typedef struct _RemoteMethod RemoteMethod;
typedef struct _RemoteClass RemoteClass;
typedef void(*NetArg_Destroy_t)(NetArg*);
typedef NetBytes* (*NetArg_MakeBytes_t)(const NetArg* Arg);

NetArg* _NetArg_Template(char Type, size_t TypeSize, NetArg_MakeBytes_t MakeBytes, NetArg_Destroy_t opt_Destroy);
void _NetArg_GenericDestroy(NetArg* Arg);

NetBytes* _NetInt_MakeBytes(const NetInt* Arg);
NetBytes* _NetString_MakeBytes(const NetString* Arg);
NetBytes* _NetBlob_MakeBytes(const NetBlob* Arg);
NetBytes* _NetList_MakeBytes(const NetList* Arg);

NetItem* _NetItem_Create(NetArg* Arg);
void _NetItem_Destroy(NetItem* Item);
void _NetList_Destroy(NetArg* Arg);
NetArgHeader* _NetArg_Pack(const NetArg* Arg);	// - Convert from host to network order
NetArg* _NetArg_Unpack(const NetArgHeader* Arg, int Len); // - Convert from network to host order
int _NetArg_PackedSize(NetArgHeader* Arg);

uint64_t _RpcClient_HashString(const char* szStr);

#ifdef _MSC_VER
#pragma pack(push, 1)
#else
__attribute__((__packed__))
#endif
struct _NetArgHeader
{
	char type;
	NetBytes bytes;
};
#ifdef _MSC_VER
#pragma pack(pop)
#endif

struct _RemoteMethod
{
	RemoteMethod* next;
	char* name;
	uint64_t hash;
	char* fmt;
	RpcClient_Callback call;
};

struct _RemoteClass
{
	RemoteMethod* head, * tail;
	int count;
};

struct _NetArg
{
	char type;
	NetArg_Destroy_t Destroy;
	NetArg_MakeBytes_t _MakeBytes;
};

// IMPORTANT: For sake of ease, use a single type (returnable by reference immediately after 'arg'!
//	This way, RpcClient can give the user a basic type instead of handing them the whole NetInt*/NetBlob* struct...

struct _NetInt
{
	NetArg arg;
	int64_t i_value;
};

struct _NetFloat
{
	NetArg arg;
	double f_value;
};

struct _NetString
{
	NetArg arg;
	char s_value[]; // Null-terminated
};

struct _NetBlob
{
	NetArg arg;
	NetBytes bytes;
};

struct _NetList
{
	NetArg arg;

	// ----- Read-only ----- //
	NetItem* head;
	NetItem* tail;
	int count;

	char* _fmtCache;
};

struct _NetItem
{
	NetItem* next;
	NetArg* arg;
};