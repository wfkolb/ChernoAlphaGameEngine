#ifdef ENGINE_DEVREL

#include "editor/ThumbnailRenderer.h"

#include <core/diag/Assert.h>
#include <core/log.h>
#include <tools/EassetLoader.h>

namespace engine::editor {

void ThumbnailRenderer::init(rendering::GpuDevice& device) {
    ENGINE_ASSERT(!initialized_, "ThumbnailRenderer::init called twice");
    device_ = &device;
    initialized_ = true;
}

void ThumbnailRenderer::requestThumbnail(const std::filesystem::path& path) {
    ENGINE_ASSERT(initialized_, "ThumbnailRenderer::requestThumbnail called before init");

    const std::string key = path.string();
    if (cache_.count(key)) return;

    LOG_TRACE("ThumbnailRenderer: requesting thumbnail for '{}'", key);

    auto mesh = tools::loadEasset(path);
    if (!mesh) {
        LOG_WARN("ThumbnailRenderer: loadEasset failed for '{}'", key);
        cache_.emplace(key, nullptr);
        return;
    }

    // Off-screen RT + SRV descriptor heap setup is deferred to Phase 9.
    // Sentinel non-null value signals "asset loaded OK" to the Asset Browser.
    cache_.emplace(key, reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(1)));
}

ImTextureID ThumbnailRenderer::getImGuiTexture(const std::filesystem::path& path) const {
    const auto it = cache_.find(path.string());
    if (it == cache_.end()) return nullptr;
    return it->second;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
