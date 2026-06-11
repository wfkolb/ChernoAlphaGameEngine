// tests/rendering/support/GoldenCompare.cpp
// Golden-image comparison utilities for integration tests.
// PNG I/O uses stb_image / stb_image_write (implementations in StbImpl.cpp).

#include "GoldenCompare.h"

// stb headers — implementations are in StbImpl.cpp; include without defines here.
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace engine::test {

// ---------------------------------------------------------------------------
// loadPng
// ---------------------------------------------------------------------------

bool loadPng(const std::filesystem::path& path, PixelReadback& out) {
    int w = 0, h = 0, channels = 0;
    // Force RGBA8 output regardless of the stored channel count.
    stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        return false;
    }

    out.width  = static_cast<uint32_t>(w);
    out.height = static_cast<uint32_t>(h);
    const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    out.pixels.assign(data, data + byteCount);

    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// savePng
// ---------------------------------------------------------------------------

bool savePng(const std::filesystem::path& path, const PixelReadback& pixels) {
    // Ensure parent directory exists.
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

    const int stride = static_cast<int>(pixels.width) * 4;
    const int result = stbi_write_png(
        path.string().c_str(),
        static_cast<int>(pixels.width),
        static_cast<int>(pixels.height),
        4,
        pixels.pixels.data(),
        stride);
    return result != 0;
}

// ---------------------------------------------------------------------------
// computeRmse
// ---------------------------------------------------------------------------

float computeRmse(const PixelReadback& actual, const PixelReadback& expected) {
    if (actual.width  != expected.width ||
        actual.height != expected.height) {
        // Dimension mismatch — return a large error to signal failure.
        return 1.0f;
    }

    const size_t pixelCount = static_cast<size_t>(actual.width) *
                              static_cast<size_t>(actual.height);

    double sumSqR = 0.0, sumSqG = 0.0, sumSqB = 0.0;
    for (size_t i = 0; i < pixelCount; ++i) {
        // Normalise channels to [0, 1] before computing error.
        const double aR = actual.pixels[i * 4 + 0] / 255.0;
        const double aG = actual.pixels[i * 4 + 1] / 255.0;
        const double aB = actual.pixels[i * 4 + 2] / 255.0;
        const double eR = expected.pixels[i * 4 + 0] / 255.0;
        const double eG = expected.pixels[i * 4 + 1] / 255.0;
        const double eB = expected.pixels[i * 4 + 2] / 255.0;

        sumSqR += (aR - eR) * (aR - eR);
        sumSqG += (aG - eG) * (aG - eG);
        sumSqB += (aB - eB) * (aB - eB);
    }

    const double invN = 1.0 / static_cast<double>(pixelCount);
    const float rmseR = static_cast<float>(std::sqrt(sumSqR * invN));
    const float rmseG = static_cast<float>(std::sqrt(sumSqG * invN));
    const float rmseB = static_cast<float>(std::sqrt(sumSqB * invN));

    return std::max({rmseR, rmseG, rmseB});
}

// ---------------------------------------------------------------------------
// assertMatchesGolden
// ---------------------------------------------------------------------------

bool assertMatchesGolden(const PixelReadback& actual,
                         const std::filesystem::path& goldenPath,
                         float rmseThreshold) {
    if (!std::filesystem::exists(goldenPath)) {
        // First-run baseline creation: save the actual image as the golden.
        if (!savePng(goldenPath, actual)) {
            std::printf("[GoldenCompare] ERROR: could not write baseline to %s\n",
                        goldenPath.string().c_str());
            return false;
        }
        std::printf("[GoldenCompare] Baseline created: %s\n",
                    goldenPath.string().c_str());
        return true;
    }

    PixelReadback expected{};
    if (!loadPng(goldenPath, expected)) {
        std::printf("[GoldenCompare] ERROR: could not load golden from %s\n",
                    goldenPath.string().c_str());
        return false;
    }

    if (actual.width  != expected.width ||
        actual.height != expected.height) {
        std::printf("[GoldenCompare] FAIL: dimension mismatch — actual %ux%u vs golden %ux%u (%s)\n",
                    actual.width, actual.height,
                    expected.width, expected.height,
                    goldenPath.string().c_str());
        return false;
    }

    const float rmse = computeRmse(actual, expected);
    if (rmse > rmseThreshold) {
        std::printf("[GoldenCompare] FAIL: RMSE %.4f exceeds threshold %.4f for %s\n",
                    rmse, rmseThreshold, goldenPath.string().c_str());
        return false;
    }

    return true;
}

} // namespace engine::test
