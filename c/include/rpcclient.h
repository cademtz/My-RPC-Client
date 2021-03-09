#pragma once
#include <stdint.h>
#include "../NetStruct/include/netstruct.h"

typedef struct _remoteclass RemoteClass;
typedef struct _rpcclient RpcClient;
typedef int(*RpcClient_Callback)(RpcClient* Client, const uint8_t* Args, int Len);

enum ERemoteError
{
	RpcCode_Ok,
	RpcCode_BadCall,		// - Incorrect args or client was not set up correctly
	RpcCode_BadRemoteCall,	// - Received bad data from peer
	RpcCode_BadConnection,	// - Data failed to send or receive
	RpcCode_InternalError,
};

void RpcClient_Create(RpcClient* Client, const RemoteClass* Class, void* Socket);
void RpcClient_Destroy(RpcClient* Client);

void RemoteClass_Create(RemoteClass* Class);
void RemoteClass_Destroy(RemoteClass* Class);
void RemoteClass_AddMethod(RemoteClass* Class, RpcClient_Callback Callback, const char* Name);

int RpcClient_Call(RpcClient* Client, const char* Method, const char* FmtArgs, ...);
int RpcClient_Recv(RpcClient* Client);

// - Block until 'Len' bytes are read to 'Buffer'
// - Return 0 if no more data is available from the connection
extern int RpcClient_Read_Impl(RpcClient* Client, char* Buffer, int Len);
extern int RpcClient_Send_Impl(RpcClient* Client, char* Buffer, int Len);

uint64_t _RpcClient_HashString(const char* szStr);

typedef struct _remotemethod
{
	struct _remotemethod* next;
	char* name;
	uint64_t hash;
	RpcClient_Callback call;
} RemoteMethod;

typedef struct _remoteclass
{
	RemoteMethod* head, * tail;
	int count;
} RemoteClass;

typedef struct _rpcclient
{
	const RemoteClass* klass;
	void* socket;
} RpcClient;