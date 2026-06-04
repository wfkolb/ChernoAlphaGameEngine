#include "tools/SaveSystem.h"
#include "tools/ByteWriter.h"

#include <lz4.h>

#include <array>
#include <cstring>
#include <fstream>
#include <system_error>

namespace engine::tools {

namespace {

// ── CRC32 (IEEE 802.3, reflected) ─────────────────────────────────────────────

uint32_t crc32(const uint8_t* data, size_t len) {
    static std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ── LZ4 helpers ───────────────────────────────────────────────────────────────

std::vector<uint8_t> lz4Compress(const std::vector<uint8_t>& src) {
    const int srcSize = static_cast<int>(src.size());
    const int bound   = LZ4_compressBound(srcSize);
    std::vector<uint8_t> out(static_cast<size_t>(bound));
    const int n = LZ4_compress_default(
        reinterpret_cast<const char*>(src.data()),
        reinterpret_cast<char*>(out.data()),
        srcSize, bound);
    if (n <= 0 && srcSize != 0) return {};
    out.resize(static_cast<size_t>(n));
    return out;
}

// Returns false if decompression fails or yields a size other than rawSize.
bool lz4Decompress(const uint8_t* src, size_t srcSize,
                   uint32_t rawSize, std::vector<uint8_t>& out) {
    out.resize(rawSize);
    if (rawSize == 0) return srcSize == 0 || true;
    const int n = LZ4_decompress_safe(
        reinterpret_cast<const char*>(src),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(srcSize),
        static_cast<int>(rawSize));
    return n == static_cast<int>(rawSize);
}

// ── Container: [magic u32][rawSize u32][crc32(payload) u32][lz4 payload] ───────
// crc32 is computed over the *uncompressed* payload so corruption of the
// compressed bytes is caught after decompression.

std::vector<uint8_t> packContainer(uint32_t magic, const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> compressed = lz4Compress(payload);
    ByteWriter bw;
    bw.writeU32(magic);
    bw.writeU32(static_cast<uint32_t>(payload.size()));
    bw.writeU32(crc32(payload.data(), payload.size()));
    bw.writeBytes(compressed.data(), compressed.size());
    return bw.data();
}

bool unpackContainer(uint32_t expectedMagic, const std::vector<uint8_t>& file,
                     std::vector<uint8_t>& payload) {
    if (file.size() < 12) return false;
    ByteReader br(file.data(), file.size());
    const uint32_t magic   = br.readU32();
    const uint32_t rawSize = br.readU32();
    const uint32_t crc     = br.readU32();
    if (magic != expectedMagic) return false;

    const uint8_t* comp     = file.data() + 12;
    const size_t   compSize = file.size() - 12;
    if (!lz4Decompress(comp, compSize, rawSize, payload)) return false;
    if (crc32(payload.data(), payload.size()) != crc) return false;
    return true;
}

// ── File I/O ──────────────────────────────────────────────────────────────────

bool readWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const auto size = static_cast<std::streamoff>(in.tellg());
    if (size < 0) return false;
    out.resize(static_cast<size_t>(size));
    in.seekg(0);
    if (size > 0)
        in.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(in);
}

// Atomic write: stage to <path>.tmp, flush, then rename over the target.
bool atomicWrite(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        // rename can fail across some filesystems if the target exists; retry.
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(tmp, rmEc);
        return false;
    }
    return true;
}

// ── PlayerProfile (de)serialization ───────────────────────────────────────────

constexpr uint32_t kProfileMagic    = 0x50464E45u; // 'ENFP' little-endian view
constexpr uint32_t kMatchMagic      = 0x52464E45u; // 'ENFR'
constexpr uint32_t kCheckpointMagic = 0x50434E45u; // 'ENCP'

void writeBlob(ByteWriter& bw, const std::vector<uint8_t>& blob) {
    bw.writeU32(static_cast<uint32_t>(blob.size()));
    bw.writeBytes(blob.data(), blob.size());
}

std::vector<uint8_t> readBlob(ByteReader& br) {
    const uint32_t n = br.readU32();
    std::vector<uint8_t> blob(n);
    if (n > 0) br.readBytes(blob.data(), n);
    return blob;
}

std::vector<uint8_t> serializeProfile(const PlayerProfile& p) {
    ByteWriter bw;
    bw.writeU64(p.accountId);
    bw.writeString(p.displayName);
    bw.writeU64(p.firstSeenUnixTime);
    bw.writeU64(p.lastSeenUnixTime);
    for (const auto& slot : p.loadout) {
        bw.writeU32(slot.weaponDefId);
        bw.writeU32(slot.skinId);
    }
    bw.writeU64(p.stats.kills);
    bw.writeU64(p.stats.deaths);
    bw.writeU64(p.stats.assists);
    bw.writeU64(p.stats.matchesPlayed);
    bw.writeU64(p.stats.matchesWon);
    bw.writeU64(p.stats.roundsPlayed);
    bw.writeU64(p.stats.roundsWon);
    bw.writeF32(p.stats.totalShotsFired);
    bw.writeF32(p.stats.totalShotsHit);
    bw.writeF32(p.stats.totalPlaytimeSec);
    bw.writeF32(p.stats.totalDamageDealt);
    bw.writeF32(p.mouseSensitivity);
    bw.writeF32(p.adsSensitivityMult);
    bw.writeF32(p.masterVolume);
    bw.writeF32(p.effectsVolume);
    bw.writeF32(p.fovDegrees);
    bw.writeU8(p.invertPitchAxis ? 1u : 0u);
    writeBlob(bw, p.bindingOverrides);
    writeBlob(bw, p.gameExtensionData);
    return bw.data();
}

PlayerProfile deserializeProfile(const std::vector<uint8_t>& payload) {
    PlayerProfile p{};
    ByteReader br(payload.data(), payload.size());
    p.accountId         = br.readU64();
    p.displayName       = br.readString();
    p.firstSeenUnixTime = br.readU64();
    p.lastSeenUnixTime  = br.readU64();
    for (auto& slot : p.loadout) {
        slot.weaponDefId = br.readU32();
        slot.skinId      = br.readU32();
    }
    p.stats.kills            = br.readU64();
    p.stats.deaths           = br.readU64();
    p.stats.assists          = br.readU64();
    p.stats.matchesPlayed    = br.readU64();
    p.stats.matchesWon       = br.readU64();
    p.stats.roundsPlayed     = br.readU64();
    p.stats.roundsWon        = br.readU64();
    p.stats.totalShotsFired  = br.readF32();
    p.stats.totalShotsHit    = br.readF32();
    p.stats.totalPlaytimeSec = br.readF32();
    p.stats.totalDamageDealt = br.readF32();
    p.mouseSensitivity   = br.readF32();
    p.adsSensitivityMult = br.readF32();
    p.masterVolume       = br.readF32();
    p.effectsVolume      = br.readF32();
    p.fovDegrees         = br.readF32();
    p.invertPitchAxis    = br.readU8() != 0;
    p.bindingOverrides   = readBlob(br);
    p.gameExtensionData  = readBlob(br);
    return p;
}

// ── MatchRecord (de)serialization ─────────────────────────────────────────────

std::vector<uint8_t> serializeMatchRecord(const MatchRecord& r) {
    ByteWriter bw;
    bw.writeU64(r.matchId);
    bw.writeU64(r.startUnixTime);
    bw.writeF32(r.durationSec);
    bw.writeString(r.mapName);
    bw.writeString(r.gameModeName);
    bw.writeU32(r.serverVersion);
    bw.writeU8(r.winningTeam);
    bw.writeU32(static_cast<uint32_t>(r.roundsPlayed));
    bw.writeU32(static_cast<uint32_t>(r.roundsWon[0]));
    bw.writeU32(static_cast<uint32_t>(r.roundsWon[1]));
    bw.writeU32(static_cast<uint32_t>(r.players.size()));
    for (const auto& pr : r.players) {
        bw.writeU64(pr.accountId);
        bw.writeString(pr.displayName);
        bw.writeU8(pr.team);
        bw.writeU32(pr.kills);
        bw.writeU32(pr.deaths);
        bw.writeU32(pr.assists);
        bw.writeF32(pr.accuracy);
        bw.writeF32(pr.damageDealt);
        bw.writeF32(pr.damageTaken);
        bw.writeF32(pr.timeAliveSec);
        bw.writeU8(pr.wasPresent ? 1u : 0u);
    }
    writeBlob(bw, r.gameModeRecord);
    return bw.data();
}

MatchRecord deserializeMatchRecord(const std::vector<uint8_t>& payload) {
    MatchRecord r{};
    ByteReader br(payload.data(), payload.size());
    r.matchId       = br.readU64();
    r.startUnixTime = br.readU64();
    r.durationSec   = br.readF32();
    r.mapName       = br.readString();
    r.gameModeName  = br.readString();
    r.serverVersion = br.readU32();
    r.winningTeam   = br.readU8();
    r.roundsPlayed  = static_cast<int32_t>(br.readU32());
    r.roundsWon[0]  = static_cast<int32_t>(br.readU32());
    r.roundsWon[1]  = static_cast<int32_t>(br.readU32());
    const uint32_t playerCount = br.readU32();
    for (uint32_t i = 0; i < playerCount && br.ok(); ++i) {
        MatchRecord::PlayerRecord pr{};
        pr.accountId    = br.readU64();
        pr.displayName  = br.readString();
        pr.team         = br.readU8();
        pr.kills        = br.readU32();
        pr.deaths       = br.readU32();
        pr.assists      = br.readU32();
        pr.accuracy     = br.readF32();
        pr.damageDealt  = br.readF32();
        pr.damageTaken  = br.readF32();
        pr.timeAliveSec = br.readF32();
        pr.wasPresent   = br.readU8() != 0;
        r.players.push_back(std::move(pr));
    }
    r.gameModeRecord = readBlob(br);
    return r;
}

// ── ServerCheckpoint (de)serialization ────────────────────────────────────────

std::vector<uint8_t> serializeCheckpoint(const ServerCheckpoint& cp) {
    ByteWriter bw;
    bw.writeU16(cp.versionMajor);
    bw.writeU16(cp.versionMinor);
    bw.writeU32(cp.engineVersion);
    bw.writeU32(cp.gameVersion);
    bw.writeU64(cp.tickNumber);
    uint64_t timeBits;
    std::memcpy(&timeBits, &cp.serverTimeSec, sizeof(timeBits));
    bw.writeU64(timeBits);
    bw.writeU64(cp.unixTimestamp);
    bw.writeString(cp.mapName);
    bw.writeString(cp.modeName);

    bw.writeU32(static_cast<uint32_t>(cp.entities.size()));
    for (const auto& e : cp.entities) {
        bw.writeU32(e.netId);
        bw.writeU16(static_cast<uint16_t>(e.components.size()));
        for (const auto& c : e.components) {
            bw.writeU8(c.typeId);
            bw.writeU16(static_cast<uint16_t>(c.data.size()));
            bw.writeBytes(c.data.data(), c.data.size());
        }
    }
    writeBlob(bw, cp.gameModeBlob);
    bw.writeU16(static_cast<uint16_t>(cp.players.size()));
    for (const auto& pl : cp.players) {
        bw.writeU64(pl.accountId);
        bw.writeU16(pl.lastKnownPingMs);
    }
    writeBlob(bw, cp.gameExtensionData);
    return bw.data();
}

bool deserializeCheckpoint(const std::vector<uint8_t>& payload, ServerCheckpoint& cp) {
    ByteReader br(payload.data(), payload.size());
    cp.versionMajor  = br.readU16();
    cp.versionMinor  = br.readU16();
    cp.engineVersion = br.readU32();
    cp.gameVersion   = br.readU32();
    cp.tickNumber    = br.readU64();
    const uint64_t timeBits = br.readU64();
    std::memcpy(&cp.serverTimeSec, &timeBits, sizeof(cp.serverTimeSec));
    cp.unixTimestamp = br.readU64();
    cp.mapName       = br.readString();
    cp.modeName      = br.readString();

    const uint32_t entCount = br.readU32();
    for (uint32_t i = 0; i < entCount && br.ok(); ++i) {
        CheckpointEntity e{};
        e.netId = br.readU32();
        const uint16_t compCount = br.readU16();
        for (uint16_t c = 0; c < compCount && br.ok(); ++c) {
            CheckpointEntity::Component comp{};
            comp.typeId = br.readU8();
            const uint16_t sz = br.readU16();
            comp.data.resize(sz);
            if (sz > 0) br.readBytes(comp.data.data(), sz);
            e.components.push_back(std::move(comp));
        }
        cp.entities.push_back(std::move(e));
    }
    cp.gameModeBlob = readBlob(br);
    const uint16_t playerCount = br.readU16();
    for (uint16_t i = 0; i < playerCount && br.ok(); ++i) {
        CheckpointPlayer pl{};
        pl.accountId       = br.readU64();
        pl.lastKnownPingMs = br.readU16();
        cp.players.push_back(pl);
    }
    cp.gameExtensionData = readBlob(br);
    return br.ok();
}

std::string toHex8(uint64_t v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i) {
        s[static_cast<size_t>(i)] = digits[v & 0xFu];
        v >>= 4;
    }
    return s;
}

