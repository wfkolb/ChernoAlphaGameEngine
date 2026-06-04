#include <gtest/gtest.h>

#include <tools/SaveSystem.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace engine::tools;
namespace fs = std::filesystem;

namespace {

// Each test gets a unique, isolated root under the temp directory.
class SaveSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::random_device rd;
        root_ = fs::temp_directory_path() /
                ("savesys_test_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        fs::remove_all(root_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path root_;
};

PlayerProfile makeProfile() {
    PlayerProfile p{};
    p.accountId         = 1001;
    p.displayName       = "Nova";
    p.firstSeenUnixTime = 1700000000ull;
    p.lastSeenUnixTime  = 1700009999ull;
    p.loadout[0]        = {42, 7};
    p.loadout[1]        = {13, 0};
    p.loadout[2]        = {99, 3};
    p.stats.kills       = 1234;
    p.stats.deaths      = 567;
    p.stats.matchesWon  = 12;
    p.stats.totalDamageDealt = 98765.5f;
    p.mouseSensitivity  = 2.5f;
    p.invertPitchAxis   = true;
    p.bindingOverrides  = {1, 2, 3, 4, 5};
    p.gameExtensionData = {0xDE, 0xAD, 0xBE, 0xEF};
    return p;
}

} // namespace

// ── Profiles ────────────────────────────────────────────────────────────────────

TEST_F(SaveSystemTest, LoadMissingProfileReturnsDefaultWithAccountId) {
    SaveSystem ss(root_);
    PlayerProfile p = ss.loadProfile(7777);
    EXPECT_EQ(p.accountId, 7777u);
    EXPECT_TRUE(p.displayName.empty());
    EXPECT_EQ(p.stats.kills, 0u);
    EXPECT_FALSE(ss.profileExists(7777));
}

TEST_F(SaveSystemTest, ProfileRoundTrip) {
    SaveSystem ss(root_);
    const PlayerProfile in = makeProfile();
    ASSERT_TRUE(ss.saveProfile(in.accountId, in));
    ASSERT_TRUE(ss.profileExists(in.accountId));

    const PlayerProfile out = ss.loadProfile(in.accountId);
    EXPECT_EQ(out.accountId, in.accountId);
    EXPECT_EQ(out.displayName, in.displayName);
    EXPECT_EQ(out.firstSeenUnixTime, in.firstSeenUnixTime);
    EXPECT_EQ(out.lastSeenUnixTime, in.lastSeenUnixTime);
    EXPECT_EQ(out.loadout[0].weaponDefId, 42u);
    EXPECT_EQ(out.loadout[2].skinId, 3u);
    EXPECT_EQ(out.stats.kills, 1234u);
    EXPECT_EQ(out.stats.matchesWon, 12u);
    EXPECT_FLOAT_EQ(out.stats.totalDamageDealt, 98765.5f);
    EXPECT_FLOAT_EQ(out.mouseSensitivity, 2.5f);
    EXPECT_TRUE(out.invertPitchAxis);
    EXPECT_EQ(out.bindingOverrides, in.bindingOverrides);
    EXPECT_EQ(out.gameExtensionData, in.gameExtensionData);
}

TEST_F(SaveSystemTest, SaveProfileOverwrites) {
    SaveSystem ss(root_);
    PlayerProfile p = makeProfile();
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));
    p.stats.kills = 9999;
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));
    EXPECT_EQ(ss.loadProfile(p.accountId).stats.kills, 9999u);
}

TEST_F(SaveSystemTest, DeleteProfile) {
    SaveSystem ss(root_);
    const PlayerProfile p = makeProfile();
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));
    ASSERT_TRUE(ss.profileExists(p.accountId));
    EXPECT_TRUE(ss.deleteProfile(p.accountId));
    EXPECT_FALSE(ss.profileExists(p.accountId));
}

