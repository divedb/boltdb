#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#include "boltdb/types.hh"

namespace boltdb {

enum class LeafFlag : std::uint32_t {
  kNone = 0x00,
  kBucket = 0x01,
};

/// A branch page element stores a key and a child page pointer.
///
/// Layout in memory:
///   [BranchElement header] ... [key bytes at offset `pos`]
///
/// `pos` is relative to the start of this element struct, not the page.
struct BranchElement {
  std::uint32_t pos;    ///< Byte offset from this struct to key data.
  std::uint32_t ksize;  ///< Key length in bytes.
  PageId pgid;          ///< Child page id.

  /// Access the key bytes. The caller must ensure this element lives
  /// within a properly allocated page.
  [[nodiscard]] std::span<const std::byte> Key() const {
    const auto* base = reinterpret_cast<const std::byte*>(this);

    return {base + pos, ksize};
  }

  [[nodiscard]] std::string_view KeyStr() const {
    const auto* base = reinterpret_cast<const char*>(this);

    return {base + pos, ksize};
  }
};

/// A leaf page element stores a key-value pair (or a sub-bucket reference).
///
/// Layout in memory:
///   [LeafElement header] ... [key bytes | value bytes at offset `pos`]
struct LeafElement {
  LeafFlag flags;
  std::uint32_t pos;    ///< Byte offset from this struct to key data.
  std::uint32_t ksize;  ///< Key length in bytes.
  std::uint32_t vsize;  ///< Value length in bytes.

  [[nodiscard]] bool IsBucket() const { return flags == LeafFlag::kBucket; }

  [[nodiscard]] std::span<const std::byte> Key() const {
    const auto* base = reinterpret_cast<const std::byte*>(this);

    return {base + pos, ksize};
  }

  [[nodiscard]] std::span<const std::byte> Value() const {
    const auto* base = reinterpret_cast<const std::byte*>(this);

    return {base + pos + ksize, vsize};
  }

  [[nodiscard]] std::string_view KeyStr() const {
    const auto* base = reinterpret_cast<const char*>(this);

    return {base + pos, ksize};
  }

  [[nodiscard]] std::string_view ValueStr() const {
    const auto* base = reinterpret_cast<const char*>(this);

    return {base + pos + ksize, vsize};
  }
};

static_assert(std::is_trivially_copyable_v<BranchElement>);
static_assert(std::is_trivially_copyable_v<LeafElement>);

inline constexpr std::size_t kBranchElementSize = sizeof(BranchElement);
inline constexpr std::size_t kLeafElementSize = sizeof(LeafElement);
inline constexpr std::size_t kMinKeysPerPage = 2;

class Meta;

/// On-disk page header.  The actual element data lives immediately after
/// this struct in the same allocation (the flexible array member pattern).
///
/// ┌─────────────────────────────────────────────────────────────┐
/// │  Page Header  │  Element[0]  Element[1]  ...  │  keys/vals  │
/// └─────────────────────────────────────────────────────────────┘
///                 | DataPtr()
class Page {
 public:
  Page(PageId id, PageFlag flags)
      : id_(id), flags_(flags), count_(0), overflow_(0) {}

  PageId Id() const noexcept { return id_; }
  PageFlag Flags() const noexcept { return flags_; }
  std::uint16_t Count() const noexcept { return count_; }
  std::uint32_t Overflow() const noexcept { return overflow_; }

  void SetId(PageId id) noexcept { id_ = id; }
  void SetFlags(PageFlag flags) noexcept { flags_ = flags; }
  void SetCount(std::uint16_t count) noexcept { count_ = count; }
  void SetOverflow(std::uint32_t overflow) noexcept { overflow_ = overflow; }

  [[nodiscard]] bool IsBranch() const noexcept {
    return flags_ & PageFlag::kBranch;
  }
  [[nodiscard]] bool IsLeaf() const noexcept {
    return flags_ & PageFlag::kLeaf;
  }
  [[nodiscard]] bool IsMeta() const noexcept {
    return flags_ & PageFlag::kMeta;
  }
  [[nodiscard]] bool IsFreelist() const noexcept {
    return flags_ & PageFlag::kFreelist;
  }

  [[nodiscard]] std::string TypeName() const {
    auto sv = PageFlagToString(flags_);
    if (sv != "unknown") return std::string(sv);

    return std::format("unknown<{:02x}>", static_cast<uint16_t>(flags_));
  }

  /// Pointer to the first byte after the page header.
  /// All element data starts here.
  [[nodiscard]] std::byte* DataPtr() {
    return reinterpret_cast<std::byte*>(this) + kHeaderSize;
  }

