#ifdef ENGINE_DEVREL

#include "editor/EditorImporter.h"

#include <tools/AssetImporter.h>
#include <core/log.h>

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace engine::editor {

namespace {

static uint64_t currentFileMTime(const std::filesystem::path& p) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER li;
    li.LowPart  = fad.ftLastWriteTime.dwLowDateTime;
    li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return li.QuadPart;
#else
    std::error_code ec;
    auto t = std::filesystem::last_write_time(p, ec);
    return ec ? 0u : static_cast<uint64_t>(t.time_since_epoch().count());
#endif
}

EditorImporter::Result doImport(std::filesystem::path source,
                                std::filesystem::path outputDir,
                                AssetImportSettings   settings) {
    EditorImporter::Result result;

    // Build output path: <outputDir>/<stem>.easset
    const std::filesystem::path eassetPath =
        outputDir / (source.stem().string() + ".easset");

    LOG_INFO("EditorImporter: importing '{}' → '{}'",
             source.string(), eassetPath.string());

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    // Map editor AssetImportSettings → tools::ImportSettings.
    tools::ImportSettings importSettings;
    importSettings.generateCollision = settings.generateCollision;
    importSettings.collisionType     = settings.collisionType;

    const auto importResult = tools::importGltf(source, eassetPath, importSettings);
    if (!importResult.ok) {
        result.succeeded    = false;
        result.errorMessage = importResult.errorMessage;
        LOG_WARN("EditorImporter: import failed: {}", importResult.errorMessage);
        return result;
    }

    // Write .meta sidecar.
    AssetMeta meta;
    meta.sourcePath    = source.string();
    meta.sourceModTime = currentFileMTime(source);
    meta.settings      = settings;
    MetaFileWriter::write(eassetPath, meta);

    result.succeeded  = true;
    result.eassetPath = eassetPath;
    return result;
}

} // anonymous namespace

bool EditorImporter::beginImport(const std::filesystem::path& sourcePath,
                                  const std::filesystem::path& outputDir,
                                  const AssetImportSettings&   settings) {
    if (isImporting()) return false;

    future_ = std::async(std::launch::async, doImport,
                         sourcePath, outputDir, settings);
    return true;
}

void EditorImporter::tick(const std::function<void(const Result&)>& onResult) {
    if (!future_) return;

    // Non-blocking check: is the future ready?
    if (future_->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

    Result res = future_->get();
    future_.reset();
    if (onResult) onResult(res);
}

bool EditorImporter::isImporting() const {
    if (!future_) return false;
    return future_->wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