std::string checkpointFileName(uint64_t tick) {
    std::string num = std::to_string(tick);
    if (num.size() < 8) num.insert(0, 8 - num.size(), '0');
    return "checkpoint_tick_" + num + ".checkpoint";
}

} // anonymous namespace

// ── SaveSystem ─────────────────────────────────────────────────────────────────

SaveSystem::SaveSystem()
    : SaveSystem(std::filesystem::current_path() / "serverdata") {}

SaveSystem::SaveSystem(std::filesystem::path root)
    : root_(std::move(root)) {
    cleanupOrphanedTempFiles();
}

void SaveSystem::setRootPath(std::filesystem::path root) {
    root_ = std::move(root);
    cleanupOrphanedTempFiles();
}

// ── Profiles ───────────────────────────────────────────────────────────────────

PlayerProfile SaveSystem::loadProfile(uint64_t accountId) const {
    const auto path = profilesDir() / (std::to_string(accountId) + ".profile");
    std::vector<uint8_t> file;
    if (!readWholeFile(path, file)) {
        PlayerProfile p{};
        p.accountId = accountId;
        return p;
    }
    std::vector<uint8_t> payload;
    if (!unpackContainer(kProfileMagic, file, payload)) {
        PlayerProfile p{};
        p.accountId = accountId;
        return p;
    }
    return deserializeProfile(payload);
}

