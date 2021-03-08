#pragma once
#include <stdint.h>
#include "../NetStruct/include/netstruct.h"

typedef int(*RpcClient_Callback)(const uint8_t* Args, int Len);

enum ERemoteError
{
	RpcCode_Ok,
	RpcCode_BadCall,		// - Incorrect args or client was not set up correctly
	RpcCode_BadRemoteCall,	// - Received bad data from peer
	RpcCode_InternalError,
};

void RpcClient_Open(void); // - Initialize necessary RPC Client variables
void RpcClient_Close(void); // - Clean up RPC Client variables

int RpcClient_Call(const char* Method, const char* FmtArgs, ...);
int RpcClient_Recv(void);
void RpcClient_AddMethod(RpcClient_Callback Callback, const char* Name);

// - Must fill 'Buffer' up to 'Len' bytes
// - Return 0 if no more data is available from the connection
extern int RpcClient_Read_Impl(char* Buffer, int Len);
extern int RpcClient_Send_Impl(char* Buffer, int Len);

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
