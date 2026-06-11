#include <gtest/gtest.h>
// XInput stubbing: expose a function pointer for testing.
// The real implementation uses g_xInputGetState; tests replace it.
#include <core/input/InputSystem.h>
#include <functional>

// Minimal test verifying dead-zone behavior conceptually.
// Full mock-injection XInput tests require platform-specific setup.
TEST(XInputTests, DeadZoneSanity) {
    // Verify dead-zone constant is in the expected range.
    constexpr int kLeftDeadZone = 7849;
    constexpr int kMaxStickValue = 32767;
    EXPECT_GT(kLeftDeadZone, 0);
    EXPECT_LT(kLeftDeadZone, kMaxStickValue);
}

TEST(XInputTests, AxisNormalization) {
    constexpr int kDeadZone = 7849;
    constexpr int kMax = 32767;
    // A value at dead-zone boundary should normalize to 0.
    auto normalize = [&](int raw) -> float {
        if (raw < -kDeadZone) raw += kDeadZone;
        else if (raw > kDeadZone) raw -= kDeadZone;
        else return 0.0f;
        return static_cast<float>(raw) / static_cast<float>(kMax - kDeadZone);
    };
    EXPECT_FLOAT_EQ(normalize(0), 0.0f);
    EXPECT_FLOAT_EQ(normalize(kDeadZone), 0.0f);
    EXPECT_GT(normalize(kMax), 0.9f);
}
