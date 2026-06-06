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

// Selects the collision shape type generated when generateCollision == true.
enum class CollisionType : uint8_t {
    TriangleMesh = 0, // Exact triangle soup — best for static level geometry.
    ConvexHull   = 1, // Convex hull — best for dynamic props / pickups.
};

// Settings that control how importGltf() processes and serialises the mesh.
// All fields have sensible defaults so callers that don't need collision can
// construct ImportSettings{} (or rely on the default-argument overload).
struct ImportSettings {
    bool          generateCollision = false;
    CollisionType collisionType     = CollisionType::TriangleMesh;
};

// Import a glTF 2.0 file and write a .easset file.
// source   : path to .gltf / .glb file
// output   : path to output .easset file
// settings : optional import settings (default: no collision)
// Returns ImportResult with ok=true on success.
ImportResult importGltf(const std::filesystem::path& source,
                        const std::filesystem::path& output,
                        const ImportSettings&        settings = {});

} // namespace engine::tools
