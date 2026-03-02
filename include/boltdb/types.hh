#pragma once

#include <cstdint>
#include <limits>

namespace boltdb {

enum class PageId : std::uint64_t {};

constexpr PageId kSentinelPageId =
    PageId{std::numeric_limits<std::uint64_t>::max()};

constexpr auto operator<=>(PageId lhs, PageId rhs) {
  return static_cast<std::uint64_t>(lhs) <=> static_cast<std::uint64_t>(rhs);
}

constexpr std::uint64_t ToUint64(PageId id) {
  return static_cast<std::uint64_t>(id);
}

/// \brief PageFlag represents the type of a page in BoltDB. It is used to
///        determine the structure and behavior of the page.
enum class PageFlag : std::uint16_t {
  kBranch = 0x01,
  kLeaf = 0x02,
  kMeta = 0x04,
  kFreelist = 0x10,
};

constexpr bool operator&(PageFlag a, PageFlag b) {
  return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}

constexpr std::string_view PageFlagToString(PageFlag flags) {
  if (flags & PageFlag::kBranch) return "branch";
  if (flags & PageFlag::kLeaf) return "leaf";
  if (flags & PageFlag::kMeta) return "meta";
  if (flags & PageFlag::kFreelist) return "freelist";

  return "unknown";
}

using TransactionId = std::uint64_t;

/// \brief BucketHeader represents the header of a bucket in BoltDB. It contains
///        the root page ID and a sequence number for the bucket.
struct BucketHeader {
  PageId root_page_id = kSentinelPageId;
  uint64_t sequence = 0;
};

}  // namespace boltdb