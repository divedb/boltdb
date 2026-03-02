#include "freelist.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "page.hh"
#include "types.hh"

namespace boltdb {

// ============================================================
// Helper utilities
// ============================================================

/// Allocate a buffer large enough for a page header + some data.
static auto AllocatePage(std::size_t total_bytes) {
  auto buf = std::make_unique<std::byte[]>(total_bytes);
  std::memset(buf.get(), 0, total_bytes);
  return buf;
}

static Page* AsPage(std::byte* buf) { return reinterpret_cast<Page*>(buf); }

/// Convenience: build a vector of PageIds from a brace-init list of ints.
static std::vector<PageId> MakeIds(std::initializer_list<uint64_t> vals) {
  std::vector<PageId> ids;
  ids.reserve(vals.size());
  for (auto v : vals) ids.push_back(PageId{v});
  return ids;
}

// ============================================================
// PageRangeSet tests
// ============================================================

class PageRangeSetTest : public ::testing::Test {
 protected:
  PageRangeSet rs;
};

TEST_F(PageRangeSetTest, EmptyByDefault) {
  EXPECT_EQ(rs.Size(), 0);
  EXPECT_TRUE(rs.IsEmpty());
}

TEST_F(PageRangeSetTest, AddSingleId) {
  rs.Add(PageId{5});
  EXPECT_EQ(rs.Size(), 1);
  EXPECT_TRUE(rs.Contains(PageId{5}));
  EXPECT_FALSE(rs.Contains(PageId{4}));
  EXPECT_FALSE(rs.Contains(PageId{6}));
}

TEST_F(PageRangeSetTest, AddMergesAdjacent) {
  rs.Add(PageId{3});
  rs.Add(PageId{5});
  EXPECT_EQ(rs.Ranges().size(), 2u);  // two disjoint ranges

  rs.Add(PageId{4});                  // fills the gap
  EXPECT_EQ(rs.Ranges().size(), 1u);  // merged into [3,5]
  EXPECT_EQ(rs.Size(), 3);
  EXPECT_TRUE(rs.Contains(PageId{3}));
  EXPECT_TRUE(rs.Contains(PageId{4}));
  EXPECT_TRUE(rs.Contains(PageId{5}));
}

TEST_F(PageRangeSetTest, AddRangeMergesOverlapping) {
  rs.AddRange(PageId{2}, PageId{4});
  rs.AddRange(PageId{6}, PageId{8});
  EXPECT_EQ(rs.Ranges().size(), 2u);
  EXPECT_EQ(rs.Size(), 6);

  rs.AddRange(PageId{4}, PageId{6});  // overlaps both
  EXPECT_EQ(rs.Ranges().size(), 1u);  // merged into [2,8]
  EXPECT_EQ(rs.Size(), 7);
}

TEST_F(PageRangeSetTest, RemoveSplitsRange) {
  rs.AddRange(PageId{10}, PageId{20});
  EXPECT_EQ(rs.Size(), 11);

  rs.Remove(PageId{15});
  EXPECT_EQ(rs.Size(), 10);
  EXPECT_FALSE(rs.Contains(PageId{15}));
  EXPECT_TRUE(rs.Contains(PageId{14}));
  EXPECT_TRUE(rs.Contains(PageId{16}));
  EXPECT_EQ(rs.Ranges().size(), 2u);  // [10,14] and [16,20]
}

TEST_F(PageRangeSetTest, RemoveFromStart) {
  rs.AddRange(PageId{10}, PageId{15});
  rs.Remove(PageId{10});
  EXPECT_EQ(rs.Size(), 5);
  EXPECT_FALSE(rs.Contains(PageId{10}));
  EXPECT_TRUE(rs.Contains(PageId{11}));
}

TEST_F(PageRangeSetTest, RemoveFromEnd) {
  rs.AddRange(PageId{10}, PageId{15});
  rs.Remove(PageId{15});
  EXPECT_EQ(rs.Size(), 5);
  EXPECT_FALSE(rs.Contains(PageId{15}));
  EXPECT_TRUE(rs.Contains(PageId{14}));
}

TEST_F(PageRangeSetTest, RemoveSingletonRange) {
  rs.Add(PageId{7});
  rs.Remove(PageId{7});
  EXPECT_EQ(rs.Size(), 0);
  EXPECT_TRUE(rs.IsEmpty());
}

TEST_F(PageRangeSetTest, RemoveNonexistent) {
  rs.AddRange(PageId{10}, PageId{15});
  rs.Remove(PageId{20});  // not in set
  EXPECT_EQ(rs.Size(), 6);
}

TEST_F(PageRangeSetTest, ContainsEdges) {
  rs.AddRange(PageId{100}, PageId{200});
  EXPECT_TRUE(rs.Contains(PageId{100}));
  EXPECT_TRUE(rs.Contains(PageId{200}));
  EXPECT_FALSE(rs.Contains(PageId{99}));
  EXPECT_FALSE(rs.Contains(PageId{201}));
}

TEST_F(PageRangeSetTest, AllocContiguousExactFit) {
  rs.AddRange(PageId{10}, PageId{14});  // 5 pages
  PageId start = rs.AllocContiguous(5);
  EXPECT_EQ(start, PageId{10});
  EXPECT_EQ(rs.Size(), 0);
  EXPECT_TRUE(rs.IsEmpty());
}

TEST_F(PageRangeSetTest, AllocContiguousPartial) {
  rs.AddRange(PageId{10}, PageId{19});  // 10 pages
  PageId start = rs.AllocContiguous(3);
  EXPECT_EQ(start, PageId{10});
  EXPECT_EQ(rs.Size(), 7);
  EXPECT_FALSE(rs.Contains(PageId{10}));
  EXPECT_FALSE(rs.Contains(PageId{11}));
  EXPECT_FALSE(rs.Contains(PageId{12}));
  EXPECT_TRUE(rs.Contains(PageId{13}));
}

TEST_F(PageRangeSetTest, AllocContiguousSkipsSmallRanges) {
  rs.AddRange(PageId{2}, PageId{3});    // 2 pages
  rs.AddRange(PageId{10}, PageId{19});  // 10 pages
  PageId start = rs.AllocContiguous(5);
  EXPECT_EQ(start, PageId{10});
  EXPECT_EQ(rs.Size(), 7);  // 2 from first range + 5 remaining from second
  EXPECT_TRUE(rs.Contains(PageId{2}));
  EXPECT_TRUE(rs.Contains(PageId{3}));
}

TEST_F(PageRangeSetTest, AllocContiguousFails) {
  rs.AddRange(PageId{2}, PageId{3});
  rs.AddRange(PageId{10}, PageId{12});
  PageId start = rs.AllocContiguous(5);
  EXPECT_EQ(start, kSentinelPageId);
  EXPECT_EQ(rs.Size(), 5);  // nothing removed
}

TEST_F(PageRangeSetTest, AllocContiguousZeroOrNegative) {
  rs.AddRange(PageId{1}, PageId{10});
  EXPECT_EQ(rs.AllocContiguous(0), kSentinelPageId);
  EXPECT_EQ(rs.AllocContiguous(-1), kSentinelPageId);
  EXPECT_EQ(rs.Size(), 10);
}

TEST_F(PageRangeSetTest, ToVectorSorted) {
  rs.Add(PageId{5});
  rs.Add(PageId{3});
  rs.Add(PageId{1});
  rs.AddRange(PageId{10}, PageId{12});

  auto vec = rs.ToVector();
  EXPECT_EQ(vec, MakeIds({1, 3, 5, 10, 11, 12}));
}

TEST_F(PageRangeSetTest, FromVectorBuildsRanges) {
  auto ids = MakeIds({1, 2, 3, 7, 8, 15});
  rs.FromVector(ids);
  EXPECT_EQ(rs.Size(), 6);
  EXPECT_EQ(rs.Ranges().size(), 3u);  // [1,3], [7,8], [15,15]
  EXPECT_TRUE(rs.Contains(PageId{1}));
  EXPECT_TRUE(rs.Contains(PageId{3}));
  EXPECT_TRUE(rs.Contains(PageId{8}));
  EXPECT_TRUE(rs.Contains(PageId{15}));
  EXPECT_FALSE(rs.Contains(PageId{4}));
}

TEST_F(PageRangeSetTest, ClearEmptiesSet) {
  rs.AddRange(PageId{1}, PageId{100});
  rs.Clear();
  EXPECT_EQ(rs.Size(), 0);
  EXPECT_TRUE(rs.IsEmpty());
}

TEST_F(PageRangeSetTest, AddDuplicateId) {
  rs.Add(PageId{5});
  rs.Add(PageId{5});  // duplicate
  EXPECT_EQ(rs.Size(), 1);
  EXPECT_TRUE(rs.Contains(PageId{5}));
}

TEST_F(PageRangeSetTest, AddOverlappingRanges) {
  rs.AddRange(PageId{5}, PageId{10});
  rs.AddRange(PageId{8}, PageId{15});  // overlaps [5,10]
  EXPECT_EQ(rs.Size(), 11);            // [5,15]
  EXPECT_EQ(rs.Ranges().size(), 1u);
}

TEST_F(PageRangeSetTest, RoundTripFromVectorToVector) {
  auto original = MakeIds({2, 3, 4, 10, 11, 50});
  rs.FromVector(original);
  auto result = rs.ToVector();
  EXPECT_EQ(result, original);
}

// ============================================================
// FreeList tests
// ============================================================

class FreeListTest : public ::testing::Test {
 protected:
  FreeList fl;
};

TEST_F(FreeListTest, EmptyByDefault) {
  EXPECT_EQ(fl.Size(), 0);
  EXPECT_EQ(fl.Count(), 0);
  EXPECT_EQ(fl.PendingCount(), 0);
}

TEST_F(FreeListTest, AllocContiguous) {
  // Manually add pages via Read from a fake page.
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{100});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(5);

  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{5};
  data[1] = PageId{6};
  data[2] = PageId{7};
  data[3] = PageId{10};
  data[4] = PageId{11};