TEST_F(SaveSystemTest, AtomicWriteLeavesNoTempFile) {
    SaveSystem ss(root_);
    const PlayerProfile p = makeProfile();
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));

    bool foundTmp = false;
    for (const auto& e : fs::recursive_directory_iterator(root_))
        if (e.path().extension() == ".tmp") foundTmp = true;
    EXPECT_FALSE(foundTmp);
}

TEST_F(SaveSystemTest, OrphanedTempFilesCleanedOnConstruct) {
    fs::create_directories(root_ / "profiles");
    const auto orphan = root_ / "profiles" / "1001.profile.tmp";
    { std::ofstream o(orphan, std::ios::binary); o << "stale"; }
    ASSERT_TRUE(fs::exists(orphan));

    SaveSystem ss(root_);   // constructor cleans orphaned .tmp files
    EXPECT_FALSE(fs::exists(orphan));
}

TEST_F(SaveSystemTest, CorruptedProfileFallsBackToDefault) {
    SaveSystem ss(root_);
    const PlayerProfile p = makeProfile();
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));

    // Flip bytes in the compressed payload region to break the CRC.
    const auto path = root_ / "profiles" / "1001.profile";
    std::vector<char> bytes;
    { std::ifstream in(path, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), {}); }
    ASSERT_GT(bytes.size(), 12u);
    bytes[bytes.size() - 1] ^= 0xFF;
    { std::ofstream out(path, std::ios::binary); out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }

    const PlayerProfile out = ss.loadProfile(p.accountId);
    EXPECT_EQ(out.accountId, p.accountId);
    EXPECT_TRUE(out.displayName.empty());   // corrupt → default
}

// ── Match records ───────────────────────────────────────────────────────────────

TEST_F(SaveSystemTest, MatchRecordRoundTrip) {
    SaveSystem ss(root_);
    MatchRecord r{};
    r.matchId       = 0xA3F9B2C1ull;
    r.startUnixTime = 1700000000ull;
    r.durationSec   = 612.5f;
    r.mapName       = "de_harbor";
    r.gameModeName  = "team_deathmatch";
    r.serverVersion = 0x00010002u;
    r.winningTeam   = 1;
    r.roundsPlayed  = 13;
    r.roundsWon[0]  = 6;
    r.roundsWon[1]  = 7;
    MatchRecord::PlayerRecord pr{};
    pr.accountId   = 1001;
    pr.displayName = "Nova";
    pr.team        = 1;
    pr.kills       = 25;
    pr.accuracy    = 0.42f;
    r.players.push_back(pr);
    r.gameModeRecord = {0x11, 0x22, 0x33};

    const std::string name = ss.writeMatchRecord(r);
    EXPECT_FALSE(name.empty());

    const auto loaded = ss.loadMatchRecord(r.matchId);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->matchId, r.matchId);
    EXPECT_EQ(loaded->mapName, "de_harbor");
    EXPECT_EQ(loaded->gameModeName, "team_deathmatch");
    EXPECT_EQ(loaded->winningTeam, 1u);
    EXPECT_EQ(loaded->roundsWon[1], 7);
    ASSERT_EQ(loaded->players.size(), 1u);
    EXPECT_EQ(loaded->players[0].displayName, "Nova");
    EXPECT_EQ(loaded->players[0].kills, 25u);
    EXPECT_FLOAT_EQ(loaded->players[0].accuracy, 0.42f);
    EXPECT_EQ(loaded->gameModeRecord, r.gameModeRecord);
}

TEST_F(SaveSystemTest, LoadMissingMatchRecordReturnsNullopt) {
    SaveSystem ss(root_);
    EXPECT_FALSE(ss.loadMatchRecord(0xDEADBEEFull).has_value());
}

// ── Checkpoints ─────────────────────────────────────────────────────────────────

