#include "rpcclient.h"
#include <stdio.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib") // MSVC only, manually link if other compiler...
typedef INT_PTR socket_t;
#else
#include <sys/socket>
#include <arpa/inet.h> 
typedef int socket_t;
#endif

#define HOST "127.0.0.1"
#define PORT 11223
socket_t sock = 0;

socket_t CreateSocket(const char* HostString, unsigned short Port);

// - Example remote-callable method
int wazzap(NetList* Args)
{
	NetString* str;
	NetFloat* fl;
	if (!NetList_GetArgs(Args, "sf", &str, &fl))
		return RemoteError_BadArgsType;

	printf("wazzap(): '%s', %15.15f\n", str->s_value, fl->f_value);
	return RemoteError_Ok;
}

/*
* 
* main()
* 
*	Try running the C# demo server, then open this client and watch them communicate!
* 
*/
int main()
{
	if ((sock = CreateSocket(HOST, PORT)) < 0)
	{
		puts("Failed to create socket\n");
		return -1;
	}

	RpcClient_Open();
	RpcClient_AddMethod(&wazzap, "wazzap", "sf"); // Add method 'wazzap(string, float)' to client

	// Call the server's 'Say_IntString(int, string, float)' method
	RpcClient_Call("Say_IntString", "isf", 80085, "gANGSTA!", 4.200026688320268);
	RpcClient_Recv(); // Wait for and process exactly one remote call from a peer
	RpcClient_Call("CloseServer", 0);
	RpcClient_Recv();

	return 0;
}

// - Fill 'Len' bytes into RpcClient's buffer, or return false
int RpcClient_Read_Impl(char* Buffer, int Len)
{
	int read = 0;
	while (read < Len)
	{
		int count = recv(sock, Buffer + read, Len - read, 0);
		if (count <= 0)
			break;
		read += count;
	}
	return read == Len;
}

// - Send RpcClient's buffer
int RpcClient_Send_Impl(char* Buffer, int Len) {
	return send(sock, Buffer, Len, 0);
}

socket_t CreateSocket(const char* HostString, unsigned short Port)
{
#ifdef _WIN32
	WSADATA data;
	int result = WSAStartup(MAKEWORD(2, 2), &data);
	if (result != 0)
	{
		printf("WSAStartup() failed: %d\n", result);
		return -1;
	}
#endif
	
	socket_t sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		puts("Error creating socket\n");
		printf("socket() failed: %d\n", WSAGetLastError());
		return -1;
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, HOST, &addr.sin_addr) <= 0)
	{
		puts("Error converting string address \"" HOST "\"\n");
		return -1;
	}

	if (connect(sock, &addr, sizeof(addr)) < 0)
	{
		puts("Error connecting socket\n");
		return -1;
	}

	return sock;
}
