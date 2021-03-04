#include "rpcclient.h"
#include <stdio.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib") // MSVC only, manually link if other compiler...
typedef INT_PTR socket_t;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
typedef int socket_t;
#endif

#define PORT 11223
socket_t sock = -1, client = -1;

socket_t CreateListener(unsigned short Port);

int Say_IntString(NetList* Args)
{
	NetInt* i;
	NetString* str;
	NetFloat* fl;
	if (!NetList_GetArgs(Args, "isf", &i, &str, &fl))
		return RemoteError_BadArgsType;

	printf("Say_IntString: %d, '%s', %15.15f\n", i->i_value, str->s_value, fl->f_value);
	return RemoteError_Ok;
}

int main()
{
	if ((sock = CreateListener(PORT)) < 0)
	{
		puts("Failed to create listener");
		return -1;
	}
	printf("Created listener %d\n", sock);
	
	RpcClient_Open();
	RpcClient_AddMethod(&Say_IntString, "Say_IntString", "isf");

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(PORT);
	
	size_t addrlen = sizeof(addr);
	while ((client = accept(sock, &addr, &addrlen)) >= 0)
	{
		printf("Accepted client %d\n", client);
		RpcClient_Recv();

		RpcClient_Call("wazzap", "sf",  "TestString", 0.15);
	}
	puts("Closing server");
	return 0;
}

// - Fill 'Len' bytes into RpcClient's buffer, or return false
int RpcClient_Read_Impl(char* Buffer, int Len)
{
        int read = 0;
        while (read < Len)
        {
                int count = recv(client, Buffer + read, Len - read, 0);
                if (count <= 0)
			break;
		read += count;
        }
        return read == Len;
}

// - Send RpcClient's buffer
int RpcClient_Send_Impl(char* Buffer, int Len) {
        return send(client, Buffer, Len, 0);
}

socket_t CreateListener(unsigned short Port)
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
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("Error creating socket");
		return -1;
	}

	int opt;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
        {
                puts("Error calling setsocketopt()");
                return -1;
        }

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(Port);

	if (bind(sock, &addr, sizeof(addr)) < 0)
	{
		puts("Error binding socket");
		return -1;
	}

	if (listen(sock, 3) < 0)
	{
		puts("Error listening on socket");
		return -1;
	}
	return sock;
}