TEST_F(SaveSystemTest, CheckpointRoundTrip) {
    SaveSystem ss(root_);
    ss.setGameVersion(0x00000003u);

    ServerCheckpoint cp{};
    cp.engineVersion = 0x00010000u;
    cp.gameVersion   = ss.gameVersion();
    cp.tickNumber    = 512000;
    cp.serverTimeSec = 8000.0;
    cp.unixTimestamp = 1700000000ull;
    cp.mapName       = "de_harbor";
    cp.modeName      = "bomb_defusal";

    CheckpointEntity e{};
    e.netId = 7;
    e.components.push_back({3, {1, 2, 3, 4}});       // typeId 3 (Health)
    e.components.push_back({1, {5, 6, 7, 8, 9, 10}}); // typeId 1 (Transform)
    cp.entities.push_back(e);

    cp.gameModeBlob = {0xAA, 0xBB};
    cp.players.push_back({1001, 35});
    cp.players.push_back({1002, 80});

    const auto path = ss.createCheckpoint(cp);
    ASSERT_FALSE(path.empty());
    EXPECT_TRUE(fs::exists(path));
    EXPECT_FALSE(ss.latestCheckpointPath().empty());

    const auto loaded = ss.loadCheckpoint(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->tickNumber, 512000u);
    EXPECT_DOUBLE_EQ(loaded->serverTimeSec, 8000.0);
    EXPECT_EQ(loaded->mapName, "de_harbor");
    EXPECT_EQ(loaded->modeName, "bomb_defusal");
    ASSERT_EQ(loaded->entities.size(), 1u);
    EXPECT_EQ(loaded->entities[0].netId, 7u);
    ASSERT_EQ(loaded->entities[0].components.size(), 2u);
    EXPECT_EQ(loaded->entities[0].components[0].typeId, 3u);
    EXPECT_EQ(loaded->entities[0].components[1].data.size(), 6u);
    EXPECT_EQ(loaded->gameModeBlob, cp.gameModeBlob);
    ASSERT_EQ(loaded->players.size(), 2u);
    EXPECT_EQ(loaded->players[1].accountId, 1002u);
    EXPECT_EQ(loaded->players[1].lastKnownPingMs, 80u);
}

TEST_F(SaveSystemTest, LatestCheckpointTracksMostRecent) {
    SaveSystem ss(root_);
    ServerCheckpoint cp{};
    cp.tickNumber = 100;
    cp.mapName    = "m1";
    ss.createCheckpoint(cp);

    cp.tickNumber = 200;
    cp.mapName    = "m2";
    ss.createCheckpoint(cp);

    const auto latest = ss.loadCheckpoint(ss.latestCheckpointPath());
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->tickNumber, 200u);
    EXPECT_EQ(latest->mapName, "m2");
}

TEST_F(SaveSystemTest, LoadCheckpointMissingReturnsNullopt) {
    SaveSystem ss(root_);
    EXPECT_FALSE(ss.loadCheckpoint(root_ / "checkpoints" / "nope.checkpoint").has_value());
}

TEST_F(SaveSystemTest, EmptyCheckpointRoundTrips) {
    SaveSystem ss(root_);
    ServerCheckpoint cp{};
    cp.tickNumber = 0;
    const auto path = ss.createCheckpoint(cp);
    ASSERT_FALSE(path.empty());
    const auto loaded = ss.loadCheckpoint(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->entities.empty());
    EXPECT_TRUE(loaded->players.empty());
}

// ── Root path reconfiguration ─────────────────────────────────────────────────

TEST_F(SaveSystemTest, SetRootPathRedirectsStorage) {
    SaveSystem ss(root_);
    const fs::path alt = root_ / "alt";
    ss.setRootPath(alt);

    const PlayerProfile p = makeProfile();
    ASSERT_TRUE(ss.saveProfile(p.accountId, p));
    EXPECT_TRUE(fs::exists(alt / "profiles" / "1001.profile"));
    EXPECT_EQ(ss.rootPath(), alt);
}
