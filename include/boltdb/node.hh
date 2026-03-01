#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

#include "boltdb/page.hh"

namespace boltdb {

class Bucket;

/// inode represents an internal node inside of a node.
struct INode {
  std::vector<std::byte> key;
  std::vector<std::byte> value;
  PageId pgid;
  std::uint32_t flags;
};

class Node {
 public:
  Node(Bucket* owner, bool is_leaf, Node* parent = nullptr)
      : owner_(owner),
        pgid_(kSentinelPageId),
        parent_(parent),
        is_leaf_(is_leaf) {}

  [[nodiscard]] bool IsLeaf() const noexcept { return is_leaf_; }

  [[nodiscard]] bool Empty() const noexcept { return inodes_.empty(); }

  [[nodiscard]] std::size_t Count() const noexcept { return inodes_.size(); }

  [[nodiscard]] std::size_t ChildCount() const noexcept {
    return children_.size();
  }

  [[nodiscard]] PageId PageID() const noexcept { return pgid_; }

  void SetPageID(PageId pgid) noexcept { pgid_ = pgid; }

  [[nodiscard]] const std::vector<std::byte>& Key() const noexcept {
    return key_;
  }

  [[nodiscard]] const INode& InodeAt(std::size_t index) const {
    assert(index < inodes_.size() && "InodeAt index out of bounds");

    return inodes_[index];
  }

  [[nodiscard]] INode& InodeAt(std::size_t index) {
    assert(index < inodes_.size() && "InodeAt index out of bounds");

    return inodes_[index];
  }

  [[nodiscard]] static int CompareKeys(std::span<const std::byte> lhs,
                                       std::span<const std::byte> rhs) noexcept {
    return CompareBytes(lhs, rhs);
  }

  const Node* Root() const noexcept {
    const Node* n = this;

    while (n->parent_) n = n->parent_;

    return n;
  }

  /// \brief Size returns the total size of this node in bytes when serialized
  ///        to a page.
  ///
  /// \return The total size of this node in bytes when serialized to a page.
  std::size_t Size() const noexcept {
    int elem_size = PageElementSize();

    return std::accumulate(inodes_.begin(), inodes_.end(), Page::kHeaderSize,
                           [elem_size](std::size_t sum, const INode& inode) {
                             return sum + elem_size + inode.key.size() +
                                    inode.value.size();
                           });
  }

  bool IsSizeLessThan(std::size_t sz) const noexcept { return Size() < sz; }

  [[nodiscard]] std::size_t LowerBound(
      std::span<const std::byte> key) const noexcept {
    const auto it = std::lower_bound(
        inodes_.begin(), inodes_.end(), key,
        [](const INode& inode, std::span<const std::byte> probe) {
          return CompareBytes(inode.key, probe) < 0;
        });

    return static_cast<std::size_t>(std::distance(inodes_.begin(), it));
  }

  [[nodiscard]] bool KeyEquals(std::size_t index,
                               std::span<const std::byte> key) const noexcept {
    return index < inodes_.size() && CompareBytes(inodes_[index].key, key) == 0;
  }

  [[nodiscard]] std::size_t ChildIndex(
      std::span<const std::byte> key) const noexcept {
    assert(!is_leaf_ && "ChildIndex should only be called on branch nodes");
    assert(!inodes_.empty() && "ChildIndex requires at least one inode");

    const auto it = std::upper_bound(
        inodes_.begin(), inodes_.end(), key,
        [](std::span<const std::byte> probe, const INode& inode) {
          return CompareBytes(probe, inode.key) < 0;
        });

    if (it == inodes_.begin()) return 0;

    return static_cast<std::size_t>(std::distance(inodes_.begin(), it) - 1);
  }

  Node* ChildAt(std::size_t index) {
    assert(!is_leaf_ && "ChildAt should only be called on branch nodes");
    assert(index < children_.size() && "ChildAt index out of bounds");

    return children_[index];
  }

  void AppendChild(Node* child) {
    assert(!is_leaf_ && "AppendChild should only be called on branch nodes");
    assert(child != nullptr && "AppendChild requires a child node");

    child->parent_ = this;
    children_.push_back(child);
  }

  void Put(std::span<const std::byte> old_key, std::span<const std::byte> new_key,
           std::span<const std::byte> value, PageId pgid, std::uint32_t flags) {
    const auto lookup_key = old_key.empty() ? new_key : old_key;
    const auto index = LowerBound(lookup_key);

    INode replacement{
        .key = std::vector<std::byte>(new_key.begin(), new_key.end()),
        .value = std::vector<std::byte>(value.begin(), value.end()),
        .pgid = pgid,
        .flags = flags,
    };

    if (index < inodes_.size() && CompareBytes(inodes_[index].key, lookup_key) == 0) {
      inodes_[index] = std::move(replacement);
    } else {
      inodes_.insert(inodes_.begin() + static_cast<std::ptrdiff_t>(index),
                     std::move(replacement));
    }

    if (!inodes_.empty()) key_ = inodes_.front().key;
  }

  bool Delete(std::span<const std::byte> key) {
    const auto index = LowerBound(key);
    if (index >= inodes_.size() || CompareBytes(inodes_[index].key, key) != 0) {
      return false;
    }

    inodes_.erase(inodes_.begin() + static_cast<std::ptrdiff_t>(index));
    key_ = inodes_.empty() ? std::vector<std::byte>{} : inodes_.front().key;
    return true;
  }

 private:
  static int CompareBytes(std::span<const std::byte> lhs,
                          std::span<const std::byte> rhs) noexcept {
    const auto mismatch = std::mismatch(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    if (mismatch.first == lhs.end() && mismatch.second == rhs.end()) return 0;
    if (mismatch.first == lhs.end()) return -1;
    if (mismatch.second == rhs.end()) return 1;

    return std::to_integer<unsigned char>(*mismatch.first) <
                   std::to_integer<unsigned char>(*mismatch.second)
               ? -1
               : 1;
  }

  /// This is used to determine when a node is underflowed and needs to be
  /// rebalanced or merged.
  ///
  /// \return The minimum number of keys this node should have.
  int MinKeys() const noexcept { return is_leaf_ ? 1 : 2; }

  /// Size of the element header for this node type.
  ///
  /// \return The size of the element header for this node type.
  int PageElementSize() const noexcept {
    return is_leaf_ ? kLeafElementSize : kBranchElementSize;
  }

  Bucket* owner_;
  PageId pgid_;
  Node* parent_;
  std::vector<Node*> children_;
  std::vector<std::byte> key_;
  std::vector<INode> inodes_;
  bool is_leaf_;
  bool unbalanced_ = false;
  bool spilled_ = false;
};

}  // namespace boltdb
