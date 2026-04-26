// tests/benchmarks/tools_bench.cpp
// Tools module microbenchmarks (Task #38).

#include <benchmark/benchmark.h>
#include "core/log.h"

// TODO: requires headless cooker invocation
// static void BM_AssetCook_Simple(benchmark::State& state) { ... }

// ---------------------------------------------------------------------------
// BM_LoggerThroughput — LOG_INFO rate, single thread, 100k messages
// ---------------------------------------------------------------------------

static void BM_LoggerThroughput(benchmark::State& state) {
    // Register a no-op sink to avoid file I/O overhead.
    engine::core::log::gLogFn = [](const engine::core::log::LogEntry&) {};
    for (auto _ : state) {
        for (int i = 0; i < 100'000; ++i) {
            LOG_INFO("benchmark message {}", i);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100'000);
}
BENCHMARK(BM_LoggerThroughput)->Iterations(1);