  fl.Deserialize(page);
  EXPECT_EQ(fl.Size(), 5);

  // Allocate 3 contiguous pages — should get [5,6,7].
  PageId start = fl.AllocContiguous(3);
  EXPECT_EQ(start, PageId{5});
  EXPECT_EQ(fl.Size(), 2);

  // Allocate 3 more — only [10,11] left, should fail.
  PageId start2 = fl.AllocContiguous(3);
  EXPECT_EQ(start2, kSentinelPageId);
  EXPECT_EQ(fl.Size(), 2);

  // Allocate 2 — should get [10,11].
  PageId start3 = fl.AllocContiguous(2);
  EXPECT_EQ(start3, PageId{10});
  EXPECT_EQ(fl.Size(), 0);
}

TEST_F(FreeListTest, FreeAndRelease) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{5});
  page->SetFlags(PageFlag::kLeaf);
  page->SetOverflow(2);  // pages 5, 6, 7

  fl.Free(100, page);
  EXPECT_EQ(fl.PendingCount(), 3);
  EXPECT_EQ(fl.Size(), 0);

  // Pages should show as freed (pending).
  EXPECT_TRUE(fl.IsFreed(PageId{5}));
  EXPECT_TRUE(fl.IsFreed(PageId{6}));
  EXPECT_TRUE(fl.IsFreed(PageId{7}));
  EXPECT_FALSE(fl.IsFreed(PageId{8}));

  fl.Release(100);
  EXPECT_EQ(fl.PendingCount(), 0);
  EXPECT_EQ(fl.Size(), 3);

  // Now they should be available (freed and released).
  EXPECT_TRUE(fl.IsFreed(PageId{5}));
  EXPECT_TRUE(fl.IsFreed(PageId{6}));
  EXPECT_TRUE(fl.IsFreed(PageId{7}));
}

