#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "boltdb/freelist.hh"
#include "boltdb/page.hh"
#include "boltdb/types.hh"

namespace boltdb {

// ============================================================
// Helpers
// ============================================================

static auto AllocatePage(std::size_t total_bytes) {
  auto buf = std::make_unique<std::byte[]>(total_bytes);
  std::memset(buf.get(), 0, total_bytes);
  return buf;
}

static Page* AsPage(std::byte* buf) { return reinterpret_cast<Page*>(buf); }

/// Generate `n` random, sorted PageIds (mirrors Go randomPgids with seed 42).
static std::vector<PageId> RandomPgids(int n) {
  std::mt19937_64 rng(42);
  std::vector<PageId> ids(n);
  for (int i = 0; i < n; ++i) {
    ids[i] = PageId{rng()};
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

// ============================================================
// Benchmark: FreeList::Release
// Ported from Go benchmark_FreelistRelease
// ============================================================

static void BM_FreelistRelease(benchmark::State& state) {
  const int size = state.range(0);
  auto ids = RandomPgids(size);
  auto pending = RandomPgids(size / 400);

  for (auto _ : state) {
    // Build a FreeList with ids and one pending txn.
    FreeList fl;

    // Deserialize ids into freelist.
    std::size_t buf_size = sizeof(Page) + ids.size() * sizeof(PageId) + 64;
    auto buf = AllocatePage(buf_size);
    auto* page = AsPage(buf.get());
    page->SetFlags(PageFlag::kFreelist);
    page->SetCount(
        static_cast<uint16_t>(std::min<std::size_t>(ids.size(), 65534)));
    std::memcpy(page->DataPtr(), ids.data(), ids.size() * sizeof(PageId));
    fl.Deserialize(page);

    // Add pending pages via Free calls.
    // We simulate by freeing individual pages under txid 1.
    for (auto id : pending) {
      auto pbuf = AllocatePage(sizeof(Page) + 64);
      auto* pp = AsPage(pbuf.get());
      pp->SetId(id);
      pp->SetFlags(PageFlag::kLeaf);
      pp->SetOverflow(0);
      fl.Free(1, pp);
    }

    // This is what we're benchmarking.
    fl.Release(1);

    benchmark::DoNotOptimize(fl.Size());
  }
}

BENCHMARK(BM_FreelistRelease)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Arg(10000000)
    ->Unit(benchmark::kMillisecond);

// ============================================================
// Benchmark: FreeList::AllocContiguous
// ============================================================

static void BM_FreelistAllocate(benchmark::State& state) {
  const int size = state.range(0);
  auto ids = RandomPgids(size);

  for (auto _ : state) {
    FreeList fl;
    std::size_t buf_size = sizeof(Page) + ids.size() * sizeof(PageId) + 64;
    auto buf = AllocatePage(buf_size);
    auto* page = AsPage(buf.get());
    page->SetFlags(PageFlag::kFreelist);
    page->SetCount(
        static_cast<uint16_t>(std::min<std::size_t>(ids.size(), 65534)));
    std::memcpy(page->DataPtr(), ids.data(), ids.size() * sizeof(PageId));
    fl.Deserialize(page);

    // Repeatedly allocate single pages.
    while (fl.AllocContiguous(1) != kSentinelPageId) {
    }

    benchmark::DoNotOptimize(fl.Size());
  }
}

BENCHMARK(BM_FreelistAllocate)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMillisecond);

// ============================================================
// Benchmark: FreeList::Deserialize + Serialize
// ============================================================

static void BM_FreelistReadWrite(benchmark::State& state) {
  const int size = state.range(0);
  auto ids = RandomPgids(size);

  std::size_t buf_size = sizeof(Page) + ids.size() * sizeof(PageId) + 64;
  auto buf = AllocatePage(buf_size);
  auto* page = AsPage(buf.get());
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(
      static_cast<uint16_t>(std::min<std::size_t>(ids.size(), 65534)));
  std::memcpy(page->DataPtr(), ids.data(), ids.size() * sizeof(PageId));

  for (auto _ : state) {
    FreeList fl;
    fl.Deserialize(page);

    auto out_buf = AllocatePage(buf_size);
    auto* out_page = AsPage(out_buf.get());
    out_page->SetFlags(PageFlag::kFreelist);
    fl.Serialize(out_page);

    benchmark::DoNotOptimize(out_page->Count());
  }
}

BENCHMARK(BM_FreelistReadWrite)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMillisecond);

}  // namespace boltdb

BENCHMARK_MAIN();
