#pragma once

#include "tools/PlayerProfile.h"
#include "tools/MatchRecord.h"
#include "tools/ServerCheckpoint.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine::tools {

// Local-file persistence backend for server-side save data:
//   profiles/<accountId>.profile        (PlayerProfile)
//   matchrecords/<matchId-hex>.matchrecord
//   checkpoints/checkpoint_tick_<tick>.checkpoint (+ latest.checkpoint copy)
//
// All writes are atomic (write to <path>.tmp, then rename over the target).
// Payloads are LZ4-compressed and guarded by a CRC32. Deserialization is
// forward-compatible: unknown trailing bytes are ignored and missing trailing
// fields default. The root path is configurable for tests.
class SaveSystem {
public:
    SaveSystem();
    explicit SaveSystem(std::filesystem::path root);

    // Storage root. Subdirectories are created on demand.
    void                         setRootPath(std::filesystem::path root);
    const std::filesystem::path& rootPath() const noexcept { return root_; }

    // Game-defined version packed into checkpoint headers.
    void     setGameVersion(uint32_t v) noexcept { gameVersion_ = v; }
    uint32_t gameVersion() const noexcept { return gameVersion_; }

    // ── Player profiles ──────────────────────────────────────────────────────
    // Returns a default profile (with accountId set) if no file exists.
    PlayerProfile loadProfile(uint64_t accountId) const;
    bool          saveProfile(uint64_t accountId, const PlayerProfile& profile) const;
    bool          profileExists(uint64_t accountId) const;
    bool          deleteProfile(uint64_t accountId) const;

    // ── Match records ────────────────────────────────────────────────────────
    // Writes <matchId-hex>.matchrecord; returns the filename (no directory),
    // or empty string on failure.
    std::string                writeMatchRecord(const MatchRecord& record) const;
    std::optional<MatchRecord> loadMatchRecord(uint64_t matchId) const;

    // ── Checkpoints ──────────────────────────────────────────────────────────
    // Writes checkpoint_tick_<tick>.checkpoint and copies it to
    // latest.checkpoint; returns the written path, or empty on failure.
    std::filesystem::path           createCheckpoint(const ServerCheckpoint& cp) const;
    std::optional<ServerCheckpoint> loadCheckpoint(const std::filesystem::path& path) const;
    std::filesystem::path           latestCheckpointPath() const;

    // Deletes orphaned *.tmp files left by an interrupted atomic write.
    // Called by the constructor; exposed for manual invocation.
    void cleanupOrphanedTempFiles() const;

private:
    std::filesystem::path profilesDir()     const { return root_ / "profiles"; }
    std::filesystem::path matchRecordsDir() const { return root_ / "matchrecords"; }
    std::filesystem::path checkpointsDir()  const { return root_ / "checkpoints"; }

    std::filesystem::path root_;
    uint32_t              gameVersion_ = 0;
};

} // namespace engine::tools
