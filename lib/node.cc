#include "boltdb/node.hh"

namespace boltdb {

Node* Node::ChildAt(std::size_t index) {
  assert(!is_leaf_ && "ChildAt should only be called on branch nodes");
  assert(index < children_.size() && "ChildAt index out of bounds");

  return children_[index];
}

}  // namespace boltdb