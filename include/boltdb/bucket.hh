#pragma once

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "boltdb/error.hh"
#include "boltdb/node.hh"
#include "boltdb/page.hh"

namespace boltdb {

class Transaction;

class Bucket {
 public:
  Bucket() = default;

  explicit Bucket(BucketHeader header, Transaction* txn = nullptr) noexcept
      : header_(header), txn_(txn) {}

  [[nodiscard]] const BucketHeader& Header() const noexcept { return header_; }

  [[nodiscard]] BucketHeader& MutableHeader() noexcept { return header_; }

  [[nodiscard]] PageId Root() const noexcept { return header_.root_page_id; }

  void SetRoot(PageId root_page_id) noexcept {
    header_.root_page_id = root_page_id;
  }

  [[nodiscard]] uint64_t Sequence() const noexcept { return header_.sequence; }

  void SetSequence(uint64_t sequence) noexcept { header_.sequence = sequence; }

  uint64_t NextSequence() noexcept { return ++header_.sequence; }

  [[nodiscard]] std::pair<Page*, Node*> PageNode(PageId pgid) noexcept;

  [[nodiscard]] Node* MaterializeNode(PageId pgid,
                                      Node* parent = nullptr) noexcept;

  [[nodiscard]] Node* RootNode() noexcept { return root_node_; }

  [[nodiscard]] const Node* RootNode() const noexcept { return root_node_; }

  std::error_code Put(std::span<const std::byte> key,
                      std::span<const std::byte> value) noexcept;

 private:
  Node* EnsureRootLeaf() noexcept;

  /// \brief IsInlinedBucket returns true if this bucket is an inlined bucket,
  ///        which means that it does not have a root page and its data is
  ///        stored directly in the parent bucket's page. Inlined buckets are
  ///        used for small sub-buckets to save space and reduce the number of
  ///        page allocations.
  ///
  /// \return true if this bucket is an inlined bucket; otherwise false.
  bool IsInlinedBucket() const noexcept {
    return header_.root_page_id == kSentinelPageId;
  }

  Node* GetNode(PageId pgid) noexcept;

  BucketHeader header_;

  /// Node cache for this bucket, keyed by page ID. This is used to avoid
  /// repeatedly reading the same page into a Node.
  std::unordered_map<PageId, Node*> nodes_;

  /// Sub-buckets contained within this bucket, keyed by their string name.
  std::unordered_map<std::string, std::unique_ptr<Bucket>> buckets_;

  /// The transaction that owns this bucket. This is used to read/write pages
  /// and to manage the lifecycle of this bucket.
  Transaction* txn_ = nullptr;

  /// If this bucket is a sub-bucket, this points to the inline page reference.
  Page* page_ = nullptr;

  /// The materialized node for the root page. This is lazily initialized.
  Node* root_node_ = nullptr;
};

inline Node* Bucket::GetNode(PageId pgid) noexcept {
  const auto it = nodes_.find(pgid);

  if (it == nodes_.end()) return nullptr;

  return it->second;
}

inline std::pair<Page*, Node*> Bucket::PageNode(PageId pgid) noexcept {
  if (IsInlinedBucket()) {
    assert(pgid == kSentinelPageId &&
           "Inlined buckets should only have the sentinel page ID");

    if (root_node_ != nullptr) return {nullptr, root_node_};

    return {page_, nullptr};
  }

  // Check the node cache for non-inline buckets.
  if (auto node = GetNode(pgid); node != nullptr) return {nullptr, node};
}

inline Node* Bucket::MaterializeNode(PageId pgid, Node* parent) noexcept {
  if (Node* node = GetNode(pgid); node != nullptr) return node;

  if (page_ != nullptr && page_->id == pgid) {
    auto [it, inserted] =
        nodes_.try_emplace(pgid, this, page_->IsLeaf(), parent);
    if (inserted) it->second.SetPageID(pgid);
    return &it->second;
  }

  return nullptr;
}

inline Node* Bucket::EnsureRootLeaf() noexcept {
  if (root_node_ != nullptr) return root_node_;

  const auto node_id =
      header_.root_page_id == PageId{} ? kSentinelPageId : header_.root_page_id;
  auto [it, inserted] = nodes_.try_emplace(node_id, this, true, nullptr);
  root_node_ = &it->second;
  if (inserted) root_node_->SetPageID(node_id);

  return root_node_;
}

}  // namespace boltdb

#include "boltdb/cursor.hh"

namespace boltdb {

inline std::error_code Bucket::Put(std::span<const std::byte> key,
                                   std::span<const std::byte> value) noexcept {
  if (key.empty()) return make_error_code(Errc::kKeyRequired);
  if (key.size() > std::numeric_limits<std::uint32_t>::max()) {
    return make_error_code(Errc::kKeyTooLarge);
  }
  if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
    return make_error_code(Errc::kValueTooLarge);
  }

  EnsureRootLeaf();

  Cursor cursor(this);
  const auto item = cursor.SeekItem(key);

  if (!item.Empty() && item.key.size() == key.size() &&
      Node::CompareKeys(item.key, key) == 0 && item.IsBucket()) {
    return make_error_code(Errc::kIncompatibleValue);
  }

  Node* leaf = cursor.NodeRef();
  if (leaf == nullptr) leaf = EnsureRootLeaf();
  if (leaf == nullptr || !leaf->IsLeaf()) {
    return make_error_code(Errc::kInvalidDatabase);
  }

  std::vector<std::byte> owned_key(key.begin(), key.end());
  leaf->Put(owned_key, owned_key, value, kSentinelPageId,
            static_cast<std::uint32_t>(LeafFlag::kNone));

  return {};
}

}  // namespace boltdb
