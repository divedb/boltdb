#include "page.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace boltdb {

/// Allocate a page buffer large enough for the header + element data.
auto AllocatePage(std::size_t total_bytes) {
  auto buf = std::make_unique<std::byte[]>(total_bytes);
  std::memset(buf.get(), 0, total_bytes);
  return buf;
}

Page* AsPage(std::byte* buf) { return reinterpret_cast<Page*>(buf); }

class PageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    buf = AllocatePage(4096);
    page = AsPage(buf.get());
  }

  std::unique_ptr<std::byte[]> buf;
  Page* page;
};

// ════════════════════════════════════════════════════════════════════
// Tests
// ════════════════════════════════════════════════════════════════════

TEST_F(PageTest, PageType) {
  page->flags = PageFlag::kBranch;
  EXPECT_TRUE(page->IsBranch());
  EXPECT_EQ(page->TypeName(), "branch");

  page->flags = PageFlag::kLeaf;
  EXPECT_TRUE(page->IsLeaf());
  EXPECT_EQ(page->TypeName(), "leaf");

  page->flags = PageFlag::kMeta;
  EXPECT_TRUE(page->IsMeta());
  EXPECT_EQ(page->TypeName(), "meta");

  page->flags = PageFlag::kFreelist;
  EXPECT_TRUE(page->IsFreelist());
  EXPECT_EQ(page->TypeName(), "freelist");
}

TEST_F(PageTest, LeafElements) {
  // Layout: [Page header][LeafElement 0][LeafElement
  // 1]["hello"]["world"]["key2"]["val2"]
  constexpr std::size_t kBufSize = 4096;
  auto buf = AllocatePage(kBufSize);
  auto* p = AsPage(buf.get());

  p->id = PageId{42};
  p->flags = PageFlag::kLeaf;
  p->count = 2;

  auto elems = p->LeafElements();
  EXPECT_EQ(elems.size(), 2);

  // Place key/value data after the two LeafElement headers.
  std::size_t data_offset = 2 * kLeafElementSize;

  // Element 0: key="hello", value="world"
  const char* key0 = "hello";
  const char* val0 = "world";
  elems[0].flags = LeafFlag::kNone;
  elems[0].pos = static_cast<uint32_t>(data_offset);
  elems[0].ksize = 5;
  elems[0].vsize = 5;
  std::memcpy(p->DataPtr() + data_offset, key0, 5);
  std::memcpy(p->DataPtr() + data_offset + 5, val0, 5);

  data_offset += 10;

  // Element 1: key="key2", value="val2"
  const char* key1 = "key2";
  const char* val1 = "val2";
  elems[1].flags = LeafFlag::kNone;
  elems[1].pos = static_cast<uint32_t>(data_offset - kLeafElementSize);
  // pos is relative to the element itself, not to DataPtr!
  // Actually, let's recalculate. pos is relative to &elems[1].

  // Re-do: pos for element N is relative to the start of element N itself.
  // Element 0 starts at DataPtr() + 0.
  // Element 1 starts at DataPtr() + kLeafElementSize.
  // Key data for elem 0 is at DataPtr() + 2*kLeafElementSize.
  //   => pos for elem 0 = 2 * kLeafElementSize (offset from elem 0 start)
  // Key data for elem 1 is at DataPtr() + 2*kLeafElementSize + 10.
  //   => pos for elem 1 = kLeafElementSize + 10 (offset from elem 1 start)

  // Let's redo both correctly.
  std::size_t elem0_start = 0;
  std::size_t elem1_start = kLeafElementSize;
  std::size_t kv_start = 2 * kLeafElementSize;

  // Element 0
  elems[0].pos = static_cast<uint32_t>(kv_start - elem0_start);
  std::memcpy(p->DataPtr() + kv_start, key0, 5);
  std::memcpy(p->DataPtr() + kv_start + 5, val0, 5);

  // Element 1
  std::size_t kv1_start = kv_start + 10;
  elems[1].pos = static_cast<uint32_t>(kv1_start - elem1_start);
  elems[1].ksize = 4;
  elems[1].vsize = 4;
  std::memcpy(p->DataPtr() + kv1_start, key1, 4);
  std::memcpy(p->DataPtr() + kv1_start + 4, val1, 4);

  // Verify element 0.
  EXPECT_EQ(elems[0].KeyStr(), "hello");
  EXPECT_EQ(elems[0].ValueStr(), "world");
  EXPECT_FALSE(elems[0].IsBucket());

  // Verify element 1.
  EXPECT_EQ(elems[1].KeyStr(), "key2");
  EXPECT_EQ(elems[1].ValueStr(), "val2");
}

TEST_F(PageTest, BranchElements) {
  page->flags = PageFlag::kBranch;
  page->count = 1;

  auto elems = page->BranchElements();
  EXPECT_EQ(elems.size(), 1);

  const char* key = "mykey";
  std::size_t key_offset = kBranchElementSize;  // right after the element
  elems[0].pos = static_cast<uint32_t>(key_offset);
  elems[0].ksize = 5;
  elems[0].pgid = PageId{99};
  std::memcpy(page->DataPtr() + key_offset, key, 5);

  EXPECT_EQ(elems[0].KeyStr(), "mykey");
  EXPECT_EQ(elems[0].pgid, PageId{99});
}

TEST(PageIdTest, MergePageIds) {
  PageIds a = {PageId{1}, PageId{3}, PageId{5}, PageId{7}};
  PageIds b = {PageId{2}, PageId{4}, PageId{6}};

  auto merged = MergePageIds(a, b);
  EXPECT_EQ(merged.size(), 7);
  for (std::size_t i = 0; i < merged.size(); ++i) {
    EXPECT_EQ(merged[i], PageId{i + 1});
  }

  // In-place variant
  PageIds dst(7);
  MergePageIds(std::span{dst}, std::span<const PageId>{a},
               std::span<const PageId>{b});
  EXPECT_EQ(dst, merged);

  // Edge cases
  EXPECT_EQ(MergePageIds({}, b), b);
  EXPECT_EQ(MergePageIds(a, {}), a);
  EXPECT_TRUE(MergePageIds({}, {}).empty());
}

TEST_F(PageTest, EmptyPage) {
  page->flags = PageFlag::kLeaf;
  page->count = 0;
  EXPECT_TRUE(page->LeafElements().empty());

  page->flags = PageFlag::kBranch;
  EXPECT_TRUE(page->BranchElements().empty());
}

TEST_F(PageTest, HexDump) {
  page->id = PageId{1};
  page->flags = PageFlag::kLeaf;
  page->count = 0;

  // Just verify it doesn't crash
  testing::internal::CaptureStdout();
  page->HexDump(32);
  std::string output = testing::internal::GetCapturedStdout();
  EXPECT_FALSE(output.empty());
}

}  // namespace boltdb