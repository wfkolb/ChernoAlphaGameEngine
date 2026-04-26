#include <gtest/gtest.h>
#include "networking/Session.h"
#include "networking/WinsockGuard.h"
#include <chrono>
#include <thread>

class NetworkingLoopbackTest : public ::testing::Test {
protected:
    engine::networking::WinsockGuard wsGuard_;
};

TEST_F(NetworkingLoopbackTest, pingPongRoundTrip) {
    auto [server, client] = engine::networking::Session::createLocalPair(17777);

    bool serverReceived = false;
    server.onMessage([&](const engine::networking::Endpoint&,
                         std::span<const uint8_t> data) {
        EXPECT_EQ(data.size(), 4u);
        EXPECT_EQ(data[0], 0xDE);
        serverReceived = true;
        server.send(client.localEndpoint(), data);
    });

    bool clientReceived = false;
    client.onMessage([&](const engine::networking::Endpoint&,
                         std::span<const uint8_t> data) {
        EXPECT_EQ(data.size(), 4u);
        EXPECT_EQ(data[0], 0xDE);
        clientReceived = true;
    });

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    client.send(server.localEndpoint(), {payload, sizeof(payload)});

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (!clientReceived && std::chrono::steady_clock::now() < deadline) {
        server.poll();
        client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_TRUE(serverReceived);
    EXPECT_TRUE(clientReceived);
}
