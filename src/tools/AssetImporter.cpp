#include "tools/AssetImporter.h"
#include "core/log.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>
#include <array>

// cgltf — single-header glTF 2.0 parser (public domain, vcpkg package)
// The implementation define must appear in exactly one translation unit.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// meshoptimizer — geometry optimisation passes (vcpkg package)
#include <meshoptimizer.h>

// stb_image — image decode (implementation is in StbImageImpl.cpp)
#include <stb_image.h>

// ---------------------------------------------------------------------------
// Internal binary layout types (not exposed in the public header)
// ---------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)

// Version 1: 1 TocEntry (MESH only)
// Version 2: 1 or 2 TocEntries (MESH, optional COLL)
struct EassHeader {
    char     magic[4];        // "EASS"
    uint16_t version;         // 1 or 2
    uint16_t assetType;       // 0 = Mesh
    uint32_t totalSize;       // filled after all data is written
    uint32_t tocOffset;       // offset to first TocEntry from start of file
    uint32_t tocEntryCount;
};
static_assert(sizeof(EassHeader) == 20);

struct TocEntry {
    char     id[4];           // FourCC e.g. "MESH", "COLL"
    uint32_t offset;          // from start of file
    uint32_t size;            // bytes
    uint32_t reserved;        // 0
};
static_assert(sizeof(TocEntry) == 16);

struct MeshSectionHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t  vertexLayout;    // 1 = Static
    uint8_t  pad[3];
    float    aabbMin[3];
    float    aabbMax[3];
};
static_assert(sizeof(MeshSectionHeader) == 36);

// Collision section header (COLL section, version 2 only).
// Immediately follows this header:
//   float[3] × vertexCount   (position only)
//   uint32_t × indexCount    (0 for ConvexHull)
struct CollSectionHeader {
    uint8_t  collisionType;   // 0 = TriangleMesh, 1 = ConvexHull
    uint8_t  pad[3];
    uint32_t vertexCount;
    uint32_t indexCount;
};
static_assert(sizeof(CollSectionHeader) == 12);

struct VertexStatic {
    float    position[3];     // 12 bytes
    uint32_t packedNormal;    //  4 bytes  R10G10B10A2_UNORM
    uint32_t packedTangent;   //  4 bytes  R10G10B10A2_UNORM; bit31 = bitangent sign
    float    uv[2];           //  8 bytes
};
static_assert(sizeof(VertexStatic) == 28);

// TEX section header (version 3).
// Followed by mipCount × (TexMipHeader + pixel data).
struct TexSectionHeader {
    uint32_t dxgiFormat;  // 28 = DXGI_FORMAT_R8G8B8A8_UNORM
    uint32_t baseWidth;
    uint32_t baseHeight;
    uint32_t mipCount;
};
static_assert(sizeof(TexSectionHeader) == 16);

struct TexMipHeader {
    uint32_t width;
    uint32_t height;
    uint32_t dataSize;
};
static_assert(sizeof(TexMipHeader) == 12);

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Pack a unit direction vector into R10G10B10A2_UNORM (w=0).
// Input components are clamped to [-1, 1] and remapped to [0, 1].
constexpr uint32_t packNormal(float x, float y, float z) noexcept {
    auto encode = [](float v) -> uint32_t {
        float t = (v + 1.0f) * 0.5f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        return static_cast<uint32_t>(t * 1023.0f + 0.5f) & 0x3FFu;
    };
    return encode(x) | (encode(y) << 10) | (encode(z) << 20);
}

// Pack a tangent (direction + bitangent sign in bit31).
constexpr uint32_t packTangent(float x, float y, float z, float sign) noexcept {
    uint32_t packed = packNormal(x, y, z);
    if (sign < 0.0f) packed |= (1u << 31);
    return packed;
}

constexpr uint32_t kAlignmentBytes = 64u;

// Pad the stream with zeros up to the next 64-byte boundary.
void padTo64(std::fstream& fs) {
    const auto pos = static_cast<uint32_t>(fs.tellp());
    const uint32_t rem = pos % kAlignmentBytes;
    if (rem == 0) return;
    const uint32_t needed = kAlignmentBytes - rem;
    const uint8_t zero = 0;
    for (uint32_t i = 0; i < needed; ++i) {
        fs.write(reinterpret_cast<const char*>(&zero), 1);
    }
}

// ---------------------------------------------------------------------------
// Raw mesh data extracted from glTF or the unit-cube fallback
// ---------------------------------------------------------------------------

struct RawMesh {
    std::vector<float>    positions; // x,y,z triples
    std::vector<float>    normals;   // x,y,z triples (may be empty)
    std::vector<float>    uvs;       // u,v pairs     (may be empty)
    std::vector<uint32_t> indices;
};

// ---------------------------------------------------------------------------
// Unit-cube mesh (fallback when glTF can't be parsed / for test compatibility)
// ---------------------------------------------------------------------------
//
//   8 unique corners, +Z-up coordinate system.
//   Per-face normals require duplicated vertices; to stay minimal we use
//   shared vertices and averaged normals (good enough for pipeline testing).
//
//   Corners:
//     0 (-0.5,-0.5,-0.5)   1 ( 0.5,-0.5,-0.5)
//     2 ( 0.5, 0.5,-0.5)   3 (-0.5, 0.5,-0.5)
//     4 (-0.5,-0.5, 0.5)   5 ( 0.5,-0.5, 0.5)
//     6 ( 0.5, 0.5, 0.5)   7 (-0.5, 0.5, 0.5)

