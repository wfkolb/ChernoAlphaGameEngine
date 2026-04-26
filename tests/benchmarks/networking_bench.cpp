// tests/benchmarks/networking_bench.cpp
// Networking module microbenchmarks (Task #38).

#include <benchmark/benchmark.h>
#include "networking/Snapshot.h"
#include "networking/Serializer.h"
#include "core/math/Quat.h"

#include <cstdint>
#include <vector>

using namespace engine::networking;
using namespace engine::core::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<SnapshotEntry> make1kEntries() {
    std::vector<SnapshotEntry> entries;
    entries.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        SnapshotEntry entry;
        entry.entity = {static_cast<uint32_t>(i), 1u};
        entry.mask.set(0);
        entry.mask.set(1);
        entry.componentData.assign(32, static_cast<uint8_t>(i & 0xFF));
        entries.push_back(std::move(entry));
    }
    return entries;
}

// ---------------------------------------------------------------------------
// BM_SnapshotEncode_1k
// ---------------------------------------------------------------------------

static void BM_SnapshotEncode_1k(benchmark::State& state) {
    const auto entries = make1kEntries();
    for (auto _ : state) {
        auto bytes = SnapshotEncoder::encode(entries);
        benchmark::DoNotOptimize(bytes);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_SnapshotEncode_1k);

// ---------------------------------------------------------------------------
// BM_SnapshotDecode_1k
// ---------------------------------------------------------------------------

class SnapshotDecodFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) override {
        if (!encoded_.empty()) return;
        encoded_ = SnapshotEncoder::encode(make1kEntries());
    }
    void TearDown(const benchmark::State&) override {}

    std::vector<uint8_t> encoded_;
};

BENCHMARK_DEFINE_F(SnapshotDecodFixture, BM_SnapshotDecode_1k)(benchmark::State& state) {
    for (auto _ : state) {
        auto entries = SnapshotDecoder::decode(encoded_);
        benchmark::DoNotOptimize(entries);
        benchmark::ClobberMemory();
    }
}
BENCHMARK_REGISTER_F(SnapshotDecodFixture, BM_SnapshotDecode_1k);

// ---------------------------------------------------------------------------
// BM_SerializerBitWrite — 1000 x writeU32
// ---------------------------------------------------------------------------

static void BM_SerializerBitWrite(benchmark::State& state) {
    // 1000 x 32 bits = 4000 bytes; allocate with headroom.
    std::vector<uint8_t> buf(4096, 0u);
    for (auto _ : state) {
        BitWriter w(buf);
        for (int i = 0; i < 1000; ++i) {
            w.writeBits(static_cast<uint64_t>(i), 32);
        }
        benchmark::DoNotOptimize(w.bitsWritten());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_SerializerBitWrite);

// ---------------------------------------------------------------------------
// BM_QuatSmallestThree — encode + decode 1M quaternions
// ---------------------------------------------------------------------------

static void BM_QuatSmallestThree(benchmark::State& state) {
    // Each quat uses 32 bits (4 bytes padded); 1M quats = 4 MB buffer.
    constexpr int kCount = 1'000'000;
    std::vector<uint8_t> buf(static_cast<size_t>(kCount) * 4, 0u);

    const Quat q = fromAxisAngle({0.577f, 0.577f, 0.577f}, 1.047f);

    for (auto _ : state) {
        // Encode
        {
            BitWriter w(buf);
            for (int i = 0; i < kCount; ++i) {
                w.writeQuatSmallestThree(q);
            }
            benchmark::DoNotOptimize(w.bitsWritten());
        }
        // Decode
        {
            BitReader r(buf);
            for (int i = 0; i < kCount; ++i) {
                Quat out = r.readQuatSmallestThree();
                benchmark::DoNotOptimize(out);
            }
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_QuatSmallestThree);