TEST_F(FreeListTest, Rollback) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{10});
  page->SetFlags(PageFlag::kLeaf);
  page->SetOverflow(0);

  fl.Free(200, page);
  EXPECT_EQ(fl.PendingCount(), 1);

  fl.Rollback(200);
  EXPECT_EQ(fl.PendingCount(), 0);
  EXPECT_EQ(fl.Size(), 0);
  EXPECT_FALSE(fl.IsFreed(PageId{10}));
}

TEST_F(FreeListTest, FreeReservedPagesIgnored) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{0});
  page->SetFlags(PageFlag::kMeta);
  page->SetOverflow(0);

  fl.Free(1, page);
  EXPECT_EQ(fl.PendingCount(), 0);

  page->SetId(PageId{1});
  fl.Free(1, page);
  EXPECT_EQ(fl.PendingCount(), 0);
}

TEST_F(FreeListTest, DeserializeAndSerialize) {
  constexpr std::size_t kBufSize = 4096;

  // Build a freelist page with some ids.
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{99});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(4);

  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{3};
  data[1] = PageId{5};
  data[2] = PageId{6};
  data[3] = PageId{7};

  fl.Deserialize(page);
  EXPECT_EQ(fl.Size(), 4);

  // Serialize to a new page and verify.
  auto buf2 = AllocatePage(kBufSize);
  auto* page2 = AsPage(buf2.get());
  page2->SetId(PageId{99});
  page2->SetFlags(PageFlag::kFreelist);

  fl.Serialize(page2);
  EXPECT_EQ(page2->Count(), 4);
  auto* out = reinterpret_cast<PageId*>(page2->DataPtr());

  // Should be sorted.
  std::vector<PageId> written(out, out + page2->Count());
  EXPECT_EQ(written, MakeIds({3, 5, 6, 7}));
}

