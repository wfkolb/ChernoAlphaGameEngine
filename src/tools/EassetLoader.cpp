#include "tools/EassetLoader.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <array>

// ---------------------------------------------------------------------------
// Internal binary layout types — must match AssetImporter.cpp exactly.
// ---------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)

struct EassHeader {
    char     magic[4];        // "EASS"
    uint16_t version;         // 1 or 2
    uint16_t assetType;       // 0 = Mesh
    uint32_t totalSize;
    uint32_t tocOffset;       // offset of first TocEntry from file start
    uint32_t tocEntryCount;
};
static_assert(sizeof(EassHeader) == 20, "EassHeader must be exactly 20 bytes");

struct TocEntry {
    char     id[4];           // "MESH" or "COLL"
    uint32_t offset;          // offset of section from file start
    uint32_t size;            // section size in bytes
    uint32_t reserved;        // 0
};
static_assert(sizeof(TocEntry) == 16, "TocEntry must be exactly 16 bytes");

struct MeshSectionHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t  vertexLayout;    // 1 = kVertexLayoutStatic
    uint8_t  pad[3];
    float    aabbMin[3];
    float    aabbMax[3];
};
static_assert(sizeof(MeshSectionHeader) == 36, "MeshSectionHeader must be exactly 36 bytes");

struct CollSectionHeader {
    uint8_t  collisionType;   // 0 = TriangleMesh, 1 = ConvexHull
    uint8_t  pad[3];
    uint32_t vertexCount;
    uint32_t indexCount;
};
static_assert(sizeof(CollSectionHeader) == 12, "CollSectionHeader must be exactly 12 bytes");

#pragma pack(pop)

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

