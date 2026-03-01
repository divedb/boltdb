#include "boltdb/meta.hh"

#include <type_traits>

namespace boltdb {

Meta::Meta(const std::uint32_t page_size, TransactionId txid)
    : magic_(kMagic),
      version_(kVersion),
      page_size_(page_size),
      flags_(0),
      root_bucket_(BucketHeader{.root_page_id = 3, .sequence = 0}),
      freelist_(2),
      pgid_(4),
      txid_(txid) {
  static_assert(std::is_standard_layout_v<Meta>,
                "Meta must be standard-layout");
  constexpr std::size_t kLen = offsetof(Meta, checksum_);
  checksum_ = fnv1a64(this, kLen);
}

}  // namespace boltdb