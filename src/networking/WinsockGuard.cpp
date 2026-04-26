// Must be first â€” ensures Winsock is included before <windows.h>.
#include "WinsockInclude.h"

#include "networking/WinsockGuard.h"
#include <core/diag/Assert.h>

namespace engine::networking {

WinsockGuard::WinsockGuard() {
    WSADATA wsaData{};
    const int result = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    ENGINE_ASSERT(result == 0, "WSAStartup failed â€” Winsock 2.2 is required");
    // In release builds ENGINE_ASSERT is a no-op; treat WSAStartup failure as
    // a hard abort via ENGINE_VERIFY to catch misconfigured Windows installs.
    ENGINE_VERIFY(result == 0, "WSAStartup failed â€” Winsock 2.2 is required");
}

WinsockGuard::~WinsockGuard() {
    ::WSACleanup();
}

} // namespace engine::networking
