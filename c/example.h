#pragma once
#include "include/rpcclient.h"
#include "NetStruct/include/netstruct_endian.h"
#include <stdio.h>

#ifdef _WIN32
	#include <WinSock2.h>
	#include <WS2tcpip.h>
	#pragma comment(lib, "Ws2_32.lib") // MSVC only, manually link if other compiler...
	typedef SOCKET socket_t;
#else
	#include <sys/socket.h>
	#include <arpa/inet.h>
	typedef int socket_t;
	#define closesocket(s) close(s)
#endif

#define HOST "127.0.0.1"
#define PORT 11223
