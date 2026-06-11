#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::test {

struct PixelReadback {
    uint32_t             width;
    uint32_t             height;
    std::vector<uint8_t> pixels;  // RGBA8_UNORM, row-major
};

// Load a PNG from disk into a PixelReadback. Returns false on failure.
bool loadPng(const std::filesystem::path& path, PixelReadback& out);

// Save a PixelReadback as PNG. Returns false on failure.
bool savePng(const std::filesystem::path& path, const PixelReadback& pixels);

// Compute per-channel RMSE between actual and expected.
// Returns the maximum RMSE across R, G, B channels (ignores alpha).
// Both must have the same dimensions.
float computeRmse(const PixelReadback& actual, const PixelReadback& expected);

// Assert that actual matches the golden image at goldenPath within rmseThreshold.
// If goldenPath does not exist, writes actual to goldenPath and returns true
// (first-run baseline creation mode).
// Prints a descriptive message on failure; returns false when the threshold is exceeded.
bool assertMatchesGolden(const PixelReadback& actual,
                         const std::filesystem::path& goldenPath,
                         float rmseThreshold = 0.02f);

} // namespace engine::test
