#include "tools/EassetLoader.h"

#include <algorithm>
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

// TEX section header (version 3+).
// Immediately follows this header for each mip:
//   uint32_t width, height, dataSize
//   uint8_t[dataSize] pixels  (RGBA8, row-major, top-to-bottom)
struct TexSectionHeader {
    uint32_t dxgiFormat;  // DXGI_FORMAT value; 28 = DXGI_FORMAT_R8G8B8A8_UNORM
    uint32_t baseWidth;
    uint32_t baseHeight;
    uint32_t mipCount;
};
static_assert(sizeof(TexSectionHeader) == 16, "TexSectionHeader must be exactly 16 bytes");

struct TexMipHeader {
    uint32_t width;
    uint32_t height;
    uint32_t dataSize;
};
static_assert(sizeof(TexMipHeader) == 12, "TexMipHeader must be exactly 12 bytes");

// CMAP section header (version 4 IBL asset).
// Followed by faceCount × mipCount × (CmapMipHeader + pixel data).
// Pixel format: R16G16B16A16_FLOAT (8 bytes per texel).
// Ordering: face0/mip0, face0/mip1, ..., face5/mip6.
struct CmapSectionHeader {
    uint32_t faceCount;   // always 6
    uint32_t mipCount;    // typically 7
    uint32_t baseSize;    // width == height of face at mip 0
};
static_assert(sizeof(CmapSectionHeader) == 12, "CmapSectionHeader must be exactly 12 bytes");

struct CmapMipHeader {
    uint32_t face;        // 0-5
    uint32_t mipLevel;    // 0 = full resolution
    uint32_t dataSize;    // bytes of R16G16B16A16_FLOAT pixel data
};
static_assert(sizeof(CmapMipHeader) == 12, "CmapMipHeader must be exactly 12 bytes");

// BRDF section header (version 4 IBL asset).
// Followed by width × height × 4 bytes of R16G16_UNORM pixels.
struct BrdfSectionHeader {
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(BrdfSectionHeader) == 8, "BrdfSectionHeader must be exactly 8 bytes");

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
    // Accept version 1 (no collision), version 2 (optional COLL section),
    // and version 3 (optional COLL + optional TEX sections).
    if (hdr.version != 1 && hdr.version != 2 && hdr.version != 3) {
        return std::nullopt;
    }
    if (hdr.assetType != 0) {
        return std::nullopt;
    }
    if (static_cast<std::size_t>(hdr.totalSize) > fileBytes.size()) {
        return std::nullopt;
    }

    // Step 4: Iterate TocEntries to find "MESH", optionally "COLL", and zero or more "TEX\0".
    TocEntry meshTocEntry{};
    bool     foundMesh = false;
    TocEntry collTocEntry{};
    bool     foundColl = false;
    std::vector<TocEntry> texTocEntries;

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
        } else if (entry.id[0] == 'T' && entry.id[1] == 'E' && entry.id[2] == 'X' && entry.id[3] == '\0') {
            texTocEntries.push_back(entry);
        }
    }

    if (!foundMesh) {
        // Texture-only .easset (no MESH section). Return CpuMesh with empty
        // geometry but populated textures — used for standalone image assets.
        if (texTocEntries.empty()) return std::nullopt;
        // Fall through; mesh steps below are wrapped in if (foundMesh).
    }

    CpuMesh result;

    // Steps 5-9: Parse MESH section (skipped for texture-only files).
    if (foundMesh) {
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
    }

    // Step 10: Parse optional COLL section (version 2 and 3).
    if (foundColl && (hdr.version == 2 || hdr.version == 3)) {
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

    // Step 11: Parse optional TEX sections (version 3 only).
    if (hdr.version == 3 && !texTocEntries.empty()) {
        result.textures.reserve(texTocEntries.size());

        for (const TocEntry& texToc : texTocEntries) {
            const std::size_t texOffset = static_cast<std::size_t>(texToc.offset);
            const std::size_t texSize   = static_cast<std::size_t>(texToc.size);

            // Bounds check: TEX section header must fit.
            if (texOffset + sizeof(TexSectionHeader) > fileBytes.size()) continue;
            if (texOffset + texSize > fileBytes.size()) continue;

            TexSectionHeader tsh{};
            std::memcpy(&tsh, fileBytes.data() + texOffset, sizeof(TexSectionHeader));

            CpuTexture tex;
            tex.dxgiFormat = tsh.dxgiFormat;
            tex.baseWidth  = tsh.baseWidth;
            tex.baseHeight = tsh.baseHeight;
            tex.mips.reserve(tsh.mipCount);

            std::size_t cursor = texOffset + sizeof(TexSectionHeader);
            bool        valid  = true;

            for (uint32_t m = 0; m < tsh.mipCount; ++m) {
                if (cursor + sizeof(TexMipHeader) > fileBytes.size()) { valid = false; break; }

                TexMipHeader tmh{};
                std::memcpy(&tmh, fileBytes.data() + cursor, sizeof(TexMipHeader));
                cursor += sizeof(TexMipHeader);

                if (cursor + static_cast<std::size_t>(tmh.dataSize) > fileBytes.size()) {
                    valid = false;
                    break;
                }

                CpuMipLevel mip;
                mip.width  = tmh.width;
                mip.height = tmh.height;
                mip.pixels.resize(tmh.dataSize);
                std::memcpy(mip.pixels.data(), fileBytes.data() + cursor, tmh.dataSize);
                cursor += tmh.dataSize;

                tex.mips.push_back(std::move(mip));
            }

            if (valid && tex.isValid()) {
                result.textures.push_back(std::move(tex));
            }
        }
    }

    // Step 12: Return the populated CpuMesh.
    // For texture-only files, require at least one valid texture.
    if (!foundMesh && result.textures.empty()) return std::nullopt;
    return result;
}

