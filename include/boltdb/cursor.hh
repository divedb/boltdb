#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "boltdb/bucket.hh"

namespace boltdb {

/// \brief ElemRef represents a reference to an element in a page or node. It
///        contains either a pointer to a page or a pointer to a node, along
///        with an index indicating the position within the page or node.
struct ElemRef {
  Page* page = nullptr;
  Node* node = nullptr;
  std::size_t index = 0;

  [[nodiscard]] bool IsLeaf() const noexcept;
  [[nodiscard]] std::size_t Count() const noexcept;
};

struct CursorItem {
  std::span<const std::byte> key;
  std::span<const std::byte> value;
  std::uint32_t flags = 0;

  [[nodiscard]] bool Empty() const noexcept { return key.empty(); }

  [[nodiscard]] bool IsBucket() const noexcept {
    return flags == static_cast<std::uint32_t>(LeafFlag::kBucket);
  }
};

/// \brief Cursor represents an iterator that can traverse over all key/value
///        pairs in a bucket in sorted order.
///
/// \note Cursors are not thread-safe and should only be used within the
///       transaction that created them.
/// \note Keys and values returned from the cursor are only valid for the life
///       of the transaction.
/// \note Changing data while traversing with a cursor may cause it to be
///       invalidated and return unexpected keys and/or values.
///       You must reposition your cursor after mutating data.
class Cursor {
 public:
  explicit Cursor(Bucket* bucket = nullptr) : bucket_(bucket) {}

  [[nodiscard]] Bucket* GetBucket() const noexcept { return bucket_; }

  [[nodiscard]] const std::vector<ElemRef>& Stack() const noexcept {
    return stack_;
  }

  [[nodiscard]] std::pair<std::span<const std::byte>,
                          std::span<const std::byte>>
  First() {
    stack_.clear();
    if (bucket_ == nullptr) return {};

    auto [page, node] = bucket_->PageNode(bucket_->Root());
    stack_.push_back({.page = page, .node = node, .index = 0});
    FirstToLeaf();

    if (!stack_.empty() && stack_.back().Count() == 0) {
      const auto item = NextInternal();
      return Normalize(item);
    }

    return Normalize(KeyValue());
  }

  [[nodiscard]] std::pair<std::span<const std::byte>,
                          std::span<const std::byte>>
  Last() {
    stack_.clear();
    if (bucket_ == nullptr) return {};

    auto [page, node] = bucket_->PageNode(bucket_->Root());
    ElemRef ref{.page = page, .node = node, .index = 0};
    ref.index = ref.Count() == 0 ? 0 : ref.Count() - 1;
    stack_.push_back(ref);
    LastToLeaf();

    return Normalize(KeyValue());
  }

  [[nodiscard]] std::pair<std::span<const std::byte>,
                          std::span<const std::byte>>
  Next() {
    return Normalize(NextInternal());
  }

  [[nodiscard]] std::pair<std::span<const std::byte>,
                          std::span<const std::byte>>
  Prev() {
    for (std::size_t i = stack_.size(); i > 0; --i) {
      auto& elem = stack_[i - 1];
      if (elem.index > 0) {
        --elem.index;
        break;
      }
      stack_.resize(i - 1);
    }

    if (stack_.empty()) return {};

    LastToLeaf();
    return Normalize(KeyValue());
  }

  [[nodiscard]] std::pair<std::span<const std::byte>,
                          std::span<const std::byte>>
  Seek(std::span<const std::byte> seek) {
    auto item = SeekInternal(seek);

    if (!stack_.empty() && stack_.back().index >= stack_.back().Count()) {
      item = NextInternal();
    }

    return Normalize(item);
  }

