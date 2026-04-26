#pragma once
#include <core/diag/Assert.h>

namespace engine::networking {

    // RAII wrapper for WSAStartup / WSACleanup.
    //
    // One WinsockGuard must be alive for the entire duration that any Socket
    // or Endpoint operation is performed.  Typically owned by Session and
    // lives for the session's lifetime.
    //
    // Construction asserts (ENGINE_ASSERT) on WSAStartup failure.
    // Destruction calls WSACleanup exactly once.
    class WinsockGuard {
    public:
        WinsockGuard();
        ~WinsockGuard();

        ENGINE_NO_COPY(WinsockGuard);
        ENGINE_NO_MOVE(WinsockGuard);
    };

} // namespace engine::networking
