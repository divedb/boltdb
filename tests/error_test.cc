#include "error.hh"

#include <gtest/gtest.h>

#include <system_error>

namespace boltdb {

TEST(ErrorCategoryTest, CategoryName) {
  EXPECT_STREQ(BoltErrorCategory().name(), "boltdb");
}

TEST(ErrorCategoryTest, ImplicitErrcToErrorCode) {
  std::error_code ec = Errc::kBucketExists;

  EXPECT_EQ(ec.category(), BoltErrorCategory());
  EXPECT_EQ(ec.value(), static_cast<int>(Errc::kBucketExists));
  EXPECT_EQ(ec.message(), "bucket already exists");
}

TEST(ErrorCategoryTest, MakeErrorCode) {
  auto ec = make_error_code(Errc::kTxClosed);

  EXPECT_EQ(ec.category(), BoltErrorCategory());
  EXPECT_EQ(ec.message(), "transaction closed");
}

TEST(ErrorCategoryTest, UnknownErrorValueFallback) {
  std::error_code ec(9999, BoltErrorCategory());

  EXPECT_EQ(ec.message(), "unknown boltdb error");
}

}  // namespace boltdb
