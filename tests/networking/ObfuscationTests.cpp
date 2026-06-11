#include <gtest/gtest.h>
#include <networking/PacketObfuscation.h>
#include <vector>

using namespace engine::networking;

TEST(ObfuscationTests, RoundTrip) {
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    const std::vector<uint8_t> key = {0xAB, 0xCD};
    const std::vector<uint8_t> original = data;
    xorObfuscate(data, key);
    EXPECT_NE(data, original);
    xorObfuscate(data, key); // decode
    EXPECT_EQ(data, original);
}

TEST(ObfuscationTests, EmptyKeyNoOp) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    const std::vector<uint8_t> original = data;
    xorObfuscate(data, std::span<const uint8_t>{});
    EXPECT_EQ(data, original);
}

TEST(ObfuscationTests, PartialKeyAlignment) {
    std::vector<uint8_t> data(100, 0xAA);
    const std::vector<uint8_t> key = {0x01, 0x02, 0x03};
    const std::vector<uint8_t> original = data;
    xorObfuscate(data, key);
    xorObfuscate(data, key);
    EXPECT_EQ(data, original);
}
