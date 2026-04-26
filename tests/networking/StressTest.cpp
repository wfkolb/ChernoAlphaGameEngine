#include <gtest/gtest.h>
#include "networking/Session.h"
#include "networking/WinsockGuard.h"
#include <atomic>
#include <chrono>
#include <thread>

class NetworkingStressTest : public ::testing::Test {
protected:
    engine::networking::WinsockGuard wsGuard_;
};

TEST_F(NetworkingStressTest, tenThousandMessagesDelivered) {
    auto [server, client] = engine::networking::Session::createLocalPair(17888);

    // Server echoes every received message back to the client.
    server.onMessage([&](const engine::networking::Endpoint&,
                         std::span<const uint8_t> data) {
        server.send(client.localEndpoint(), data);
    });

    // Client counts every echo it receives.
    std::atomic<int> clientReceived{0};
    client.onMessage([&](const engine::networking::Endpoint&,
                         std::span<const uint8_t>) {
        ++clientReceived;
    });

    // Send 10,000 messages from client to server.
    const engine::networking::Endpoint serverEndpoint = server.localEndpoint();
    for (int seq = 0; seq < 10000; ++seq) {
        const uint8_t payload[8] = {
            0xA0, 0xA1, 0xA2, 0xA3,
            static_cast<uint8_t>(seq & 0xFF),
            static_cast<uint8_t>((seq >> 8) & 0xFF),
            0x00, 0x00
        };
        client.send(serverEndpoint, {payload, sizeof(payload)});
    }

    // Poll until all echoes arrive or a 10-second deadline is reached.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (clientReceived.load() < 10000 &&
           std::chrono::steady_clock::now() < deadline) {
        server.poll();
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Reliable channel guarantees at-least-once delivery; retransmits may cause
    // duplicates, so accept any count >= 10000.
    EXPECT_GE(clientReceived.load(), 10000);
}
