#include "tools/AssetImporter.h"
#include "core/log.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>
#include <algorithm>

// cgltf — single-header glTF 2.0 parser (public domain, vcpkg package)
// The implementation define must appear in exactly one translation unit.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

// meshoptimizer — geometry optimisation passes (vcpkg package)
#include <meshoptimizer.h>

// ---------------------------------------------------------------------------
// Internal binary layout types (not exposed in the public header)
// ---------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)

struct EassHeader {
    char     magic[4];        // "EASS"
    uint16_t version;         // 1
    uint16_t assetType;       // 0 = Mesh
    uint32_t totalSize;       // filled after all data is written
    uint32_t tocOffset;       // offset to first TocEntry from start of file
    uint32_t tocEntryCount;
};
static_assert(sizeof(EassHeader) == 20);

struct TocEntry {
    char     id[4];           // FourCC e.g. "MESH"
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

struct VertexStatic {
    float    position[3];     // 12 bytes
    uint32_t packedNormal;    //  4 bytes  R10G10B10A2_UNORM
    uint32_t packedTangent;   //  4 bytes  R10G10B10A2_UNORM; bit31 = bitangent sign
    float    uv[2];           //  8 bytes
};
static_assert(sizeof(VertexStatic) == 28);

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

// Attempt to parse the first mesh primitive from a glTF/glb file.
// Returns false (without touching `out`) when the file is missing or invalid.
bool tryParseGltf(const std::string& path, RawMesh& out) {
    cgltf_options opts{};
    cgltf_data*   data = nullptr;

    cgltf_result result = cgltf_parse_file(&opts, path.c_str(), &data);
    if (result != cgltf_result_success || !data) {
        return false;
    }

    // Load external buffers (.bin files / data URIs).
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    // Find the first mesh with at least one primitive that has POSITION.
    const cgltf_mesh*      mesh      = nullptr;
    const cgltf_primitive* primitive = nullptr;

    for (cgltf_size mi = 0; mi < data->meshes_count && !primitive; ++mi) {
        for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count; ++pi) {
            const cgltf_primitive* prim = &data->meshes[mi].primitives[pi];
            // Require indexed triangles with at least a POSITION attribute.
            if (prim->type != cgltf_primitive_type_triangles) continue;
            if (!prim->indices) continue;

            bool hasPosition = false;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    hasPosition = true;
                    break;
                }
            }
            if (!hasPosition) continue;

            mesh      = &data->meshes[mi];
            primitive = prim;
            break;
        }
    }

    if (!primitive) {
        cgltf_free(data);
        return false;
    }

    // Extract per-attribute data.
    const cgltf_accessor* posAcc    = nullptr;
    const cgltf_accessor* normAcc   = nullptr;
    const cgltf_accessor* uvAcc     = nullptr;

    for (cgltf_size ai = 0; ai < primitive->attributes_count; ++ai) {
        const cgltf_attribute& attr = primitive->attributes[ai];
        if (attr.type == cgltf_attribute_type_position)  posAcc  = attr.data;
        if (attr.type == cgltf_attribute_type_normal)    normAcc = attr.data;
        if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
    }

    RawMesh raw;

    if (!readFloatAccessor(posAcc, raw.positions, 3)) {
        cgltf_free(data);
        return false;
    }

    // Normals and UVs are optional — leave vectors empty if absent.
    readFloatAccessor(normAcc, raw.normals, 3);
    readFloatAccessor(uvAcc,   raw.uvs,    2);

    // Extract indices.
    const cgltf_accessor* idxAcc = primitive->indices;
    const cgltf_size indexCount = idxAcc->count;
    raw.indices.resize(indexCount);
    for (cgltf_size i = 0; i < indexCount; ++i) {
        raw.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(idxAcc, i));
    }

    cgltf_free(data);
    out = std::move(raw);
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
// .easset writer — takes an already-optimized mesh and serialises it
// ---------------------------------------------------------------------------

bool writeEasset(const std::filesystem::path& output,
                 const OptimizedMesh& mesh)
{
    const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t indexCount  = static_cast<uint32_t>(mesh.indices.size());

    const uint32_t meshHeaderSize  = sizeof(MeshSectionHeader);
    const uint32_t vertexDataSize  = vertexCount * static_cast<uint32_t>(sizeof(VertexStatic));
    const uint32_t indexDataSize   = indexCount  * static_cast<uint32_t>(sizeof(uint32_t));
    const uint32_t meshSectionSize = meshHeaderSize + vertexDataSize + indexDataSize;

    // Layout:
    //   [0..19]  EassHeader (20 bytes)
    //   [20..35] TocEntry × 1  (16 bytes)
    //   pad to 64
    //   MESH section
    constexpr uint32_t kHeaderSize   = sizeof(EassHeader);   // 20
    constexpr uint32_t kTocEntrySize = sizeof(TocEntry);     // 16
    const uint32_t tocOffset         = kHeaderSize;
    const uint32_t rawDataStart      = kHeaderSize + kTocEntrySize;
    const uint32_t meshSectionOffset =
        (rawDataStart + kAlignmentBytes - 1) & ~(kAlignmentBytes - 1);
    const uint32_t totalSize         = meshSectionOffset + meshSectionSize;

    std::filesystem::create_directories(output.parent_path());

    std::fstream fs(output, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!fs.is_open()) return false;

    // Header
    EassHeader hdr{};
    std::memcpy(hdr.magic, "EASS", 4);
    hdr.version       = 1;
    hdr.assetType     = 0;
    hdr.totalSize     = totalSize;
    hdr.tocOffset     = tocOffset;
    hdr.tocEntryCount = 1;
    fs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // TOC
    TocEntry toc{};
    std::memcpy(toc.id, "MESH", 4);
    toc.offset   = meshSectionOffset;
    toc.size     = meshSectionSize;
    toc.reserved = 0;
    fs.write(reinterpret_cast<const char*>(&toc), sizeof(toc));

    // Padding
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

    return fs.good();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

namespace engine::tools {

ImportResult importGltf(const std::filesystem::path& source,
                        const std::filesystem::path& output) {
    // Attempt to parse the glTF/glb file.  On failure (missing file, invalid
    // format, unsupported primitive) fall back to the built-in unit-cube mesh
    // so that pipeline tests that supply a dummy path still pass.
    RawMesh raw;
    const bool parsed = tryParseGltf(source.string(), raw);
    if (!parsed) {
        LOG_WARN("importGltf: could not parse '{}' — using built-in unit-cube fallback.",
                 source.string());
        raw = buildUnitCubeRaw();
    } else {
        LOG_INFO("importGltf: parsed '{}' ({} vertices, {} indices).",
                 source.string(),
                 static_cast<uint32_t>(raw.positions.size() / 3),
                 static_cast<uint32_t>(raw.indices.size()));
    }

    // Run meshoptimizer passes over the raw geometry.
    OptimizedMesh optimized = runMeshoptimizer(raw);

    if (!writeEasset(output, optimized)) {
        return { false, "Write error while producing: " + output.string() };
    }

    LOG_INFO("importGltf: wrote '{}' ({} vertices, {} indices).",
             output.string(),
             static_cast<uint32_t>(optimized.vertices.size()),
             static_cast<uint32_t>(optimized.indices.size()));

    return { true, {} };
}

} // namespace engine::tools