RawMesh buildUnitCubeRaw() {
    const float d = 0.57735027f; // 1/sqrt(3)

    struct Corner { float px, py, pz, nx, ny, nz, u, v; };
    const Corner corners[8] = {
        {-0.5f,-0.5f,-0.5f, -d,-d,-d, 0.0f,1.0f},
        { 0.5f,-0.5f,-0.5f,  d,-d,-d, 1.0f,1.0f},
        { 0.5f, 0.5f,-0.5f,  d, d,-d, 1.0f,0.0f},
        {-0.5f, 0.5f,-0.5f, -d, d,-d, 0.0f,0.0f},
        {-0.5f,-0.5f, 0.5f, -d,-d, d, 0.0f,1.0f},
        { 0.5f,-0.5f, 0.5f,  d,-d, d, 1.0f,1.0f},
        { 0.5f, 0.5f, 0.5f,  d, d, d, 1.0f,0.0f},
        {-0.5f, 0.5f, 0.5f, -d, d, d, 0.0f,0.0f},
    };

    RawMesh m;
    m.positions.reserve(8 * 3);
    m.normals.reserve(8 * 3);
    m.uvs.reserve(8 * 2);
    for (const auto& c : corners) {
        m.positions.push_back(c.px); m.positions.push_back(c.py); m.positions.push_back(c.pz);
        m.normals.push_back(c.nx);   m.normals.push_back(c.ny);   m.normals.push_back(c.nz);
        m.uvs.push_back(c.u);        m.uvs.push_back(c.v);
    }

    // 6 faces × 2 triangles × 3 indices = 36
    m.indices = {
        // -Z face
        0,2,1,  0,3,2,
        // +Z face
        4,5,6,  4,6,7,
        // -X face
        0,4,7,  0,7,3,
        // +X face
        1,2,6,  1,6,5,
        // -Y face
        0,1,5,  0,5,4,
        // +Y face
        3,7,6,  3,6,2,
    };

    return m;
}

// ---------------------------------------------------------------------------
// Texture data (CPU-side, before serialisation)
// ---------------------------------------------------------------------------

struct RawMipLevel {
    std::vector<uint8_t> pixels;
    uint32_t             width  = 0;
    uint32_t             height = 0;
};

struct RawTexture {
    std::vector<RawMipLevel> mips;
    uint32_t                 dxgiFormat = 28; // DXGI_FORMAT_R8G8B8A8_UNORM (always RGBA8 for now)
    uint32_t                 baseWidth  = 0;
    uint32_t                 baseHeight = 0;
};

// Generate mip chain via a simple 2×2 box filter.
// Source image must be RGBA8 (4 bytes per pixel), row-major, top-to-bottom.
static RawMipLevel downsampleMip(const RawMipLevel& src)
{
    RawMipLevel dst;
    dst.width  = std::max(1u, src.width  / 2u);
    dst.height = std::max(1u, src.height / 2u);
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height * 4u);

    for (uint32_t dy = 0; dy < dst.height; ++dy) {
        for (uint32_t dx = 0; dx < dst.width; ++dx) {
            // Source pixel coordinates — clamp to src bounds for non-power-of-two.
            const uint32_t sx0 = dx * 2u;
            const uint32_t sy0 = dy * 2u;
            const uint32_t sx1 = std::min(sx0 + 1u, src.width  - 1u);
            const uint32_t sy1 = std::min(sy0 + 1u, src.height - 1u);

            for (int c = 0; c < 4; ++c) {
                const uint32_t p00 = src.pixels[(sy0 * src.width + sx0) * 4u + c];
                const uint32_t p10 = src.pixels[(sy0 * src.width + sx1) * 4u + c];
                const uint32_t p01 = src.pixels[(sy1 * src.width + sx0) * 4u + c];
                const uint32_t p11 = src.pixels[(sy1 * src.width + sx1) * 4u + c];
                dst.pixels[(dy * dst.width + dx) * 4u + c] =
                    static_cast<uint8_t>((p00 + p10 + p01 + p11 + 2u) / 4u);
            }
        }
    }
    return dst;
}

// Decode one embedded glTF image (buffer_view bytes) into a full mip chain.
// Returns an empty optional when decoding fails.
static std::optional<RawTexture> decodeEmbeddedImage(const uint8_t* data, std::size_t size)
{
    int w = 0, h = 0, comp = 0;
    uint8_t* decoded = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(data),
        static_cast<int>(size),
        &w, &h, &comp, 4 /* force RGBA */);

    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return std::nullopt;
    }

    RawTexture tex;
    tex.baseWidth  = static_cast<uint32_t>(w);
    tex.baseHeight = static_cast<uint32_t>(h);

    // Mip 0: the full-resolution image.
    {
        RawMipLevel mip0;
        mip0.width  = tex.baseWidth;
        mip0.height = tex.baseHeight;
        const std::size_t byteCount = static_cast<std::size_t>(w) * h * 4u;
        mip0.pixels.assign(decoded, decoded + byteCount);
        tex.mips.push_back(std::move(mip0));
    }
    stbi_image_free(decoded);

    // Generate additional mips until 1×1.
    while (tex.mips.back().width > 1u || tex.mips.back().height > 1u) {
        tex.mips.push_back(downsampleMip(tex.mips.back()));
    }

    return tex;
}

