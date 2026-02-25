#pragma once

#include "boltdb/type.hh"

namespace boltdb {

/// \brief BucketHeader represents the header of a bucket in BoltDB. It contains
///        the root page ID and a sequence number for the bucket.
struct BucketHeader {
  PageID root_page_id;
  uint64_t sequence;
};

class Bucket {};

} // namespace boltdb