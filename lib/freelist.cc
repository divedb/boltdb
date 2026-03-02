#include "boltdb/freelist.hh"

#include "boltdb/page.hh"

namespace boltdb {

// ============================================================
// PageRangeSet
// ============================================================

static PageId NextPageId(PageId id) { return PageId{ToUint64(id) + 1}; }

static PageId PrevPageId(PageId id) { return PageId{ToUint64(id) - 1}; }

static int RangeLen(PageId lo, PageId hi) {
  return static_cast<int>(ToUint64(hi) - ToUint64(lo) + 1);
}

void PageRangeSet::Add(PageId id) noexcept { AddRange(id, id); }

void PageRangeSet::AddRange(PageId lo, PageId hi) noexcept {
  assert(lo <= hi);

  // Find the first range whose end >= lo - 1 (could merge on the left).
  // We iterate from the range just before `lo` to see if we can extend.
  auto it = ranges_.upper_bound(lo);

  // Check the previous range — it might contain or adjoin lo.
  if (it != ranges_.begin()) {
    auto prev = std::prev(it);

    if (prev->second >= PrevPageId(lo)) {
      // prev range overlaps or adjoins [lo, hi] on the left — extend.
      lo = std::min(lo, prev->first);
      hi = std::max(hi, prev->second);
      size_ -= RangeLen(prev->first, prev->second);
      it = ranges_.erase(prev);
    }
  }

  // Absorb any subsequent ranges that overlap or adjoin [lo, hi].
  while (it != ranges_.end() && it->first <= NextPageId(hi)) {
    hi = std::max(hi, it->second);
    size_ -= RangeLen(it->first, it->second);
    it = ranges_.erase(it);
  }

  ranges_[lo] = hi;
  size_ += RangeLen(lo, hi);
}

void PageRangeSet::Remove(PageId id) noexcept {
  // Find the range containing `id`.
  auto it = ranges_.upper_bound(id);
  if (it == ranges_.begin()) return;  // no range can contain id
  --it;

  if (it->second < id) return;  // id is not in this range

  PageId lo = it->first;
  PageId hi = it->second;
  size_ -= RangeLen(lo, hi);
  ranges_.erase(it);

  // Re-insert the left part [lo, id-1] if non-empty.
  if (lo < id) {
    ranges_[lo] = PrevPageId(id);
    size_ += RangeLen(lo, PrevPageId(id));
  }

  // Re-insert the right part [id+1, hi] if non-empty.
  if (id < hi) {
    ranges_[NextPageId(id)] = hi;
    size_ += RangeLen(NextPageId(id), hi);
  }
}

bool PageRangeSet::Contains(PageId id) const noexcept {
  auto it = ranges_.upper_bound(id);

  if (it == ranges_.begin()) return false;

  --it;

  return it->second >= id;
}

PageId PageRangeSet::AllocContiguous(int n) noexcept {
  if (n <= 0) return kSentinelPageId;

  for (auto it = ranges_.begin(); it != ranges_.end(); ++it) {
    int len = RangeLen(it->first, it->second);

    if (len >= n) {
      PageId start = it->first;
      PageId alloc_end = PageId{ToUint64(start) + static_cast<uint64_t>(n) - 1};

      size_ -= len;

      if (len == n) {
        ranges_.erase(it);
      } else {
        // Shrink: keep [alloc_end+1, hi]
        PageId new_lo = NextPageId(alloc_end);
        PageId hi = it->second;
        ranges_.erase(it);
        ranges_[new_lo] = hi;
        size_ += RangeLen(new_lo, hi);
      }

      return start;
    }
  }

  return kSentinelPageId;
}

std::vector<PageId> PageRangeSet::ToVector() const noexcept {
  std::vector<PageId> result;
  result.reserve(size_);
  for (const auto& [lo, hi] : ranges_) {
    for (uint64_t id = ToUint64(lo); id <= ToUint64(hi); ++id) {
      result.push_back(PageId{id});
    }
  }
  return result;
}

void PageRangeSet::FromVector(const std::vector<PageId>& ids) noexcept {
  Clear();

  if (ids.empty()) return;

  // Assumes `ids` is sorted.
  PageId lo = ids[0];
  PageId hi = ids[0];

  for (size_t i = 1; i < ids.size(); ++i) {
    if (ids[i] == NextPageId(hi)) {
      hi = ids[i];
    } else {
      ranges_[lo] = hi;
      size_ += RangeLen(lo, hi);
      lo = ids[i];
      hi = ids[i];
    }
  }

  ranges_[lo] = hi;
  size_ += RangeLen(lo, hi);
}

void PageRangeSet::Clear() noexcept {
  ranges_.clear();
  size_ = 0;
}

// ============================================================
// FreeList
// ============================================================

int FreeList::Count() const noexcept { return Size() + PendingCount(); }

std::vector<PageId> FreeList::SortedMergedFreePages() const noexcept {
  std::vector<PageId> pending_ids;
  pending_ids.reserve(PendingCount());

  for (const auto& [_, ids] : pending_) {
    pending_ids.insert(pending_ids.end(), ids.begin(), ids.end());
  }

  std::sort(pending_ids.begin(), pending_ids.end());

  return MergePageIds(ids_.ToVector(), pending_ids);
}

PageId FreeList::AllocContiguous(int n) noexcept {
  return ids_.AllocContiguous(n);
}

void FreeList::Free(TransactionId txid, Page* page) noexcept {
  if (ToUint64(page->Id()) <= 1) return;

  auto& txn_pending = pending_[txid];
  uint64_t base = ToUint64(page->Id());
  uint32_t overflow = page->Overflow();

  for (uint64_t id = base; id <= base + overflow; ++id) {
    txn_pending.push_back(PageId{id});
  }
}

void FreeList::Release(TransactionId txid) noexcept {
  auto end = pending_.upper_bound(txid);

  for (auto it = pending_.begin(); it != end; ++it) {
    for (PageId id : it->second) ids_.Add(id);
  }

  pending_.erase(pending_.begin(), end);
}

void FreeList::Rollback(TransactionId txid) noexcept { pending_.erase(txid); }

bool FreeList::IsFreed(PageId id) const noexcept {
  if (ids_.Contains(id)) return true;

  // Check pending pages. This is O(n^2) in the worst case, but typically there
  // are few pending pages per transaction, and transactions are expected to be
  // short-lived, so this should be efficient enough in practice.
  for (const auto& [_, ids] : pending_) {
    for (PageId pid : ids) {
      if (pid == id) return true;
    }
  }

  return false;
}

void FreeList::Deserialize(const Page* page) noexcept {
  assert(page != nullptr);

  std::uint16_t count = page->Count();
  const PageId* ids = reinterpret_cast<const PageId*>(page->DataPtr());

  // If the page.count is at the max uint16 value (64k) then it's considered
  // an overflow and the size of the freelist is stored as the first element.
  if (count == std::numeric_limits<std::uint16_t>::max()) {
    count = ToUint64(*ids);
    ids++;
  }

  if (count == 0) {
    ids_.Clear();
    return;
  } else {
    std::vector<PageId> id_vec(ids, ids + count);
    std::sort(id_vec.begin(), id_vec.end());
    ids_.FromVector(id_vec);
  }
}

void FreeList::Serialize(Page* page) const noexcept {
  assert(page != nullptr);

  page->SetFlags(PageFlag::kFreelist);
  std::vector<PageId> all = SortedMergedFreePages();
  std::size_t count = all.size();
  PageId* dst = reinterpret_cast<PageId*>(page->DataPtr());

  if (count >= std::numeric_limits<std::uint16_t>::max()) {
    // Overflow: store the real count as the first element.
    page->SetCount(std::numeric_limits<std::uint16_t>::max());
    dst[0] = PageId{count};
    std::memcpy(dst + 1, all.data(), count * sizeof(PageId));
  } else {
    page->SetCount(static_cast<std::uint16_t>(count));
    std::memcpy(dst, all.data(), count * sizeof(PageId));
  }
}

void FreeList::Reload(Page* page) noexcept {
  Deserialize(page);

  // Re-apply all pending page ids by removing them from the available set,
  // since they are already tracked separately.
  for (const auto& [_, ids] : pending_) {
    for (PageId id : ids) {
      ids_.Remove(id);
    }
  }
}

}  // namespace boltdb