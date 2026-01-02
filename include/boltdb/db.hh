#pragma once

#include <array>
#include <fstream>
#include <mio/mmap.hpp>
#include <mutex>
#include <string>
#include <system_error>

#include "boltdb/expected.hh"
#include "boltdb/options.hh"
#include "boltdb/status.hh"

namespace fs = std::filesystem;

namespace boltdb {

class Meta;

class MmapSizer {
 public:
  /// \brief Default maximum mmap size (256 TB).
  static constexpr size_t kDefaultMaxMmapSize = 0xFFFFFFFFFFFF;

  /// \brief Default maximum mmap step (1 GiB).
  static constexpr size_t kDefaultMaxMmapStep = 1ULL << 30;

  /// \brief Constructs a MmapSizer with specified parameters.
  ///
  /// \param page_size     The size of a memory page.
  /// \param max_mmap_size The maximum allowed mmap size.
  /// \param max_mmap_step The maximum step size for mmap growth.
  MmapSizer(size_t page_size, size_t max_mmap_size = kDefaultMaxMmapSize,
            size_t max_mmap_step = kDefaultMaxMmapStep)
      : max_mmap_size_(max_mmap_size),
        max_mmap_step_(max_mmap_step),
        page_size_(page_size) {}

  /// \brief Calculates the appropriate mmap size based on the requested size.
  ///
  /// \param requested_size The requested size for the mmap.
  /// \return               The calculated mmap size or an error code if the
  ///                       requested size is too large.
  [[nodiscard]] constexpr Expected<size_t, std::error_code> ComputeMmapSize(
      size_t requested_size) const {
    if (requested_size > max_mmap_size_) {
      auto ec = std::make_error_code(
          static_cast<std::errc>(DBErrorCode::kMmapTooLarge));

      return UnExpected{ec};
    }

    auto it = std::lower_bound(kMmapSizeLevels.begin(), kMmapSizeLevels.end(),
                               requested_size);

    if (it != kMmapSizeLevels.end()) [[likely]] {
      return *it;
    }

    // If requested size exceeds predefined levels, align to max_mmap_step_
    // boundary. The default max_mmap_step_ is typically set to 1 GiB.
    size_t new_size = AlignTo(requested_size, max_mmap_step_);

    // Finally, align to page size.
    new_size = AlignTo(new_size, page_size_);

    return std::min(new_size, max_mmap_size_);
  }

 private:
  static constexpr std::array<uint64_t, 16> kMmapSizeLevels = {
      1ULL << 15, 1ULL << 16, 1ULL << 17, 1ULL << 18, 1ULL << 19, 1ULL << 20,
      1ULL << 21, 1ULL << 22, 1ULL << 23, 1ULL << 24, 1ULL << 25, 1ULL << 26,
      1ULL << 27, 1ULL << 28, 1ULL << 29, 1ULL << 30};

  const size_t max_mmap_size_;
  const size_t max_mmap_step_;
  const size_t page_size_;
};

class DB {
 public:
  static std::expected<std::unique_ptr<DB>, std::error_code> Open(
      const std::string& path, const Options& options = {}) {
    auto db = std::make_unique<DB>(fs::path(path), options);
    std::error_code ec;
    db->file_handle_ =
        mio::open_file(db->path_, mio::access_mode::read_write, ec);

    if (ec) return std::unexpected(ec);
  }

 private:
  DB(fs::path path, const Options& options)
      : path_(std::move(path)), options_(options) {}

  ~DB();

  /// \brief Ensures the memory map is at least min_sz bytes.
  ///
  /// \param min_sz The minimum size required for the memory map.
  void Mmap(size_t min_sz) { std::lock_guard<std::mutex> lock(mmap_mutex_); }

  Meta* meta0_ = nullptr;
  Meta* meta1_ = nullptr;
  size_t page_size_ = 0;
  std::mutex mmap_mutex_;
  fs::path path_;
  Options options_;
  mio::file_handle_type file_handle_;
  mio::mmap_sink mmap_;
};

}  // namespace boltdb