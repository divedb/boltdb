#include "bucket.hh"

#include <gtest/gtest.h>

namespace boltdb {

TEST(BucketTest, DefaultState) {
  Bucket b;

  EXPECT_EQ(b.Root(), 0u);
  EXPECT_EQ(b.Sequence(), 0u);
}

TEST(BucketTest, ConstructFromHeader) {
  BucketHeader header{
      .root_page_id = 42,
      .sequence = 9,
  };
  Bucket b(header);

  EXPECT_EQ(b.Root(), 42u);
  EXPECT_EQ(b.Sequence(), 9u);
  EXPECT_EQ(b.Header().root_page_id, 42u);
  EXPECT_EQ(b.Header().sequence, 9u);
}

TEST(BucketTest, MutatorsAndNextSequence) {
  Bucket b;

  b.SetRoot(100);
  b.SetSequence(7);

  EXPECT_EQ(b.Root(), 100u);
  EXPECT_EQ(b.Sequence(), 7u);
  EXPECT_EQ(b.NextSequence(), 8u);
  EXPECT_EQ(b.Sequence(), 8u);
}

TEST(BucketTest, MutableHeaderView) {
  Bucket b;
  auto& header = b.MutableHeader();
  header.root_page_id = 77;
  header.sequence = 123;

  EXPECT_EQ(b.Root(), 77u);
  EXPECT_EQ(b.Sequence(), 123u);
}

}  // namespace boltdb