  std::error_code Delete() {
    Node* leaf = CurrentNode();
    if (leaf == nullptr) return make_error_code(Errc::kInvalidDatabase);

    const auto item = KeyValue();
    if (item.Empty()) return make_error_code(Errc::kKeyRequired);
    if (item.IsBucket()) return make_error_code(Errc::kIncompatibleValue);

    if (!leaf->Delete(item.key)) return make_error_code(Errc::kKeyRequired);

    return {};
  }

  [[nodiscard]] CursorItem SeekItem(std::span<const std::byte> seek) {
    return SeekInternal(seek);
  }

  [[nodiscard]] Node* NodeRef() { return CurrentNode(); }

 private:
  [[nodiscard]] static std::pair<std::span<const std::byte>,
                                 std::span<const std::byte>>
  Normalize(const CursorItem& item) noexcept {
    if (item.Empty()) return {};
    if (item.IsBucket()) return {item.key, {}};

    return {item.key, item.value};
  }

  [[nodiscard]] CursorItem SeekInternal(std::span<const std::byte> seek) {
    stack_.clear();
    if (bucket_ == nullptr) return {};

    Search(seek, bucket_->Root());
    if (stack_.empty()) return {};

    auto& ref = stack_.back();
    if (ref.index >= ref.Count()) return {};

    return KeyValue();
  }

  void FirstToLeaf() {
    while (!stack_.empty()) {
      auto& ref = stack_.back();
      if (ref.IsLeaf()) break;

      auto pgid = ChildPageId(ref);
      auto [page, node] = bucket_->PageNode(pgid);
      stack_.push_back({.page = page, .node = node, .index = 0});
    }
  }

  void LastToLeaf() {
    while (!stack_.empty()) {
      auto& ref = stack_.back();
      if (ref.IsLeaf()) break;

      auto pgid = ChildPageId(ref);
      auto [page, node] = bucket_->PageNode(pgid);
      ElemRef next{.page = page, .node = node, .index = 0};
      next.index = next.Count() == 0 ? 0 : next.Count() - 1;
      stack_.push_back(next);
    }
  }

  [[nodiscard]] CursorItem NextInternal() {
    for (;;) {
      std::ptrdiff_t i = static_cast<std::ptrdiff_t>(stack_.size()) - 1;
      for (; i >= 0; --i) {
        auto& elem = stack_[static_cast<std::size_t>(i)];
        if (elem.index + 1 < elem.Count()) {
          ++elem.index;
          break;
        }
      }

      if (i < 0) return {};

      stack_.resize(static_cast<std::size_t>(i) + 1);
      FirstToLeaf();

      if (!stack_.empty() && stack_.back().Count() == 0) continue;

      return KeyValue();
    }
  }

  void Search(std::span<const std::byte> key, PageId pgid) {
    auto [page, node] = bucket_->PageNode(pgid);
    stack_.push_back({.page = page, .node = node, .index = 0});

    if (stack_.back().IsLeaf()) {
      NSearch(key);
      return;
    }

    if (node != nullptr) {
      SearchNode(key, node);
      return;
    }

    SearchPage(key, page);
  }

  void SearchNode(std::span<const std::byte> key, Node* node) {
    bool exact = false;
    const auto index = LowerBoundIndex(
        node->Count(), key,
        [&](std::size_t i, std::span<const std::byte> probe) {
          const auto ret = Node::CompareKeys(node->InodeAt(i).key, probe);
          if (ret == 0) exact = true;
          return ret < 0;
        });
    std::size_t child_index = index;
    if (!exact && child_index > 0) --child_index;
    stack_.back().index = child_index;

    if (child_index >= node->Count()) return;

    Search(key, node->InodeAt(child_index).pgid);
  }

  void SearchPage(std::span<const std::byte> key, Page* page) {
    if (page == nullptr || !page->IsBranch()) return;

    const auto inodes = page->BranchElements();
    bool exact = false;
    auto it = std::lower_bound(
        inodes.begin(), inodes.end(), key,
        [&](const BranchElement& elem, std::span<const std::byte> probe) {
          const auto ret = Node::CompareKeys(elem.Key(), probe);
          if (ret == 0) exact = true;
          return ret < 0;
        });

    std::size_t index =
        static_cast<std::size_t>(std::distance(inodes.begin(), it));
    if (!exact && index > 0) --index;
    stack_.back().index = index;

    if (index >= inodes.size()) return;

    Search(key, inodes[index].pgid);
  }