  [[nodiscard]] const std::byte* DataPtr() const {
    return reinterpret_cast<const std::byte*>(this) + kHeaderSize;
  }

  [[nodiscard]] Meta* GetMeta() {
    assert(IsMeta());

    return reinterpret_cast<Meta*>(DataPtr());
  }

  [[nodiscard]] const Meta* GetMeta() const {
    assert(IsMeta());

    return reinterpret_cast<const Meta*>(DataPtr());
  }

  [[nodiscard]] BranchElement& GetBranchElement(std::uint16_t index) {
    assert(IsBranch() && index < count_);

    auto* elems = reinterpret_cast<BranchElement*>(DataPtr());

    return elems[index];
  }

  [[nodiscard]] const BranchElement& GetBranchElement(
      std::uint16_t index) const {
    assert(IsBranch() && index < count_);

    auto* elems = reinterpret_cast<const BranchElement*>(DataPtr());

    return elems[index];
  }

  [[nodiscard]] std::span<BranchElement> BranchElements() {
    if (count_ == 0) return {};

    assert(IsBranch());

    return {reinterpret_cast<BranchElement*>(DataPtr()), count_};
  }

  [[nodiscard]] std::span<const BranchElement> BranchElements() const {
    if (count_ == 0) return {};
    assert(IsBranch());
    return {reinterpret_cast<const BranchElement*>(DataPtr()), count_};
  }

  [[nodiscard]] LeafElement& GetLeafElement(std::uint16_t index) {
    assert(IsLeaf() && index < count_);

    auto* elems = reinterpret_cast<LeafElement*>(DataPtr());

    return elems[index];
  }

  [[nodiscard]] const LeafElement& GetLeafElement(std::uint16_t index) const {
    assert(IsLeaf() && index < count_);

    auto* elems = reinterpret_cast<const LeafElement*>(DataPtr());

    return elems[index];
  }

  [[nodiscard]] std::span<LeafElement> LeafElements() {
    if (count_ == 0) return {};

    assert(IsLeaf());

    return {reinterpret_cast<LeafElement*>(DataPtr()), count_};
  }

  [[nodiscard]] std::span<const LeafElement> LeafElements() const {
    if (count_ == 0) return {};

    assert(IsLeaf());

    return {reinterpret_cast<const LeafElement*>(DataPtr()), count_};
  }

  /// Dump the first `n` bytes of this page to stderr as hex.
  void HexDump(std::size_t n) const {
    const auto* buf = reinterpret_cast<const unsigned char*>(this);

    for (std::size_t i = 0; i < n; ++i) {
      std::cerr << std::format("{:02x}", buf[i]);
    }

    std::cerr << '\n';
  }

  /// Size of the page header (everything before the element data).
  /// Computed manually: id(8) + flags(2) + count(2) + overflow(4) = 16
  static constexpr std::size_t kHeaderSize = sizeof(PageId) + sizeof(PageFlag) +
                                             sizeof(std::uint16_t) +
                                             sizeof(std::uint32_t);

 private:
  PageId id_;
  std::uint32_t overflow_;
  PageFlag flags_;
  std::uint16_t count_;
};

static_assert(std::is_trivially_copyable_v<Page>);
static_assert(sizeof(Page) == 16);

// ====================================================================
// PageInfo (human-readable diagnostic)
// ====================================================================

struct PageInfo {
  int id;
  std::string type;
  int count;
  int overflow_count;
};

// ====================================================================
// PageId vector utilities
// ====================================================================

using PageIds = std::vector<PageId>;

/// Merge two sorted PageId vectors into a sorted union.
///
/// Unlike the Go version which uses sort.Search in a loop, we use
/// std::merge which is O(n+m) for already-sorted inputs.
inline PageIds MergePageIds(const PageIds& a, const PageIds& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;

  assert(std::is_sorted(a.begin(), a.end()));
  assert(std::is_sorted(b.begin(), b.end()));

  PageIds dst;
  dst.reserve(a.size() + b.size());
  std::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(dst));

  return dst;
}

/// Merge two sorted PageId spans into `dst`.
/// `dst` must have capacity ≥ a.size() + b.size().
///
/// This is the in-place variant matching Go's `mergepgids(dst, a, b)`.
inline void MergePageIds(std::span<PageId> dst, std::span<const PageId> a,
                         std::span<const PageId> b) {
  assert(dst.size() >= a.size() + b.size());
  assert(std::is_sorted(a.begin(), a.end()));
  assert(std::is_sorted(b.begin(), b.end()));

  std::merge(a.begin(), a.end(), b.begin(), b.end(), dst.begin());
}

}  // namespace boltdb