TEST_F(FreeListTest, SerializeIncludesPending) {
  constexpr std::size_t kBufSize = 4096;

  // Add some free pages.
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{99});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(2);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{10};
  data[1] = PageId{20};
  fl.Deserialize(page);

  // Add pending pages.
  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{15});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(0);
  fl.Free(300, p2);

  // Serialize should include both free and pending.
  auto buf3 = AllocatePage(kBufSize);
  auto* page3 = AsPage(buf3.get());
  page3->SetFlags(PageFlag::kFreelist);

  fl.Serialize(page3);
  EXPECT_EQ(page3->Count(), 3);
  auto* out = reinterpret_cast<PageId*>(page3->DataPtr());
  std::vector<PageId> written(out, out + page3->Count());
  EXPECT_EQ(written, MakeIds({10, 15, 20}));
}

TEST_F(FreeListTest, SortedMergedFreePages) {
  constexpr std::size_t kBufSize = 4096;

  // Add some free pages via Read.
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{99});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(2);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{10};
  data[1] = PageId{20};
  fl.Deserialize(page);

  // Add pending pages.
  auto buf2 = AllocatePage(kBufSize);
  auto* page2 = AsPage(buf2.get());
  page2->SetId(PageId{15});
  page2->SetFlags(PageFlag::kLeaf);
  page2->SetOverflow(0);
  fl.Free(300, page2);

  auto merged = fl.SortedMergedFreePages();
  EXPECT_EQ(merged, MakeIds({10, 15, 20}));
}

TEST_F(FreeListTest, CountIncludesPending) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{99});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(2);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{10};
  data[1] = PageId{20};
  fl.Deserialize(page);

  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{30});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(1);  // pages 30, 31
  fl.Free(400, p2);

  EXPECT_EQ(fl.Size(), 2);
  EXPECT_EQ(fl.PendingCount(), 2);
  EXPECT_EQ(fl.Count(), 4);
}

TEST_F(FreeListTest, ReloadRemovesPendingFromFree) {
  constexpr std::size_t kBufSize = 4096;

  // Suppose on-disk page has ids 5,6,7,10,11
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{99});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(5);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{5};
  data[1] = PageId{6};
  data[2] = PageId{7};
  data[3] = PageId{10};
  data[4] = PageId{11};

  // Some pages are pending (e.g., page 6 was freed in an ongoing txn).
  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{6});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(0);
  fl.Free(500, p2);

  fl.Reload(page);

  // Page 6 should NOT be in the available set (it's still pending).
  EXPECT_FALSE(fl.RangeSet().Contains(PageId{6}));
  // But the rest should be.
  EXPECT_TRUE(fl.RangeSet().Contains(PageId{5}));
  EXPECT_TRUE(fl.RangeSet().Contains(PageId{7}));
  EXPECT_TRUE(fl.RangeSet().Contains(PageId{10}));
  EXPECT_TRUE(fl.RangeSet().Contains(PageId{11}));
  EXPECT_EQ(fl.Size(), 4);
}