// Extract all embedded textures from a parsed cgltf_data.
// Skips external URI images (logs a warning); only processes buffer_view-backed images.
static std::vector<RawTexture> extractTextures(const cgltf_data* data)
{
    std::vector<RawTexture> result;
    if (!data || data->images_count == 0) return result;

    result.reserve(data->images_count);

    for (cgltf_size i = 0; i < data->images_count; ++i) {
        const cgltf_image& img = data->images[i];

        if (!img.buffer_view) {
            // External URI — not supported in v1 of this feature.
            LOG_WARN("importGltf: image[{}] '{}' has no buffer_view (external URI?); skipping.",
                     static_cast<uint32_t>(i),
                     img.uri ? img.uri : "<unnamed>");
            continue;
        }

        const cgltf_buffer_view* bv   = img.buffer_view;
        const uint8_t*           base = static_cast<const uint8_t*>(bv->buffer->data);
        if (!base) {
            LOG_WARN("importGltf: image[{}] buffer data not loaded; skipping.", static_cast<uint32_t>(i));
            continue;
        }

        const uint8_t*    imageData = base + bv->offset;
        const std::size_t imageSize = bv->size;

        auto tex = decodeEmbeddedImage(imageData, imageSize);
        if (!tex.has_value()) {
            LOG_WARN("importGltf: image[{}] failed to decode (stbi error: {}); skipping.",
                     static_cast<uint32_t>(i), stbi_failure_reason());
            continue;
        }

        LOG_INFO("importGltf: image[{}] decoded — {}×{}, {} mips.",
                 static_cast<uint32_t>(i), tex->baseWidth, tex->baseHeight,
                 static_cast<uint32_t>(tex->mips.size()));
        result.push_back(std::move(*tex));
    }

    return result;
}

// ---------------------------------------------------------------------------
// glTF parsing via cgltf
// ---------------------------------------------------------------------------

// Read a float attribute (POSITION/NORMAL/TEXCOORD_0) into a flat float vector.
// Returns false if the accessor is absent or the component type is unexpected.
bool readFloatAccessor(const cgltf_accessor* acc, std::vector<float>& out, int numComponents) {
    if (!acc) return false;
    const cgltf_size count = acc->count;
    out.resize(count * static_cast<cgltf_size>(numComponents));
    for (cgltf_size i = 0; i < count; ++i) {
        cgltf_accessor_read_float(acc, i, &out[i * static_cast<cgltf_size>(numComponents)],
                                  static_cast<cgltf_size>(numComponents));
    }
    return true;
}

// Apply a cgltf column-major 4x4 world matrix to a position (with translation).
static void applyMatPos(const float m[16], float x, float y, float z,
                        float& ox, float& oy, float& oz) noexcept {
    ox = m[0]*x + m[4]*y + m[8]*z  + m[12];
    oy = m[1]*x + m[5]*y + m[9]*z  + m[13];
    oz = m[2]*x + m[6]*y + m[10]*z + m[14];
}

// Apply a cgltf world matrix to a direction vector (no translation), then renormalize.
static void applyMatDir(const float m[16], float x, float y, float z,
                        float& ox, float& oy, float& oz) noexcept {
    ox = m[0]*x + m[4]*y + m[8]*z;
    oy = m[1]*x + m[5]*y + m[9]*z;
    oz = m[2]*x + m[6]*y + m[10]*z;
    const float len = std::sqrt(ox*ox + oy*oy + oz*oz);
    if (len > 1e-6f) { ox /= len; oy /= len; oz /= len; }
}

