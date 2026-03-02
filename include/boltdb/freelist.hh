#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "boltdb/types.hh"

namespace boltdb {

class Page;  // forward declaration

/// \brief A sorted, non-overlapping set of [start, end] (inclusive) page-id
///        ranges.  Supports O(log k) insert/remove/lookup and O(k) contiguous
///        allocation, where k is the number of disjoint ranges (typically much
///        smaller than the total number of free pages).
class PageRangeSet {
 public:
  /// Number of individual page ids stored across all ranges.
  int Size() const noexcept { return size_; }

  bool IsEmpty() const noexcept { return ranges_.empty(); }

  /// \brief Insert a single page id, merging with adjacent ranges.
  void Add(PageId id) noexcept;

  /// \brief Insert a contiguous range [lo, hi] (inclusive).
  ///
  /// \param lo The starting page id of the range.
  /// \param hi The ending page id of the range (inclusive).
  void AddRange(PageId lo, PageId hi) noexcept;

  /// \brief Remove a single page id, splitting a range if necessary.
  ///
  /// \param id The page id to remove from the set.
  void Remove(PageId id) noexcept;

  /// \brief Check whether a page id is contained in any range.
  ///
  /// \param id The page id to check.
  /// \return   True if the page id is contained in any range; otherwise false.
  bool Contains(PageId id) const noexcept;

  /// \brief Find and remove the first contiguous run of `n` pages.
  ///        Returns the first page id of the run, or kSentinelPageId if none.
  PageId AllocContiguous(int n) noexcept;

  /// \brief Expand all ranges into a sorted vector of individual page ids.
  std::vector<PageId> ToVector() const noexcept;

  /// \brief Build the range set from a sorted vector of page ids.
  void FromVector(const std::vector<PageId>& ids) noexcept;

  /// \brief Remove all ranges.
  void Clear() noexcept;

  /// Expose the underlying map for iteration (read-only).
  const std::map<PageId, PageId>& Ranges() const noexcept { return ranges_; }

 private:
  /// Sorted map: start -> end (inclusive).  Invariant: ranges never overlap or
  /// touch; adjacent ranges are always merged.
  std::map<PageId, PageId> ranges_;

  /// Total number of individual page ids across all ranges.
  int size_ = 0;
};

class FreeList {
 public:
  /// Total number of free (available) page ids.
  int Size() const noexcept { return ids_.Size(); }

  /// Total number of free + pending page ids.
  int Count() const noexcept;

  /// \brief Returns the number of pending page ids that have been freed but not
  ///        yet released.
  ///
  /// \return The number of pending page ids.
  int PendingCount() const noexcept {
    return std::accumulate(
        pending_.begin(), pending_.end(), 0,
        [](int sum, const auto& pair) { return sum + pair.second.size(); });
  }

  /// \brief Get a sorted vector of all free page ids, including both available
  ///        and pending ones.
  ///
  /// \return A sorted vector of all free page ids.
  std::vector<PageId> SortedMergedFreePages() const noexcept;

  /// \brief Allocate a contiguous run of `n` pages from the free list.
  ///
  /// \return The first page id of the run, or kSentinelPageId on failure.
  PageId AllocContiguous(int n) noexcept;

  /// \brief Free a page and add it to the pending list for a transaction.
  ///
  /// \param txid The transaction id.
  /// \param page The page to free.
  /// \note The difference between Free and Release is that Free adds the page
  ///       to the pending list (not yet available for allocation), while
  ///       Release moves all pending pages to the available free list (after
  ///       the transaction commits).
  void Free(TransactionId txid, Page* page) noexcept;

  /// \brief Move all pending pages for a transaction to the available free
  /// list.
  ///
  /// \param txid The transaction id.
  void Release(TransactionId txid) noexcept;

  /// \brief Rollback all pending pages for a transaction.
  ///
  /// \param txid The transaction id.
  void Rollback(TransactionId txid) noexcept;

  /// \brief Check whether a page id is freed (either available or pending).
  ///
  /// \param id The page id to check.
  /// \return   True if the page id is freed; otherwise false.
  bool IsFreed(PageId id) const noexcept;

  /// \brief Deserialize the freelist from a page.
  ///
  /// \param page The page to deserialize from.
  void Deserialize(const Page* page) noexcept;

  /// \brief Serialize the freelist to a page.
  ///
  /// \param page The page to serialize to.
  void Serialize(Page* page) const noexcept;
  void Reload(Page* page) noexcept;

  const PageRangeSet& RangeSet() const noexcept { return ids_; }

 private:
  /// All free and available page ids, stored as merged ranges.
  PageRangeSet ids_;

  /// All pending page ids that have been freed but not yet released.
  /// Ordered by txid so Release() can erase [begin, upper_bound) in one pass.
  std::map<TransactionId, std::vector<PageId>> pending_;
};

}  // namespace boltdb