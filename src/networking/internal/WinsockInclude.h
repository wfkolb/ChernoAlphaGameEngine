#pragma once
// Internal header — never include from public headers.
// Include this FIRST in any .cpp that needs Winsock to ensure correct include order.
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
