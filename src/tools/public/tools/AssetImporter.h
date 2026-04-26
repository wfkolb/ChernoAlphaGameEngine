#pragma once
#include <filesystem>
#include <string>
#include <cstdint>

namespace engine::tools {

// Result type for asset import operations.
struct ImportResult {
    bool        ok = false;
    std::string errorMessage;
};

// Import a glTF 2.0 file and write a .easset file.
// source : path to .gltf file
// output : path to output .easset file
// Returns ImportResult with ok=true on success.
ImportResult importGltf(const std::filesystem::path& source,
                        const std::filesystem::path& output);

} // namespace engine::tools