// ---------------------------------------------------------------------------
// loadIblEasset — parses a v4 .easset with CMAP and/or BRDF sections
// ---------------------------------------------------------------------------

std::optional<IblData> loadIblEasset(const std::filesystem::path& path)
{
    // Open and read the entire file.
    std::ifstream fs(path, std::ios::binary);
    if (!fs.is_open()) return std::nullopt;

    fs.seekg(0, std::ios::end);
    const auto fileSize = static_cast<std::size_t>(fs.tellg());
    fs.seekg(0, std::ios::beg);

    std::vector<uint8_t> fileBytes(fileSize);
    if (!fs.read(reinterpret_cast<char*>(fileBytes.data()),
                 static_cast<std::streamsize>(fileSize))) {
        return std::nullopt;
    }

    // Validate EassHeader.
    if (fileBytes.size() < sizeof(EassHeader)) return std::nullopt;

    EassHeader hdr{};
    std::memcpy(&hdr, fileBytes.data(), sizeof(EassHeader));

    if (std::memcmp(hdr.magic, "EASS", 4) != 0) return std::nullopt;
    // Only accept version 4 IBL assets (assetType == 1).
    if (hdr.version != 4) return std::nullopt;
    if (hdr.assetType != 1) return std::nullopt;
    if (static_cast<std::size_t>(hdr.totalSize) > fileBytes.size()) return std::nullopt;

    // Scan TOC for CMAP and BRDF entries.
    TocEntry cmapToc{};
    bool     foundCmap = false;
    TocEntry brdfToc{};
    bool     foundBrdf = false;

    for (uint32_t i = 0; i < hdr.tocEntryCount; ++i) {
        const std::size_t entryOffset =
            static_cast<std::size_t>(hdr.tocOffset) + i * sizeof(TocEntry);
        if (entryOffset + sizeof(TocEntry) > fileBytes.size()) return std::nullopt;

        TocEntry entry{};
        std::memcpy(&entry, fileBytes.data() + entryOffset, sizeof(TocEntry));

        if (std::memcmp(entry.id, "CMAP", 4) == 0) {
            cmapToc   = entry;
            foundCmap = true;
        } else if (std::memcmp(entry.id, "BRDF", 4) == 0) {
            brdfToc   = entry;
            foundBrdf = true;
        }
    }

    IblData result;

    // Parse CMAP section.
    if (foundCmap) {
        const std::size_t cmapOffset = static_cast<std::size_t>(cmapToc.offset);
        const std::size_t cmapSize   = static_cast<std::size_t>(cmapToc.size);

        if (cmapOffset + sizeof(CmapSectionHeader) <= fileBytes.size()
            && cmapOffset + cmapSize               <= fileBytes.size()) {

            CmapSectionHeader csh{};
            std::memcpy(&csh, fileBytes.data() + cmapOffset, sizeof(CmapSectionHeader));

            CpuCubemap cubemap;
            cubemap.baseSize  = csh.baseSize;
            cubemap.mipCount  = csh.mipCount;
            cubemap.faceCount = csh.faceCount;
            cubemap.mips.reserve(static_cast<std::size_t>(csh.faceCount) * csh.mipCount);

            std::size_t cursor = cmapOffset + sizeof(CmapSectionHeader);
            bool        valid  = true;

            const std::size_t totalMips =
                static_cast<std::size_t>(csh.faceCount) * csh.mipCount;

            for (std::size_t m = 0; m < totalMips; ++m) {
                if (cursor + sizeof(CmapMipHeader) > fileBytes.size()) { valid = false; break; }

                CmapMipHeader cmh{};
                std::memcpy(&cmh, fileBytes.data() + cursor, sizeof(CmapMipHeader));
                cursor += sizeof(CmapMipHeader);

                if (cursor + static_cast<std::size_t>(cmh.dataSize) > fileBytes.size()) {
                    valid = false;
                    break;
                }

                // Compute expected width/height for this mip level.
                const uint32_t mipW = std::max(1u, csh.baseSize >> cmh.mipLevel);
                const uint32_t mipH = mipW; // cubemap faces are square

                CpuCubemapMip mip;
                mip.face     = cmh.face;
                mip.mipLevel = cmh.mipLevel;
                mip.width    = mipW;
                mip.height   = mipH;
                mip.pixels.resize(cmh.dataSize);
                std::memcpy(mip.pixels.data(), fileBytes.data() + cursor, cmh.dataSize);
                cursor += cmh.dataSize;

                cubemap.mips.push_back(std::move(mip));
            }

            if (valid && cubemap.isValid()) {
                result.prefilteredEnvMap = std::move(cubemap);
            }
        }
    }

    // Parse BRDF section.
    if (foundBrdf) {
        const std::size_t brdfOffset = static_cast<std::size_t>(brdfToc.offset);
        const std::size_t brdfSize   = static_cast<std::size_t>(brdfToc.size);

        if (brdfOffset + sizeof(BrdfSectionHeader) <= fileBytes.size()
            && brdfOffset + brdfSize               <= fileBytes.size()) {

            BrdfSectionHeader bsh{};
            std::memcpy(&bsh, fileBytes.data() + brdfOffset, sizeof(BrdfSectionHeader));

            // R16G16_UNORM: 4 bytes per texel.
            const std::size_t expectedPixelBytes =
                static_cast<std::size_t>(bsh.width) * bsh.height * 4u;

            const std::size_t dataOffset = brdfOffset + sizeof(BrdfSectionHeader);

            if (dataOffset + expectedPixelBytes <= fileBytes.size()) {
                CpuBrdfLut lut;
                lut.width  = bsh.width;
                lut.height = bsh.height;
                lut.pixels.resize(expectedPixelBytes);
                std::memcpy(lut.pixels.data(), fileBytes.data() + dataOffset,
                            expectedPixelBytes);
                result.brdfLut = std::move(lut);
            }
        }
    }

    // Return nullopt only if neither section parsed successfully.
    if (!result.hasEnvMap() && !result.hasBrdfLut()) return std::nullopt;
    return result;
}

} // namespace engine::tools
