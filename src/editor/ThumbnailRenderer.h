#pragma once
#ifdef ENGINE_DEVREL

#include <filesystem>
#include <string>
#include <unordered_map>

using ImTextureID = void*;

namespace engine::rendering { class GpuDevice; }

namespace engine::editor {

class ThumbnailRenderer {
public:
    ThumbnailRenderer() = default;
    ~ThumbnailRenderer() = default;

    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    // Must be called while a frame is open (after GpuDevice::beginFrame()).
    void init(rendering::GpuDevice& device);

    // Loads and caches a thumbnail for path. Synchronous on first call.
    // Subsequent calls for the same path are no-ops.
    void requestThumbnail(const std::filesystem::path& path);

    // Returns cached ImTextureID, or nullptr if path has not been rendered.
    ImTextureID getImGuiTexture(const std::filesystem::path& path) const;

    bool isInitialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
    rendering::GpuDevice* device_ = nullptr;
    std::unordered_map<std::string, ImTextureID> cache_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
