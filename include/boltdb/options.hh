#pragma once

#include <chrono>
#include <cstdint>

#include "boltdb/util.hh"

using namespace std::literals;

namespace boltdb {

class Options {
 public:
  Options() : page_size_(GetPageSize()) {}

  /// \brief Set the timeout for acquiring a file lock. When set to zero, it
  ///        will wait indefinitely. This option is only available on Darwin and
  ///        Linux.
  ///
  /// \param timeout The timeout duration for acquiring a file lock.
  /// \return        A reference to the current Options object.
  Options& WithFileLockTimeout(std::chrono::milliseconds timeout) noexcept {
    file_lock_timeout_ms_ = timeout;

    return *this;
  }

  /// \brief Set the initial size of the memory-mapped region. This option is
  ///        used to control the initial size of the mmap when opening a
  ///        database. If the database file grows beyond this size, the mmap
  ///        will be remapped to accommodate the new size. Setting this to a
  ///        larger value can reduce the number of remaps for larger databases,
  ///        but it may also consume more virtual memory upfront.
  ///
  /// \param size The initial size of the memory-mapped region in bytes.
  /// \return     A reference to the current Options object.
  Options& WithInitialMmapSize(int size) noexcept {
    initial_mmap_size_ = size;

    return *this;
  }

  /// \brief Set whether to disable synchronous file growth. When this option is
  ///        enabled, the database file will grow asynchronously when it needs
  ///        to expand. This can improve performance in some cases, but it also
  ///        means that the file size on disk may not reflect the actual size of
  ///        the database until the growth operation completes. Use this option
  ///        with caution, as it may lead to data loss if the application
  ///        crashes before the file growth completes.
  ///
  /// \param no_grow_sync If true, disable synchronous file growth. If false,
  ///                     enable synchronous file growth. The default value is
  ///                     false (synchronous growth enabled).
  /// \return             A reference to the current Options object.
  Options& WithNoGrowSync(bool no_grow_sync) noexcept {
    no_grow_sync_ = no_grow_sync;

    return *this;
  }

  /// \brief Set whether to open the database in read-only mode. When this
  /// option
  ///        is enabled, all transactions will be read-only and any attempt to
  ///        perform a write operation will result in an error. This can be
  ///        useful for applications that only need to read from the database
  ///        and want to ensure that no accidental writes occur.
  ///
  /// \param read_only If true, open the database in read-only mode. If false,
  ///                  open the database in read-write mode. The default value
  ///                  is false (read-write mode).
  /// \return          A reference to the current Options object.
  Options& WithReadOnly(bool read_only) noexcept {
    read_only_ = read_only;

    return *this;
  }

  /// \brief Set the file mode for the database file. This option is only
  ///        applicable when creating a new database file.
  ///
  /// \param mode The file mode to use when creating the database file.
  /// \return     A reference to the current Options object.
  Options& WithMode(int mode) noexcept {
    mode_ = mode;

    return *this;
  }

  /// \brief Set whether to create the database file if it does not exist. If
  ///        this option is enabled and the specified database file does not
  ///        exist, a new file will be created with the specified mode. If this
  ///        option is disabled and the specified database file does
  ///        not exist, an error will be returned when attempting to open the
  ///        database.
  ///
  /// \param created_if_not_exists If true, create the database file if it does
  ///                              not exist. If false, do not create the
  ///                              database file if it does not exist. The
  ///                              default value is true.
  /// \return                      A reference to the current Options object.
  Options& WithCreatedIfNotExists(bool created_if_not_exists) noexcept {
    created_if_not_exists_ = created_if_not_exists;
    return *this;
  }

  Options& WithPageSize(int page_size) noexcept {
    page_size_ = page_size;

    return *this;
  }

  Options& WithMaxBatchSize(int max_batch_size) noexcept {
    max_batch_size_ = max_batch_size;

    return *this;
  }

  Options& WithMaxBatchDelay(
      std::chrono::milliseconds max_batch_delay) noexcept {
    max_batch_delay_ms_ = max_batch_delay;

    return *this;
  }

  Options& WithPreallocateSize(int preallocate_size) noexcept {
    preallocate_size_ = preallocate_size;

    return *this;
  }

  [[nodiscard]] std::chrono::milliseconds FileLockTimeout() const noexcept {
    return file_lock_timeout_ms_;
  }

  [[nodiscard]] int MaxBatchSize() const noexcept { return max_batch_size_; }

  [[nodiscard]] std::chrono::milliseconds MaxBatchDelay() const noexcept {
    return max_batch_delay_ms_;
  }

  [[nodiscard]] int PreallocateSize() const noexcept {
    return preallocate_size_;
  }

  [[nodiscard]] int PageSize() const noexcept { return page_size_; }

  [[nodiscard]] int InitialMmapSize() const noexcept {
    return initial_mmap_size_;
  }

  [[nodiscard]] int Mode() const noexcept { return mode_; }

  [[nodiscard]] bool CreatedIfNotExists() const noexcept {
    return created_if_not_exists_;
  }

  [[nodiscard]] bool NoGrowSync() const noexcept { return no_grow_sync_; }

  [[nodiscard]] bool ReadOnly() const noexcept { return read_only_; }

 private:
  std::chrono::milliseconds file_lock_timeout_ms_{0};

  /// The maximum delay for batching write operations. When a batch of write
  /// operations is being accumulated, if the time since the first operation in
  /// the batch exceeds this duration, the batch will be flushed to disk.
  ///
  /// \note If <= 0, batching is disabled and all write operations will be
  ///       flushed immediately.
  std::chrono::milliseconds max_batch_delay_ms_{10ms};

  /// The maximum size of a batch of write operations. When a batch of write
  /// operations reaches this size, it will be flushed to disk. Setting this to
  /// a larger value can improve performance by reducing the number of disk
  /// writes, but it also means that more data may be lost in the event of a
  /// crash before the batch is flushed.
  ///
  /// \note If <= 0, batching is disabled and all write operations will be
  ///       flushed immediately.
  int max_batch_size_{1000};

  /// The amount of space to preallocate when growing the database file. When
  /// the database file needs to grow, it will be expanded by at least this
  /// amount of bytes. This is done to amortize the cost of truncate() and
  /// fsync() calls when growing the file.
  int preallocate_size_{16 * 1024 * 1024};

  int page_size_;

  int initial_mmap_size_{0};
  int mode_{0600};
  bool created_if_not_exists_{true};
  bool no_grow_sync_{false};
  bool read_only_{false};
};

}  // namespace boltdb
