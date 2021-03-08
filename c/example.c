#include "example.h"

socket_t sock_main = -1;
socket_t CreateSocket(const char* HostString, unsigned short Port);
int srv_main();

// - Example remote-callable method
int wazzap(const uint8_t* Args, int Len)
{
	const char* str;
	float fl;
	if (NetStruct_UnpackFmt(Args, Len, "sf", &str, &fl) <= 0)
		return RpcCode_BadRemoteCall;

	printf("wazzap(): '%s', %15.15f\n", str, fl);
	return RpcCode_Ok;
}

/*
* 
* main()
* 
*	Try running the C# demo server, then open this client and watch them communicate!
* 
*/
int main(int argc, char** argv)
{
	if (argc < 2 || strcmp(argv[1], "-client"))
		return srv_main();

	puts("Running client example");
	if ((sock_main = CreateSocket(HOST, PORT)) == -1)
	{
		puts("Failed to connect to server");
		return -1;
	}

	RpcClient_Open();
	RpcClient_AddMethod(&wazzap, "wazzap"); // Add method 'wazzap(string, float)' to client

	// Call the server's 'Say_IntString(int, string, float)' method
	RpcClient_Call("Say_IntString", "isf", 23395, "gANGSTA!", 4.0000006288320268);

	// Wait for and process exactly one remote call from the server
	RpcClient_Recv();

	RpcClient_Close();

	return 0;
}

// - Fill 'Len' bytes into RpcClient's buffer, or return false
int RpcClient_Read_Impl(char* Buffer, int Len)
{
	int read = 0;
	while (read < Len)
	{
		int count = recv(sock_main, Buffer + read, Len - read, 0);
		if (count <= 0)
			break;
		read += count;
	}
	return read == Len;
}

// - Send RpcClient's buffer
int RpcClient_Send_Impl(char* Buffer, int Len) {
	return send(sock_main, Buffer, Len, 0);
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
		puts("Error creating socket");
#ifdef _WIN32
		printf("socket() failed: %d\n", WSAGetLastError());
#endif
		return -1;
	}

	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, HOST, &addr.sin_addr) <= 0)
	{
		puts("Error converting string address \"" HOST "\"");
		return -1;
	}

	if (connect(sock, &addr, sizeof(addr)) < 0)
	{
		puts("Error connecting socket");
		return -1;
	}

	return sock;
}