  void NSearch(std::span<const std::byte> key) {
    if (stack_.empty()) return;

    auto& ref = stack_.back();
    if (ref.node != nullptr) {
      ref.index = ref.node->LowerBound(key);
      return;
    }

    if (ref.page == nullptr || !ref.page->IsLeaf()) {
      ref.index = 0;
      return;
    }

    const auto inodes = ref.page->LeafElements();
    auto it = std::lower_bound(
        inodes.begin(), inodes.end(), key,
        [&](const LeafElement& elem, std::span<const std::byte> probe) {
          return Node::CompareKeys(elem.Key(), probe) < 0;
        });
    ref.index = static_cast<std::size_t>(std::distance(inodes.begin(), it));
  }

  [[nodiscard]] CursorItem KeyValue() const {
    if (stack_.empty()) return {};

    const auto& ref = stack_.back();
    if (ref.Count() == 0 || ref.index >= ref.Count()) return {};

    if (ref.node != nullptr) {
      const auto& inode = ref.node->InodeAt(ref.index);
      return {
          .key = inode.key,
          .value = inode.value,
          .flags = inode.flags,
      };
    }

    if (ref.page == nullptr || !ref.page->IsLeaf()) return {};

    const auto& elem =
        ref.page->GetLeafElement(static_cast<std::uint16_t>(ref.index));
    return {
        .key = elem.Key(),
        .value = elem.Value(),
        .flags = static_cast<std::uint32_t>(elem.flags),
    };
  }

  [[nodiscard]] Node* CurrentNode() {
    if (stack_.empty() || bucket_ == nullptr) return nullptr;

    if (auto& ref = stack_.back(); ref.node != nullptr && ref.IsLeaf()) {
      return ref.node;
    }

    Node* node = stack_.front().node;
    if (node == nullptr && stack_.front().page != nullptr) {
      node = bucket_->MaterializeNode(stack_.front().page->id, nullptr);
    }

    if (node == nullptr) return nullptr;

    const std::size_t limit = stack_.empty() ? 0 : stack_.size() - 1;
    for (std::size_t i = 0; i < limit; ++i) {
      if (node->IsLeaf()) return nullptr;
      const auto index = stack_[i].index;
      if (index >= node->ChildCount()) return nullptr;
      node = node->ChildAt(index);
      if (node == nullptr) return nullptr;
    }

    return node->IsLeaf() ? node : nullptr;
  }

  [[nodiscard]] static PageId ChildPageId(const ElemRef& ref) noexcept {
    if (ref.node != nullptr) {
      if (ref.index >= ref.node->Count()) return kSentinelPageId;
      return ref.node->InodeAt(ref.index).pgid;
    }

    if (ref.page != nullptr && ref.page->IsBranch() &&
        ref.index < ref.page->count) {
      return ref.page->GetBranchElement(static_cast<std::uint16_t>(ref.index))
          .pgid;
    }

    return kSentinelPageId;
  }

  template <class T, class Compare>
  [[nodiscard]] static std::size_t LowerBoundIndex(std::size_t count, T needle,
                                                   Compare compare) {
    std::size_t first = 0;
    std::size_t len = count;
    while (len > 0) {
      const auto step = len / 2;
      const auto mid = first + step;
      if (compare(mid, needle)) {
        first = mid + 1;
        len -= step + 1;
      } else {
        len = step;
      }
    }

    return first;
  }

  Bucket* bucket_ = nullptr;
  std::vector<ElemRef> stack_;
};

}  // namespace boltdb
