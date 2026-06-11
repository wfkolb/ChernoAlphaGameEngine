#pragma once
#include <cstdint>

namespace engine::networking {

struct InputMessage {
    uint32_t tick       { 0 };
    int8_t   moveX      { 0 };   // -127..127 normalized
    int8_t   moveZ      { 0 };   // -127..127 normalized
    int16_t  yawDelta   { 0 };   // 1/100 degree units
    int16_t  pitchDelta { 0 };   // 1/100 degree units
    uint8_t  buttons    { 0 };   // bit0=jump bit1=fire bit2=crouch bit3=interact
    uint8_t  fireSerial { 0 };   // wrapping counter for fire de-dup
};

} // namespace engine::networking
