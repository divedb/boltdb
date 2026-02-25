#pragma once

#include "boltdb/type.hh"

namespace boltdb {

/// \brief PageFlag represents the type of a page in BoltDB. It is used to
///        determine the structure and behavior of the page.
enum class PageFlag : uint16_t {
  kBranchPageFlag = 0x01,
  kLeafPageFlag = 0x02,
  kMetaPageFlag = 0x04,
  kFreelistPageFlag = 0x10,
};

// ┌───────────────────────────────────────────────────────────────┐
// │                        Page Header (~32 bytes)                │
// │  flags: branch (0x01)   n: 4   overflow: 0   pgid: 42         │
// │  checksum: ...                                                │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │  branchPageElement[0]    (12 bytes)                           │
// │    pos:   3800   ksize:  8   pgid: 100                        │
// │                                                               │
// │  branchPageElement[1]    (12 bytes)                           │
// │    pos:   3780   ksize: 12   pgid:  85                        │
// │                                                               │
// │  branchPageElement[2]    (12 bytes)                           │
// │    pos:   3750   ksize:  7   pgid:  60                        │
// │                                                               │
// │  branchPageElement[3]    (12 bytes)                           │
// │    pos:   3720   ksize:  9   pgid:  45                        │
// │                                                               │
// │  ... (possible padding / alignment bytes)                     │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │                         free space                            │
// │                     (can be used for new elements             │
// │                      and new keys when inserting)             │
// │                                                               │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │   keys are stored backwards from the end of the page          │
// │                                                               │
// │   "user:zzzz\0"           <- pos = 3800, length = 8           │
// │   "user:wwww extra"       <- pos = 3780, length = 12          │
// │   "user:tttt"             <- pos = 3750, length = 7           │
// │   "user:mmmm123"          <- pos = 3720, length = 9           │
// │                                                               │
// └───────────────────────────────────────────────────────────────┘
struct BranchPageElement {
  PageID child_page_id;  ///< ID of the child page.
  uint32_t pos;          ///< Position of the key.
  uint32_t ksize;        ///< Size of the key in bytes.
};

/// \brief Page represents a physical page in BoltDB. It contains metadata about
///        the page, such as its type, the number of elements it contains, and
///        its ID.
class Page {
 public:
  /// \brief Returns a string representation of the page type based on its flag.
  ///
  /// \return A string representing the page type ("branch", "leaf", "meta",
  ///         "freelist", or "unknown").
  const char *Type() const noexcept {
    switch (flag_) {
      case PageFlag::kBranchPageFlag:
        return "branch";

      case PageFlag::kLeafPageFlag:
        return "leaf";

      case PageFlag::kMetaPageFlag:
        return "meta";

      case PageFlag::kFreelistPageFlag:
        return "freelist";

      default:
        return "unknown";
    }
  }

 private:
  PageFlag flag_;      ///< Page type.
  uint16_t count_;     ///< Number of elements on this page.
  uint32_t overflow_;  ///< Number of contiguous overflow pages.
  PageID id_;  ///< Page ID. This is the page's offset in the file divided by
               ///< the page size.
};

}  // namespace boltdb