TEST_F(FreeListTest, MultipleTransactionsPending) {
  constexpr std::size_t kBufSize = 4096;

  auto buf1 = AllocatePage(kBufSize);
  auto* p1 = AsPage(buf1.get());
  p1->SetId(PageId{10});
  p1->SetFlags(PageFlag::kLeaf);
  p1->SetOverflow(0);

  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{20});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(1);  // pages 20, 21

  fl.Free(100, p1);
  fl.Free(200, p2);

  EXPECT_EQ(fl.PendingCount(), 3);
  EXPECT_EQ(fl.Size(), 0);

  // Release only txn 100.
  fl.Release(100);
  EXPECT_EQ(fl.PendingCount(), 2);
  EXPECT_EQ(fl.Size(), 1);
  EXPECT_TRUE(fl.RangeSet().Contains(PageId{10}));

  // Rollback txn 200.
  fl.Rollback(200);
  EXPECT_EQ(fl.PendingCount(), 0);
  EXPECT_EQ(fl.Size(), 1);
  EXPECT_FALSE(fl.IsFreed(PageId{20}));
}

// ============================================================
// Go boltdb equivalent tests
// (Ported from github.com/boltdb/bolt/freelist_test.go)
// ============================================================

// Equivalent of Go TestFreelist_free:
// Ensure that a page is added to a transaction's freelist (pending).
TEST_F(FreeListTest, GoFree) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{12});
  page->SetFlags(PageFlag::kLeaf);
  page->SetOverflow(0);

  fl.Free(100, page);
  EXPECT_EQ(fl.PendingCount(), 1);
  EXPECT_TRUE(fl.IsFreed(PageId{12}));
}

// Equivalent of Go TestFreelist_free_overflow:
// Ensure that a page and its overflow is added to a transaction's freelist.
TEST_F(FreeListTest, GoFreeOverflow) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{12});
  page->SetFlags(PageFlag::kLeaf);
  page->SetOverflow(3);  // pages 12, 13, 14, 15

  fl.Free(100, page);
  EXPECT_EQ(fl.PendingCount(), 4);
  EXPECT_TRUE(fl.IsFreed(PageId{12}));
  EXPECT_TRUE(fl.IsFreed(PageId{13}));
  EXPECT_TRUE(fl.IsFreed(PageId{14}));
  EXPECT_TRUE(fl.IsFreed(PageId{15}));
}

// Equivalent of Go TestFreelist_release:
// Ensure that a transaction's free pages can be released.
TEST_F(FreeListTest, GoRelease) {
  constexpr std::size_t kBufSize = 4096;

  // f.free(100, &page{id: 12, overflow: 1})  → pages 12, 13
  auto buf1 = AllocatePage(kBufSize);
  auto* p1 = AsPage(buf1.get());
  p1->SetId(PageId{12});
  p1->SetFlags(PageFlag::kLeaf);
  p1->SetOverflow(1);
  fl.Free(100, p1);

  // f.free(100, &page{id: 9})
  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{9});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(0);
  fl.Free(100, p2);

  // f.free(102, &page{id: 39})
  auto buf3 = AllocatePage(kBufSize);
  auto* p3 = AsPage(buf3.get());
  p3->SetId(PageId{39});
  p3->SetFlags(PageFlag::kLeaf);
  p3->SetOverflow(0);
  fl.Free(102, p3);

  // f.release(100); f.release(101);
  fl.Release(100);
  fl.Release(101);
  // Go expect: f.ids == [9, 12, 13]
  EXPECT_EQ(fl.RangeSet().ToVector(), MakeIds({9, 12, 13}));

  // f.release(102)
  fl.Release(102);
  // Go expect: f.ids == [9, 12, 13, 39]
  EXPECT_EQ(fl.RangeSet().ToVector(), MakeIds({9, 12, 13, 39}));
}

