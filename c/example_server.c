#include "example.h"

socket_t CreateListener(unsigned short Port);

int Say_IntString(RpcClient* Client, const uint8_t* Args, int Len)
{
	int i;
	const char* str;
	float fl;
	if (NetStruct_UnpackFmt(Args, Len, "isf", &i, &str, &fl) <= 0)
		return RpcCode_BadRemoteCall;

	printf("Say_IntString: %d, '%s', %15.15f\n", i, str, fl);
	return RpcCode_Ok;
}

int srv_main()
{
	puts("Running server example");

	socket_t sock_listen, sock_client;
	RpcClient client;
	RemoteClass klass;

	if ((sock_listen = CreateListener(PORT)) == -1)
	{
		puts("Failed to create listener");
		return -1;
	}
	puts("Created listener");
	
	RemoteClass_Create(&klass);
	RemoteClass_AddMethod(&klass, &Say_IntString, "Say_IntString");

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htobe16(PORT);
	
	size_t addrlen = sizeof(addr);
	while (1)
	{
		sock_client = accept(sock_listen, &addr, &addrlen);
		if (sock_client == -1 || sock_client < 0)
			break;

		puts("Accepted client");
		RpcClient_Create(&client, &klass, (void*)sock_client);
		RpcClient_Recv(&client);

		RpcClient_Call(&client, "wazzap", "sf",  "TestString", 0.15);
		closesocket(sock_client);
	}

	RemoteClass_Destroy(&klass);

	puts("Closing server");
	return 0;
}

socket_t CreateListener(unsigned short Port)
{
#ifdef _WIN32
	WSADATA data;
	int result = WSAStartup(MAKEWORD(2, 2), &data);
	if (result)
	{
		printf("WSAStartup() failed: %d\n", result);
		return -1;
	}
#endif

	socket_t sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 || sock == -1)
	{
		printf("Error creating socket");
		return -1;
	}

	int opt;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
	{
		puts("Error calling setsocketopt()");
		return -1;
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htobe16(Port);

	if (bind(sock, &addr, sizeof(addr)))
	{
		puts("Error binding socket");
		return -1;
	}

	if (listen(sock, 3))
	{
		puts("Error listening on socket");
		return -1;
	}
	return sock;
}