bool SaveSystem::saveProfile(uint64_t accountId, const PlayerProfile& profile) const {
    const auto path = profilesDir() / (std::to_string(accountId) + ".profile");
    const std::vector<uint8_t> file = packContainer(kProfileMagic, serializeProfile(profile));
    return atomicWrite(path, file);
}

bool SaveSystem::profileExists(uint64_t accountId) const {
    const auto path = profilesDir() / (std::to_string(accountId) + ".profile");
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool SaveSystem::deleteProfile(uint64_t accountId) const {
    const auto path = profilesDir() / (std::to_string(accountId) + ".profile");
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

// ── Match records ───────────────────────────────────────────────────────────────

std::string SaveSystem::writeMatchRecord(const MatchRecord& record) const {
    const std::string name = toHex8(record.matchId) + ".matchrecord";
    const auto path = matchRecordsDir() / name;
    const std::vector<uint8_t> file = packContainer(kMatchMagic, serializeMatchRecord(record));
    return atomicWrite(path, file) ? name : std::string{};
}

std::optional<MatchRecord> SaveSystem::loadMatchRecord(uint64_t matchId) const {
    const auto path = matchRecordsDir() / (toHex8(matchId) + ".matchrecord");
    std::vector<uint8_t> file;
    if (!readWholeFile(path, file)) return std::nullopt;
    std::vector<uint8_t> payload;
    if (!unpackContainer(kMatchMagic, file, payload)) return std::nullopt;
    return deserializeMatchRecord(payload);
}

// ── Checkpoints ─────────────────────────────────────────────────────────────────

std::filesystem::path SaveSystem::createCheckpoint(const ServerCheckpoint& cp) const {
    const auto path = checkpointsDir() / checkpointFileName(cp.tickNumber);
    const std::vector<uint8_t> file = packContainer(kCheckpointMagic, serializeCheckpoint(cp));
    if (!atomicWrite(path, file)) return {};
    // Maintain latest.checkpoint as a copy (not a symlink, for portability).
    std::error_code ec;
    const auto latest = checkpointsDir() / "latest.checkpoint";
    std::filesystem::copy_file(path, latest,
        std::filesystem::copy_options::overwrite_existing, ec);
    return path;
}

std::optional<ServerCheckpoint> SaveSystem::loadCheckpoint(
        const std::filesystem::path& path) const {
    std::vector<uint8_t> file;
    if (!readWholeFile(path, file)) return std::nullopt;
    std::vector<uint8_t> payload;
    if (!unpackContainer(kCheckpointMagic, file, payload)) return std::nullopt;
    ServerCheckpoint cp{};
    if (!deserializeCheckpoint(payload, cp)) return std::nullopt;
    return cp;
}

std::filesystem::path SaveSystem::latestCheckpointPath() const {
    const auto latest = checkpointsDir() / "latest.checkpoint";
    std::error_code ec;
    if (std::filesystem::exists(latest, ec)) return latest;
    return {};
}

// ── Maintenance ─────────────────────────────────────────────────────────────────

void SaveSystem::cleanupOrphanedTempFiles() const {
    std::error_code ec;
    for (const auto& dir : {profilesDir(), matchRecordsDir(), checkpointsDir()}) {
        if (!std::filesystem::exists(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.path().extension() == ".tmp")
                std::filesystem::remove(entry.path(), ec);
        }
    }
}

} // namespace engine::tools
