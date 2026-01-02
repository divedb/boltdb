#pragma once

#include <chrono>

using namespace std::literals::chrono_literals;

namespace boltdb {

struct Options {
  /// Timeout is the amount of time to wait to obtain a file lock.
  /// When set to zero it will wait indefinitely.
  /// This option is only available on Darwin and Linux.
  std::chrono::milliseconds lock_timeout_ms{};

  /// Disable growing the memory map with sync (DB.NoGrowSync equivalent).
  /// When true, avoids fsync() during file growth for better performance
  /// at the cost of potential data loss on power failure.
  bool disable_grow_sync{};

  /// Open the database in read-only mode.
  bool read_only{};

  /// Sets the initial size (in meta bytes) of the memory-mapped region when the
  /// database is opened.
  ///
  /// Why it matters:
  ///  If this value is large enough to cover the entire database file (current
  ///  size + expected growth), read transactions will never block write
  ///  transactions due to memory map resizing. (See DB.Begin() for details on
  ///  the read/write transaction isolation model.)
  int initial_mmap_size_mb{};

  /// Sets the maximum batch size for batch writes.
  ///
  /// If <=0, disables batching.
  const int max_batch_size = 1000;

  /// Sets the maximum delay before a batch starts.
  ///
  /// If <=0, effectively disables batching.
  const std::chrono::milliseconds max_batch_delay_ms = 10ms;

  /// AllocSize is the amount of space allocated when the database
  /// needs to create new pages. This is done to amortize the cost
  /// of truncate() and fsync() when growing the data file.
  const int alloc_size = 1 << 24;  // 16 MiB
};

}  // namespace boltdb