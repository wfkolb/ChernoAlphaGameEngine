#pragma once
#ifdef ENGINE_DEVREL

#include "editor/MetaFileWriter.h"

#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>

namespace engine::editor {

// Background glTF import. Wraps tools::importGltf() and writes the .easset.meta
// sidecar. One import runs at a time; enqueue() while busy returns false.
class EditorImporter {
public:
    struct Result {
        bool                  succeeded  = false;
        std::filesystem::path eassetPath;
        std::string           errorMessage;
    };

    // Begin an import in the background. Returns false if already importing.
    // outputDir: directory to write <stem>.easset into.
    bool beginImport(const std::filesystem::path& sourcePath,
                     const std::filesystem::path& outputDir,
                     const AssetImportSettings&   settings);

    // Must be called every frame. Resolves the future and delivers the result
    // to onResult if the background job finished.
    void tick(const std::function<void(const Result&)>& onResult);

    bool isImporting() const;

private:
    std::optional<std::future<Result>> future_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