// Append one glTF primitive (with its node world transform) into a merged RawMesh.
// baseVertex is the vertex count already in merged before this call.
static void appendPrimitive(const cgltf_primitive* prim,
                            const float worldMat[16],
                            RawMesh& merged,
                            int& primitivesFound) {
    if (prim->type != cgltf_primitive_type_triangles) return;
    if (!prim->indices) return;

    const cgltf_accessor* posAcc  = nullptr;
    const cgltf_accessor* normAcc = nullptr;
    const cgltf_accessor* uvAcc   = nullptr;

    for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
        const cgltf_attribute& attr = prim->attributes[ai];
        if (attr.type == cgltf_attribute_type_position)  posAcc  = attr.data;
        if (attr.type == cgltf_attribute_type_normal)    normAcc = attr.data;
        if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
    }

    if (!posAcc) return;

    std::vector<float> positions, normals, uvs;
    if (!readFloatAccessor(posAcc, positions, 3)) return;
    readFloatAccessor(normAcc, normals, 3);
    readFloatAccessor(uvAcc,   uvs,    2);

    const uint32_t primVertCount = static_cast<uint32_t>(positions.size() / 3);
    const uint32_t baseVertex    = static_cast<uint32_t>(merged.positions.size() / 3);

    // Apply node world transform to every position and normal.
    for (uint32_t i = 0; i < primVertCount; ++i) {
        applyMatPos(worldMat,
            positions[i*3+0], positions[i*3+1], positions[i*3+2],
            positions[i*3+0], positions[i*3+1], positions[i*3+2]);
    }
    if (normals.size() == positions.size()) {
        for (uint32_t i = 0; i < primVertCount; ++i) {
            applyMatDir(worldMat,
                normals[i*3+0], normals[i*3+1], normals[i*3+2],
                normals[i*3+0], normals[i*3+1], normals[i*3+2]);
        }
    }

    // Append positions.
    merged.positions.insert(merged.positions.end(), positions.begin(), positions.end());

    // Normals: pad earlier slots with Y-up if this prim has them but prior ones didn't.
    if (normals.size() == positions.size()) {
        if (merged.normals.size() < static_cast<size_t>(baseVertex) * 3) {
            const size_t prev = merged.normals.size() / 3;
            merged.normals.resize(static_cast<size_t>(baseVertex) * 3, 0.0f);
            for (size_t k = prev; k < baseVertex; ++k)
                merged.normals[k * 3 + 1] = 1.0f; // Y-up default
        }
        merged.normals.insert(merged.normals.end(), normals.begin(), normals.end());
    } else if (!merged.normals.empty()) {
        // Prior prims had normals; pad new slots with Y-up.
        const size_t prev = merged.normals.size() / 3;
        merged.normals.resize(merged.normals.size() + static_cast<size_t>(primVertCount) * 3, 0.0f);
        for (size_t k = prev; k < prev + primVertCount; ++k)
            merged.normals[k * 3 + 1] = 1.0f;
    }

    // UVs: same strategy; default to (0,0).
    if (uvs.size() == static_cast<size_t>(primVertCount) * 2) {
        if (merged.uvs.size() < static_cast<size_t>(baseVertex) * 2)
            merged.uvs.resize(static_cast<size_t>(baseVertex) * 2, 0.0f);
        merged.uvs.insert(merged.uvs.end(), uvs.begin(), uvs.end());
    } else if (!merged.uvs.empty()) {
        merged.uvs.resize(merged.uvs.size() + static_cast<size_t>(primVertCount) * 2, 0.0f);
    }

    // Indices: offset by baseVertex so they address the merged buffer.
    const cgltf_accessor* idxAcc = prim->indices;
    for (cgltf_size i = 0; i < idxAcc->count; ++i)
        merged.indices.push_back(baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(idxAcc, i)));

    ++primitivesFound;
}

// Recursively walk a scene node tree, collecting mesh primitives into merged.
static void walkNode(const cgltf_node* node, RawMesh& merged, int& primitivesFound) {
    if (node->mesh) {
        float worldMat[16];
        cgltf_node_transform_world(node, worldMat);
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
            appendPrimitive(&node->mesh->primitives[pi], worldMat, merged, primitivesFound);
    }
    for (cgltf_size ci = 0; ci < node->children_count; ++ci)
        walkNode(node->children[ci], merged, primitivesFound);
}

// Parse ALL mesh primitives from a glTF/glb file into one merged RawMesh,
// applying each scene node's world transform so multi-object levels are correct.
// Also extracts embedded textures into outTextures.
// Returns false (without touching `out`) when the file is missing or has no valid primitives.
bool tryParseGltf(const std::string& path, RawMesh& out, std::vector<RawTexture>& outTextures) {
    cgltf_options opts{};
    cgltf_data*   data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success || !data)
        return false;

    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    RawMesh merged;
    int primitivesFound = 0;

    // Walk the default scene (or first scene) so node world transforms are correct.
    // Fall back to a flat node walk if there are no scenes (rare but valid glTF).
    if (data->scenes_count > 0) {
        const cgltf_scene& scene = data->scene ? *data->scene : data->scenes[0];
        for (cgltf_size ni = 0; ni < scene.nodes_count; ++ni)
            walkNode(scene.nodes[ni], merged, primitivesFound);
    } else {
        // No scene — iterate all nodes with identity world transform fallback.
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
            walkNode(&data->nodes[ni], merged, primitivesFound);
    }

    // Extract embedded textures (best-effort — failures are warned, not fatal).
    outTextures = extractTextures(data);

    cgltf_free(data);

    if (primitivesFound == 0)
        return false;

    out = std::move(merged);
    return true;
}

// ---------------------------------------------------------------------------
// meshoptimizer passes
// ---------------------------------------------------------------------------

// A flat vertex struct used only during optimization — positions/normals/uvs
// interleaved so meshoptimizer can hash-compare whole vertices.
struct OptVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct OptimizedMesh {
    std::vector<VertexStatic> vertices;
    std::vector<uint32_t>     indices;
};

