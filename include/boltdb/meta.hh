#pragma once

#include <byte>
#include <system_error>

#include "boltdb/types.hh"

namespace boltdb {

constexpr std::uint64_t fnv1a64(const void* data, std::size_t n) noexcept {
  constexpr std::uint64_t kOffset = 14695981039346656037ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;

  auto p = static_cast<const std::uint8_t*>(data);
  std::uint64_t h = kOffset;

  for (std::size_t i = 0; i < n; ++i) {
    h ^= static_cast<std::uint64_t>(p[i]);
    h *= kPrime;
  }

  return h;
}

class Meta {
 public:
  static constexpr std::uint32_t kMagic = 0xED0CDAED;
  static constexpr std::uint32_t kVersion = 2;

  /// \brief Constructs a new Meta object with the given page size and
  /// transaction ID.
  ///
  /// \param page_size The size of pages in the database.
  /// \param txid The transaction ID of the last transaction that modified the
  ///             database.
  Meta(const std::uint32_t page_size, TransactionId txid);

  std::error_code Validate() const noexcept {
    if (magic_ != kMagic) return make_error_code(Errc::kMagicMismatch);
    if (version_ != kVersion) return make_error_code(Errc::kVersionMismatch);
    if (checksum_ != fnv1a64(this, offsetof(Meta, checksum_))) {
      return make_error_code(Errc::kChecksumMismatch);
    }

    return {};
  }

  [[nodiscard]] std::uint32_t PageSize() const noexcept { return page_size_; }

  [[nodiscard]] BucketHeader& RootBucket() noexcept { return root_bucket_; }

  [[nodiscard]] const BucketHeader& RootBucket() const noexcept {
    return root_bucket_;
  }

 private:
  std::uint32_t magic_;
  std::uint32_t version_;
  std::uint32_t page_size_;
  std::uint32_t flags_;

  /// The page ID of the freelist page, which tracks free pages in the database.
  PageId freelist_;

  /// The root bucket header, which contains the root page ID and
  /// sequence number for the root bucket of the database.
  BucketHeader root_bucket_;

  /// The next page ID to allocate. This is used to track the highest page ID
  /// that has been allocated in the database. When a new page is needed, this
  /// value is used as the page ID for the new page, and then it is incremented.
  PageId pgid_;

  /// The transaction ID of the last transaction that modified the database.
  /// This is used to track the state of the database and ensure consistency
  /// across transactions.
  TransactionId txid_;

  /// The checksum of the meta page, used to verify the integrity of the meta
  /// page.
  std::uint64_t checksum_;
};

}  // namespace boltdb