// Equivalent of Go TestFreelist_allocate:
// Ensure that a freelist can find contiguous blocks of pages.
TEST_F(FreeListTest, GoAllocate) {
  // Go: f := &freelist{ids: []pgid{3, 4, 5, 6, 7, 9, 12, 13, 18}}
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetId(PageId{0});
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(9);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{3};
  data[1] = PageId{4};
  data[2] = PageId{5};
  data[3] = PageId{6};
  data[4] = PageId{7};
  data[5] = PageId{9};
  data[6] = PageId{12};
  data[7] = PageId{13};
  data[8] = PageId{18};
  fl.Deserialize(page);

  // allocate(3) == 3
  EXPECT_EQ(fl.AllocContiguous(3), PageId{3});
  // allocate(1) == 6
  EXPECT_EQ(fl.AllocContiguous(1), PageId{6});
  // allocate(3) == 0 (no room → sentinel)
  EXPECT_EQ(fl.AllocContiguous(3), kSentinelPageId);
  // allocate(2) == 12
  EXPECT_EQ(fl.AllocContiguous(2), PageId{12});
  // allocate(1) == 7
  EXPECT_EQ(fl.AllocContiguous(1), PageId{7});
  // allocate(0) == 0 (sentinel)
  EXPECT_EQ(fl.AllocContiguous(0), kSentinelPageId);
  // allocate(0) == 0 (sentinel)
  EXPECT_EQ(fl.AllocContiguous(0), kSentinelPageId);
  // Remaining: [9, 18]
  EXPECT_EQ(fl.RangeSet().ToVector(), MakeIds({9, 18}));

  // allocate(1) == 9
  EXPECT_EQ(fl.AllocContiguous(1), PageId{9});
  // allocate(1) == 18
  EXPECT_EQ(fl.AllocContiguous(1), PageId{18});
  // allocate(1) == 0 (sentinel)
  EXPECT_EQ(fl.AllocContiguous(1), kSentinelPageId);
  // Remaining: []
  EXPECT_EQ(fl.RangeSet().ToVector(), MakeIds({}));
}

// Equivalent of Go TestFreelist_read:
// Ensure that a freelist can deserialize from a freelist page.
TEST_F(FreeListTest, GoRead) {
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(2);

  auto* ids = reinterpret_cast<PageId*>(page->DataPtr());
  ids[0] = PageId{23};
  ids[1] = PageId{50};

  fl.Deserialize(page);
  EXPECT_EQ(fl.RangeSet().ToVector(), MakeIds({23, 50}));
}

// Equivalent of Go TestFreelist_write:
// Ensure that a freelist can serialize into a freelist page.
TEST_F(FreeListTest, GoWrite) {
  constexpr std::size_t kBufSize = 4096;

  // Build freelist with ids [12, 39]
  auto buf = AllocatePage(kBufSize);
  auto* page = AsPage(buf.get());
  page->SetFlags(PageFlag::kFreelist);
  page->SetCount(2);
  auto* data = reinterpret_cast<PageId*>(page->DataPtr());
  data[0] = PageId{12};
  data[1] = PageId{39};
  fl.Deserialize(page);

  // Add pending: pending[100] = [28, 11], pending[101] = [3]
  auto buf2 = AllocatePage(kBufSize);
  auto* p2 = AsPage(buf2.get());
  p2->SetId(PageId{28});
  p2->SetFlags(PageFlag::kLeaf);
  p2->SetOverflow(0);
  fl.Free(100, p2);

  auto buf3 = AllocatePage(kBufSize);
  auto* p3 = AsPage(buf3.get());
  p3->SetId(PageId{11});
  p3->SetFlags(PageFlag::kLeaf);
  p3->SetOverflow(0);
  fl.Free(100, p3);

  auto buf4 = AllocatePage(kBufSize);
  auto* p4 = AsPage(buf4.get());
  p4->SetId(PageId{3});
  p4->SetFlags(PageFlag::kLeaf);
  p4->SetOverflow(0);
  fl.Free(101, p4);

  // Write to page
  auto buf5 = AllocatePage(kBufSize);
  auto* outPage = AsPage(buf5.get());
  outPage->SetFlags(PageFlag::kFreelist);
  fl.Serialize(outPage);

  // Read it back
  FreeList fl2;
  fl2.Deserialize(outPage);

  // All pages should be present and sorted: [3, 11, 12, 28, 39]
  EXPECT_EQ(fl2.RangeSet().ToVector(), MakeIds({3, 11, 12, 28, 39}));
}

}  // namespace boltdb