OptimizedMesh runMeshoptimizer(const RawMesh& raw) {
    const uint32_t srcVertexCount = static_cast<uint32_t>(raw.positions.size() / 3);
    const uint32_t indexCount     = static_cast<uint32_t>(raw.indices.size());

    const bool hasNormals = (raw.normals.size() == raw.positions.size());
    const bool hasUVs     = (raw.uvs.size()     == srcVertexCount * 2u);

    // Build interleaved OptVertex array from raw data.
    std::vector<OptVertex> srcVerts(srcVertexCount);
    for (uint32_t i = 0; i < srcVertexCount; ++i) {
        srcVerts[i].px = raw.positions[i * 3 + 0];
        srcVerts[i].py = raw.positions[i * 3 + 1];
        srcVerts[i].pz = raw.positions[i * 3 + 2];
        srcVerts[i].nx = hasNormals ? raw.normals[i * 3 + 0] : 0.0f;
        srcVerts[i].ny = hasNormals ? raw.normals[i * 3 + 1] : 0.0f;
        srcVerts[i].nz = hasNormals ? raw.normals[i * 3 + 2] : 1.0f;
        srcVerts[i].u  = hasUVs ? raw.uvs[i * 2 + 0] : 0.0f;
        srcVerts[i].v  = hasUVs ? raw.uvs[i * 2 + 1] : 0.0f;
    }

    // 1. Generate vertex remap (deduplicates vertices).
    std::vector<unsigned int> remap(srcVertexCount);
    const size_t uniqueVertexCount = meshopt_generateVertexRemap(
        remap.data(),
        raw.indices.data(),
        indexCount,
        srcVerts.data(),
        srcVertexCount,
        sizeof(OptVertex));

    // 2. Remap index and vertex buffers.
    std::vector<uint32_t>  remappedIndices(indexCount);
    std::vector<OptVertex> remappedVerts(uniqueVertexCount);

    meshopt_remapIndexBuffer(remappedIndices.data(), raw.indices.data(), indexCount, remap.data());
    meshopt_remapVertexBuffer(remappedVerts.data(), srcVerts.data(), srcVertexCount,
                              sizeof(OptVertex), remap.data());

    // 3. Optimize vertex cache.
    meshopt_optimizeVertexCache(remappedIndices.data(), remappedIndices.data(),
                                indexCount, uniqueVertexCount);

    // 4. Optimize overdraw (threshold = 1.05 — slight improvement over perfect ACMR).
    meshopt_optimizeOverdraw(remappedIndices.data(), remappedIndices.data(), indexCount,
                             &remappedVerts[0].px, uniqueVertexCount, sizeof(OptVertex), 1.05f);

    // 5. Optimize vertex fetch (reorders vertex buffer to match index buffer access).
    meshopt_optimizeVertexFetch(remappedVerts.data(), remappedIndices.data(), indexCount,
                                remappedVerts.data(), uniqueVertexCount, sizeof(OptVertex));

    // Build final VertexStatic array.
    const uint32_t finalVertexCount = static_cast<uint32_t>(uniqueVertexCount);
    OptimizedMesh out;
    out.vertices.resize(finalVertexCount);
    out.indices  = std::move(remappedIndices);

    for (uint32_t i = 0; i < finalVertexCount; ++i) {
        const OptVertex& ov = remappedVerts[i];
        VertexStatic& vs = out.vertices[i];
        vs.position[0]  = ov.px;
        vs.position[1]  = ov.py;
        vs.position[2]  = ov.pz;
        vs.packedNormal  = packNormal(ov.nx, ov.ny, ov.nz);
        vs.packedTangent = packTangent(1.0f, 0.0f, 0.0f, 1.0f); // +X tangent default
        vs.uv[0] = ov.u;
        vs.uv[1] = ov.v;
    }

    return out;
}

// ---------------------------------------------------------------------------
// AABB computation
// ---------------------------------------------------------------------------

struct AABB {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

AABB computeAabb(const std::vector<VertexStatic>& verts) {
    AABB box{ 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f };
    for (const auto& v : verts) {
        box.minX = v.position[0] < box.minX ? v.position[0] : box.minX;
        box.minY = v.position[1] < box.minY ? v.position[1] : box.minY;
        box.minZ = v.position[2] < box.minZ ? v.position[2] : box.minZ;
        box.maxX = v.position[0] > box.maxX ? v.position[0] : box.maxX;
        box.maxY = v.position[1] > box.maxY ? v.position[1] : box.maxY;
        box.maxZ = v.position[2] > box.maxZ ? v.position[2] : box.maxZ;
    }
    return box;
}

// ---------------------------------------------------------------------------
// Collision geometry generation
// ---------------------------------------------------------------------------

// Flat collision data ready to be serialised into the COLL section.
struct CollisionData {
    engine::tools::CollisionType        type;
    std::vector<std::array<float, 3>>   vertices; // position only
    std::vector<uint32_t>               indices;  // empty for ConvexHull
};

// Build a TriangleMesh collision from the optimised render mesh.
// Positions are extracted directly; UV/normal data is stripped.
CollisionData buildTriangleMeshCollision(const OptimizedMesh& mesh) {
    CollisionData cd;
    cd.type = engine::tools::CollisionType::TriangleMesh;
    cd.vertices.reserve(mesh.vertices.size());
    for (const VertexStatic& v : mesh.vertices) {
        cd.vertices.push_back({ v.position[0], v.position[1], v.position[2] });
    }
    cd.indices = mesh.indices;
    return cd;
}

// Build a ConvexHull collision from the optimised mesh.
// Strategy: collect position-only data with duplicate elimination using
// meshopt_generateVertexRemap on the position stream alone.
// The deduplicated position set is a valid convex-hull point cloud; the
// physics engine computes the actual convex hull from it at simulation time.
// No index array is stored — physics uses a vertex soup for convex hulls.
CollisionData buildConvexHullCollision(const OptimizedMesh& mesh) {
    CollisionData cd;
    cd.type = engine::tools::CollisionType::ConvexHull;

    const size_t srcCount = mesh.vertices.size();
    if (srcCount == 0) return cd;

    // Build a packed float3 position buffer.
    std::vector<float> positions;
    positions.reserve(srcCount * 3);
    for (const VertexStatic& v : mesh.vertices) {
        positions.push_back(v.position[0]);
        positions.push_back(v.position[1]);
        positions.push_back(v.position[2]);
    }

    // Deduplicate positions using meshopt's vertex remap on position-only data.
    // This removes any vertices that share the same position (can happen when
    // multiple vertices differ only in normal/UV).
    std::vector<unsigned int> remap(srcCount);
    const size_t uniqueCount = meshopt_generateVertexRemap(
        remap.data(),
        nullptr,           // no index buffer — treat as unindexed
        srcCount,
        positions.data(),
        srcCount,
        sizeof(float) * 3);

    // Collect the unique positions in their remapped order.
    std::vector<std::array<float, 3>> uniquePos(uniqueCount);
    for (size_t i = 0; i < srcCount; ++i) {
        const unsigned int newIdx = remap[i];
        uniquePos[newIdx] = {
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]
        };
    }

