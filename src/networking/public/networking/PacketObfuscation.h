#pragma once
#include <span>
#include <cstdint>

namespace engine::networking {

// NOT cryptographic security. XOR obfuscation only — a future DTLS layer
// should replace this entirely. See scope-networking.md §security posture.
void xorObfuscate(std::span<uint8_t> packet, std::span<const uint8_t> key);

} // namespace engine::networking