namespace engine::tools {

std::optional<CpuMesh> loadEasset(const std::filesystem::path& path)
{
    // Step 1: Open file in binary mode; read all bytes.
    std::ifstream fs(path, std::ios::binary);
    if (!fs.is_open()) {
        return std::nullopt;
    }

    fs.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::size_t>(fs.tellg());
    fs.seekg(0, std::ios::beg);

    std::vector<uint8_t> fileBytes(fileSize);
    if (!fs.read(reinterpret_cast<char*>(fileBytes.data()),
                 static_cast<std::streamsize>(fileSize))) {
        return std::nullopt;
    }

    // Step 2: Validate minimum size for EassHeader.
    if (fileBytes.size() < sizeof(EassHeader)) {
        return std::nullopt;
    }

    // Step 3: Read and validate EassHeader.
    EassHeader hdr{};
    std::memcpy(&hdr, fileBytes.data(), sizeof(EassHeader));

    if (std::memcmp(hdr.magic, "EASS", 4) != 0) {
        return std::nullopt;
    }
    // Accept version 1 (no collision) and version 2 (optional COLL section).
    if (hdr.version != 1 && hdr.version != 2) {
        return std::nullopt;
    }
    if (hdr.assetType != 0) {
        return std::nullopt;
    }
    if (static_cast<std::size_t>(hdr.totalSize) > fileBytes.size()) {
        return std::nullopt;
    }

    // Step 4: Iterate TocEntries to find "MESH" and optionally "COLL".
    TocEntry meshTocEntry{};
    bool     foundMesh = false;
    TocEntry collTocEntry{};
    bool     foundColl = false;

    for (uint32_t i = 0; i < hdr.tocEntryCount; ++i) {
        const std::size_t entryOffset =
            static_cast<std::size_t>(hdr.tocOffset) + i * sizeof(TocEntry);

        // Bounds check before memcpy.
        if (entryOffset + sizeof(TocEntry) > fileBytes.size()) {
            return std::nullopt;
        }

        TocEntry entry{};
        std::memcpy(&entry, fileBytes.data() + entryOffset, sizeof(TocEntry));

        if (std::memcmp(entry.id, "MESH", 4) == 0) {
            meshTocEntry = entry;
            foundMesh    = true;
        } else if (std::memcmp(entry.id, "COLL", 4) == 0) {
            collTocEntry = entry;
            foundColl    = true;
        }
    }

    if (!foundMesh) {
        return std::nullopt;
    }

    // Step 5: Validate MESH TocEntry section bounds.
    const std::size_t sectionOffset = static_cast<std::size_t>(meshTocEntry.offset);
    const std::size_t sectionSize   = static_cast<std::size_t>(meshTocEntry.size);

    if (sectionOffset + sectionSize > fileBytes.size()) {
        return std::nullopt;
    }

    // Step 6: Read MeshSectionHeader.
    if (sectionOffset + sizeof(MeshSectionHeader) > fileBytes.size()) {
        return std::nullopt;
    }

    MeshSectionHeader msh{};
    std::memcpy(&msh, fileBytes.data() + sectionOffset, sizeof(MeshSectionHeader));

    // Step 7: Validate vertex layout.
    if (msh.vertexLayout != 1) { // 1 = kVertexLayoutStatic
        return std::nullopt;
    }

    // Step 8: Validate expected section size.
    const std::size_t expectedSize =
        sizeof(MeshSectionHeader)
        + static_cast<std::size_t>(msh.vertexCount) * sizeof(rendering::VertexStatic)
        + static_cast<std::size_t>(msh.indexCount)  * sizeof(uint32_t);

    if (sectionSize < expectedSize) {
        return std::nullopt;
    }

    // Step 9: All mesh validation passed — copy vertex and index data.
    CpuMesh result;
    result.vertices.resize(msh.vertexCount);
    result.indices.resize(msh.indexCount);

    const uint8_t* ptrAfterHeader   = fileBytes.data() + sectionOffset + sizeof(MeshSectionHeader);
    const uint8_t* ptrAfterVertices = ptrAfterHeader
        + static_cast<std::size_t>(msh.vertexCount) * sizeof(rendering::VertexStatic);

    std::memcpy(result.vertices.data(),
                ptrAfterHeader,
                msh.vertexCount * sizeof(rendering::VertexStatic));

    std::memcpy(result.indices.data(),
                ptrAfterVertices,
                msh.indexCount * sizeof(uint32_t));

    // Step 10: Parse optional COLL section (version 2 only).
    if (foundColl && hdr.version == 2) {
        const std::size_t collOffset = static_cast<std::size_t>(collTocEntry.offset);
        const std::size_t collSize   = static_cast<std::size_t>(collTocEntry.size);

        // Bounds check: header must fit.
        if (collOffset + sizeof(CollSectionHeader) <= fileBytes.size()
            && collOffset + collSize              <= fileBytes.size()) {

            CollSectionHeader csh{};
            std::memcpy(&csh, fileBytes.data() + collOffset, sizeof(CollSectionHeader));

            // Validate declared data fits within collSize.
            const std::size_t expectedCollSize =
                sizeof(CollSectionHeader)
                + static_cast<std::size_t>(csh.vertexCount) * 3u * sizeof(float)
                + static_cast<std::size_t>(csh.indexCount)  * sizeof(uint32_t);

            if (expectedCollSize <= collSize) {
                CpuCollision coll;
                coll.type = static_cast<CollisionType>(csh.collisionType);

                // Read position-only vertices.
                const uint8_t* pv = fileBytes.data() + collOffset + sizeof(CollSectionHeader);
                coll.vertices.resize(csh.vertexCount);
                for (uint32_t vi = 0; vi < csh.vertexCount; ++vi) {
                    std::array<float, 3> pos{};
                    std::memcpy(pos.data(), pv + vi * 3u * sizeof(float), 3u * sizeof(float));
                    coll.vertices[vi] = pos;
                }

                // Read index buffer (empty for ConvexHull).
                if (csh.indexCount > 0) {
                    const uint8_t* pi = pv + static_cast<std::size_t>(csh.vertexCount)
                                           * 3u * sizeof(float);
                    coll.indices.resize(csh.indexCount);
                    std::memcpy(coll.indices.data(), pi,
                                csh.indexCount * sizeof(uint32_t));
                }

                result.collision = std::move(coll);
            }
        }
    }

    // Step 11: Return the populated CpuMesh.
    return result;
}

} // namespace engine::tools