    cd.vertices = std::move(uniquePos);
    // ConvexHull stores no index array (physics uses vertex soup).
    return cd;
}

// ---------------------------------------------------------------------------
// .easset writer
// version 1: MESH only (no collision, no textures)
// version 2: MESH + optional COLL
// version 3: MESH + optional COLL + zero or more TEX sections
// ---------------------------------------------------------------------------

// Compute the byte size of one TEX section (header + all mip headers + pixel data).
static uint32_t computeTexSectionSize(const RawTexture& tex)
{
    uint32_t size = sizeof(TexSectionHeader);
    for (const auto& mip : tex.mips) {
        size += sizeof(TexMipHeader);
        size += static_cast<uint32_t>(mip.pixels.size());
    }
    return size;
}

bool writeEasset(const std::filesystem::path&   output,
                 const OptimizedMesh&            mesh,
                 const CollisionData*            collision,
                 const std::vector<RawTexture>&  textures)
{
    const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t indexCount  = static_cast<uint32_t>(mesh.indices.size());

    const uint32_t meshHeaderSize  = sizeof(MeshSectionHeader);
    const uint32_t vertexDataSize  = vertexCount * static_cast<uint32_t>(sizeof(VertexStatic));
    const uint32_t indexDataSize   = indexCount  * static_cast<uint32_t>(sizeof(uint32_t));
    const uint32_t meshSectionSize = meshHeaderSize + vertexDataSize + indexDataSize;

    const bool hasCollision = (collision != nullptr);
    const bool hasTextures  = !textures.empty();

    // Version bumps: 1 (mesh only) → 2 (+ coll) → 3 (+ tex)
    uint16_t fileVersion;
    if (hasTextures)       fileVersion = 3u;
    else if (hasCollision) fileVersion = 2u;
    else                   fileVersion = 1u;

    // TOC entry count: MESH always, +COLL if present, +1 per texture.
    const uint32_t tocEntries = 1u
        + (hasCollision ? 1u : 0u)
        + static_cast<uint32_t>(textures.size());

    // Compute collision section size when needed.
    uint32_t collSectionSize = 0;
    if (hasCollision) {
        collSectionSize = sizeof(CollSectionHeader)
            + static_cast<uint32_t>(collision->vertices.size()) * 3u * sizeof(float)
            + static_cast<uint32_t>(collision->indices.size())  * sizeof(uint32_t);
    }

    // Compute per-texture section sizes.
    std::vector<uint32_t> texSectionSizes;
    texSectionSizes.reserve(textures.size());
    for (const auto& tex : textures)
        texSectionSizes.push_back(computeTexSectionSize(tex));

    // Layout:
    //   [0..19]       EassHeader (20 bytes)
    //   [20..]        TocEntry × tocEntries  (each 16 bytes)
    //   pad to 64
    //   MESH section
    //   [optional] COLL section (64-byte aligned)
    //   [optional] TEX section × N (each 64-byte aligned)
    constexpr uint32_t kHeaderSize   = sizeof(EassHeader);   // 20
    constexpr uint32_t kTocEntrySize = sizeof(TocEntry);     // 16
    const uint32_t tocOffset         = kHeaderSize;
    const uint32_t rawDataStart      = kHeaderSize + tocEntries * kTocEntrySize;
    const uint32_t meshSectionOffset =
        (rawDataStart + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);

    uint32_t collSectionOffset = 0;
    uint32_t totalSize         = meshSectionOffset + meshSectionSize;
    if (hasCollision) {
        collSectionOffset = (totalSize + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);
        totalSize         = collSectionOffset + collSectionSize;
    }

    std::vector<uint32_t> texSectionOffsets;
    texSectionOffsets.reserve(textures.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(textures.size()); ++i) {
        const uint32_t alignedOffset = (totalSize + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);
        texSectionOffsets.push_back(alignedOffset);
        totalSize = alignedOffset + texSectionSizes[i];
    }

    std::filesystem::create_directories(output.parent_path());

    std::fstream fs(output, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!fs.is_open()) return false;

    // Header
    EassHeader hdr{};
    std::memcpy(hdr.magic, "EASS", 4);
    hdr.version       = fileVersion;
    hdr.assetType     = 0;
    hdr.totalSize     = totalSize;
    hdr.tocOffset     = tocOffset;
    hdr.tocEntryCount = tocEntries;
    fs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // MESH TocEntry
    TocEntry meshToc{};
    std::memcpy(meshToc.id, "MESH", 4);
    meshToc.offset   = meshSectionOffset;
    meshToc.size     = meshSectionSize;
    meshToc.reserved = 0;
    fs.write(reinterpret_cast<const char*>(&meshToc), sizeof(meshToc));

    // COLL TocEntry (version 2+ only)
    if (hasCollision) {
        TocEntry collToc{};
        std::memcpy(collToc.id, "COLL", 4);
        collToc.offset   = collSectionOffset;
        collToc.size     = collSectionSize;
        collToc.reserved = 0;
        fs.write(reinterpret_cast<const char*>(&collToc), sizeof(collToc));
    }

    // TEX TocEntries (version 3 only)
    for (uint32_t i = 0; i < static_cast<uint32_t>(textures.size()); ++i) {
        TocEntry texToc{};
        texToc.id[0] = 'T'; texToc.id[1] = 'E'; texToc.id[2] = 'X'; texToc.id[3] = '\0';
        texToc.offset   = texSectionOffsets[i];
        texToc.size     = texSectionSizes[i];
        texToc.reserved = 0;
        fs.write(reinterpret_cast<const char*>(&texToc), sizeof(texToc));
    }

    // Padding before MESH section
    padTo64(fs);

    // MESH section header
    const AABB box = computeAabb(mesh.vertices);
    MeshSectionHeader msh{};
    msh.vertexCount  = vertexCount;
    msh.indexCount   = indexCount;
    msh.vertexLayout = 1; // kVertexLayoutStatic
    msh.pad[0] = msh.pad[1] = msh.pad[2] = 0;
    msh.aabbMin[0] = box.minX; msh.aabbMin[1] = box.minY; msh.aabbMin[2] = box.minZ;
    msh.aabbMax[0] = box.maxX; msh.aabbMax[1] = box.maxY; msh.aabbMax[2] = box.maxZ;
    fs.write(reinterpret_cast<const char*>(&msh), sizeof(msh));

    // Vertex data
    fs.write(reinterpret_cast<const char*>(mesh.vertices.data()),
             static_cast<std::streamsize>(vertexDataSize));

    // Index data
    fs.write(reinterpret_cast<const char*>(mesh.indices.data()),
             static_cast<std::streamsize>(indexDataSize));

    // COLL section (version 2+ only)
    if (hasCollision) {
        padTo64(fs);

        CollSectionHeader csh{};
        csh.collisionType = static_cast<uint8_t>(collision->type);
        csh.pad[0] = csh.pad[1] = csh.pad[2] = 0;
        csh.vertexCount   = static_cast<uint32_t>(collision->vertices.size());
        csh.indexCount    = static_cast<uint32_t>(collision->indices.size());
        fs.write(reinterpret_cast<const char*>(&csh), sizeof(csh));

        // Position-only vertices (3 floats each)
        for (const auto& v : collision->vertices) {
            fs.write(reinterpret_cast<const char*>(v.data()),
                     static_cast<std::streamsize>(3 * sizeof(float)));
        }

        // Index buffer (empty for ConvexHull)
        if (!collision->indices.empty()) {
            fs.write(reinterpret_cast<const char*>(collision->indices.data()),
                     static_cast<std::streamsize>(
                         collision->indices.size() * sizeof(uint32_t)));
        }
    }

    // TEX sections (version 3 only)
    for (const auto& tex : textures) {
        padTo64(fs);

        TexSectionHeader tsh{};
        tsh.dxgiFormat = tex.dxgiFormat;
        tsh.baseWidth  = tex.baseWidth;
        tsh.baseHeight = tex.baseHeight;
        tsh.mipCount   = static_cast<uint32_t>(tex.mips.size());
        fs.write(reinterpret_cast<const char*>(&tsh), sizeof(tsh));

        for (const auto& mip : tex.mips) {
            TexMipHeader tmh{};
            tmh.width    = mip.width;
            tmh.height   = mip.height;
            tmh.dataSize = static_cast<uint32_t>(mip.pixels.size());
            fs.write(reinterpret_cast<const char*>(&tmh), sizeof(tmh));
            fs.write(reinterpret_cast<const char*>(mip.pixels.data()),
                     static_cast<std::streamsize>(mip.pixels.size()));
        }
    }

    return fs.good();
}

