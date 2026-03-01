#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "boltdb/error.hh"
#include "boltdb/expected.hh"
#include "boltdb/file_handle.hh"
#include "boltdb/memory_map.hh"
#include "boltdb/options.hh"
#include "boltdb/types.hh"

namespace boltdb {

class Txn;
class Page;

// DB represents a collection of buckets persisted to a file on disk.
// All data access is performed through transactions which can be obtained
// through the DB. All the functions on DB will return a ErrDatabaseNotOpen if
// accessed before Open() is called.
class DB {
 public:
  DB(FileHandle handle, const Options& options)
      : handle_(std::move(handle)), options_(options) {}

  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  DB(DB&& other) noexcept { MoveFrom(std::move(other)); }

  DB& operator=(DB&& other) noexcept {
    if (this != &other) {
      Close();
      MoveFrom(std::move(other));
    }

    return *this;
  }

  ~DB() { Close(); }

  /// \brief GetPage retrieves a page by its PageId.
  ///
  /// \param pgid The PageId of the page to retrieve.
  /// \return     A pointer to the page.
  Page* GetPage(PageId pgid) noexcept {
    assert(IsOpen() && "GetPage called on a closed database");
    assert(Mapping().IsMapped() &&
           "GetPage called on a database without a memory map");

    auto* base = Mapping().MutableData();
    auto* page_ptr = base + (ToUint64(pgid) * options_.PageSize());

    return reinterpret_cast<Page*>(page_ptr);
  }

  [[nodiscard]] bool IsOpen() const noexcept { return opened_; }

  [[nodiscard]] bool IsReadOnly() const noexcept { return options_.ReadOnly(); }

  [[nodiscard]] bool NoGrowSync() const noexcept {
    return options_.NoGrowSync();
  }

  [[nodiscard]] const FileHandle& File() const noexcept { return handle_; }

  [[nodiscard]] FileHandle& File() noexcept { return handle_; }

  [[nodiscard]] const MemoryMap& Mapping() const noexcept { return mapping_; }

  [[nodiscard]] MemoryMap& Mapping() noexcept { return mapping_; }

  std::error_code Remap(std::size_t length = 0,
                        std::uint64_t offset = 0) noexcept {
    if (!handle_.IsOpen()) {
      return make_error_code(Errc::kDatabaseNotOpen);
    }

    auto map = MemoryMap::Map(handle_, !read_only_, offset, length);
    if (!map.HasValue()) {
      return map.Error();
    }

    if (auto unmap_ec = mapping_.Unmap()) {
      return unmap_ec;
    }

    mapping_ = std::move(map.Value());
    return {};
  }

  std::error_code Close() noexcept {
    std::error_code ec;

    if (auto unmap_ec = mapping_.Unmap(); unmap_ec) {
      ec = unmap_ec;
    }

    if (auto close_ec = handle_.Close(); close_ec && !ec) {
      ec = close_ec;
    }

    opened_ = false;
    read_only_ = false;

    return ec;
  }

 private:
  friend Expected<std::unique_ptr<DB>, std::error_code> Open(
      const std::string& path, const Options& options);

  std::error_code Init() noexcept {
    auto size = handle_.Size();

    if (!size.HasValue()) return size.Error();

    if (size.Value() == 0) {
      if (IsReadOnly()) return make_error_code(Errc::kInvalidDatabase);

      return FormatEmptyDatabase();
    } else {
      if (auto err = VerifyMetaPage(); err) return err;
    }

    // TODO(gc):
  }

  std::error_code FormatEmptyDatabase() noexcept {
    std::vector<std::byte> empty_page(options_.PageSize() * 4);
  }

  std::error_code VerifyMetaPage() noexcept;

  void MoveFrom(DB&& other) noexcept {
    handle_ = std::move(other.handle_);
    mapping_ = std::move(other.mapping_);
    opened_ = other.opened_;
    options_ = std::move(other.options_);

    other.opened_ = false;
  }

  FileHandle handle_;
  Options options_;
  MemoryMap mapping_;
  bool opened_{false};
};

inline Expected<std::unique_ptr<DB>, std::error_code> Open(
    std::string_view path, const Options& options) {
  auto handle = FileHandle::Open(path, options);

  if (!handle.HasValue()) return UnExpected(handle.Error());

  auto timeout_ms = options.FileLockTimeout();
  const auto err = options.ReadOnly() ? handle_.LockShared(timeout_ms)
                                      : handle_.LockExclusive(timeout_ms);
  if (err) return UnExpected(err);

  if (auto map_ec = db->Remap(); map_ec) {
    db->Close();
    return UnExpected(map_ec);
  }

  return db;
}

}  // namespace boltdb