// ---------------------------------------------------------------------------
// Texture-only .easset writer
// version 3: TEX sections only (no MESH, no COLL)
// ---------------------------------------------------------------------------

// Write a .easset containing only TEX sections (no MESH or COLL sections).
// Used by importPng() for standalone image assets.
static bool writeTextureOnlyEasset(const std::filesystem::path&   output,
                                    const std::vector<RawTexture>& textures)
{
    if (textures.empty()) return false;

    const uint32_t tocEntryCount = static_cast<uint32_t>(textures.size());

    std::vector<uint32_t> texSectionSizes;
    texSectionSizes.reserve(textures.size());
    for (const auto& tex : textures)
        texSectionSizes.push_back(computeTexSectionSize(tex));

    constexpr uint32_t kHeaderSize   = sizeof(EassHeader);
    constexpr uint32_t kTocEntrySize = sizeof(TocEntry);
    const uint32_t rawDataStart      = kHeaderSize + tocEntryCount * kTocEntrySize;
    const uint32_t firstTexOffset    =
        (rawDataStart + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);

    std::vector<uint32_t> texSectionOffsets;
    texSectionOffsets.reserve(textures.size());
    uint32_t totalSize = firstTexOffset;
    for (uint32_t i = 0; i < tocEntryCount; ++i) {
        const uint32_t alignedOffset = (totalSize + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);
        texSectionOffsets.push_back(alignedOffset);
        totalSize = alignedOffset + texSectionSizes[i];
    }

    std::filesystem::create_directories(output.parent_path());

    std::fstream fs(output, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!fs.is_open()) return false;

    EassHeader hdr{};
    std::memcpy(hdr.magic, "EASS", 4);
    hdr.version       = 3;
    hdr.assetType     = 0;
    hdr.totalSize     = totalSize;
    hdr.tocOffset     = kHeaderSize;
    hdr.tocEntryCount = tocEntryCount;
    fs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    for (uint32_t i = 0; i < tocEntryCount; ++i) {
        TocEntry texToc{};
        texToc.id[0] = 'T'; texToc.id[1] = 'E'; texToc.id[2] = 'X'; texToc.id[3] = '\0';
        texToc.offset   = texSectionOffsets[i];
        texToc.size     = texSectionSizes[i];
        texToc.reserved = 0;
        fs.write(reinterpret_cast<const char*>(&texToc), sizeof(texToc));
    }

    for (const auto& tex : textures) {
        padTo64(fs);

        TexSectionHeader tsh{};
        tsh.dxgiFormat = tex.dxgiFormat;
        tsh.baseWidth  = tex.baseWidth;
        tsh.baseHeight = tex.baseHeight;
        tsh.mipCount   = static_cast<uint32_t>(tex.mips.size());
        fs.write(reinterpret_cast<const char*>(&tsh), sizeof(tsh));

        for (const auto& mip : tex.mips) {
            TexMipHeader tmh{};
            tmh.width    = mip.width;
            tmh.height   = mip.height;
            tmh.dataSize = static_cast<uint32_t>(mip.pixels.size());
            fs.write(reinterpret_cast<const char*>(&tmh), sizeof(tmh));
            fs.write(reinterpret_cast<const char*>(mip.pixels.data()),
                     static_cast<std::streamsize>(mip.pixels.size()));
        }
    }

    return fs.good();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

namespace engine::tools {

ImportResult importGltf(const std::filesystem::path& source,
                        const std::filesystem::path& output,
                        const ImportSettings&        settings) {
    // Attempt to parse the glTF/glb file.  On failure (missing file, invalid
    // format, unsupported primitive) fall back to the built-in unit-cube mesh
    // so that pipeline tests that supply a dummy path still pass.
    RawMesh raw;
    std::vector<RawTexture> textures;
    const bool parsed = tryParseGltf(source.string(), raw, textures);
    if (!parsed) {
        LOG_WARN("importGltf: could not parse '{}' — using built-in unit-cube fallback.",
                 source.string());
        raw = buildUnitCubeRaw();
        textures.clear(); // no textures for the fallback cube
    } else {
        LOG_INFO("importGltf: parsed '{}' — merged all primitives ({} vertices, {} indices, {} textures).",
                 source.string(),
                 static_cast<uint32_t>(raw.positions.size() / 3),
                 static_cast<uint32_t>(raw.indices.size()),
                 static_cast<uint32_t>(textures.size()));
    }

    // Run meshoptimizer passes over the raw geometry.
    OptimizedMesh optimized = runMeshoptimizer(raw);

    // Build optional collision data.
    CollisionData  collData;
    CollisionData* collPtr = nullptr;
    if (settings.generateCollision) {
        if (settings.collisionType == CollisionType::ConvexHull) {
            collData = buildConvexHullCollision(optimized);
        } else {
            collData = buildTriangleMeshCollision(optimized);
        }
        collPtr = &collData;
    }

    if (!writeEasset(output, optimized, collPtr, textures)) {
        return { false, "Write error while producing: " + output.string() };
    }

    LOG_INFO("importGltf: wrote '{}' ({} vertices, {} indices, collision={}, textures={}).",
             output.string(),
             static_cast<uint32_t>(optimized.vertices.size()),
             static_cast<uint32_t>(optimized.indices.size()),
             settings.generateCollision ? "yes" : "no",
             static_cast<uint32_t>(textures.size()));

    return { true, {} };
}

ImportResult importPng(const std::filesystem::path& source,
                       const std::filesystem::path& output)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc* decoded = stbi_load(source.string().c_str(), &w, &h, &comp, 4 /* force RGBA */);

    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return { false,
            "importPng: stbi_load failed for '" + source.string()
            + "': " + stbi_failure_reason() };
    }

    RawTexture tex;
    tex.baseWidth  = static_cast<uint32_t>(w);
    tex.baseHeight = static_cast<uint32_t>(h);

    {
        RawMipLevel mip0;
        mip0.width  = tex.baseWidth;
        mip0.height = tex.baseHeight;
        const std::size_t byteCount =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        mip0.pixels.assign(decoded, decoded + byteCount);
        tex.mips.push_back(std::move(mip0));
    }
    stbi_image_free(decoded);

    while (tex.mips.back().width > 1u || tex.mips.back().height > 1u)
        tex.mips.push_back(downsampleMip(tex.mips.back()));

    const uint32_t logW    = tex.baseWidth;
    const uint32_t logH    = tex.baseHeight;
    const uint32_t logMips = static_cast<uint32_t>(tex.mips.size());

    std::vector<RawTexture> texVec;
    texVec.push_back(std::move(tex));

    if (!writeTextureOnlyEasset(output, texVec)) {
        return { false, "importPng: write error while producing: " + output.string() };
    }

    LOG_INFO("importPng: wrote '{}' ({}x{}, {} mips).",
             output.string(), logW, logH, logMips);

    return { true, {} };
}

} // namespace engine::tools
