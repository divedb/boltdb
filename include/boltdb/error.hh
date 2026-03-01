#pragma once

#include <string>
#include <system_error>

namespace boltdb {

enum class Errc {
  kOk = 0,
  kIOError,
  kShortWrite,
  kShortRead,

  kDatabaseNotOpen,
  kDatabaseOpen,
  kInvalidDatabase,
  kMagicMismatch,
  kVersionMismatch,
  kChecksumMismatch,
  kTimeout,
  kTxNotWritable,
  kTxClosed,
  kDatabaseReadOnly,
  kBucketNotFound,
  kBucketExists,
  kBucketNameRequired,
  kKeyRequired,
  kKeyTooLarge,
  kValueTooLarge,
  kIncompatibleValue,
};

class ErrorCategory final : public std::error_category {
 public:
  const char* name() const noexcept override { return "boltdb"; }

  std::string message(int ev) const override {
    switch (static_cast<Errc>(ev)) {
      case Errc::kOk:
        return "ok";
      case Errc::kIOError:
        return "I/O error";
      case Errc::kShortWrite:
        return "incomplete write: fewer bytes written than expected";
      case Errc::kShortRead:
        return "incomplete read: fewer bytes read than expected";
      case Errc::kDatabaseNotOpen:
        return "database not open";
      case Errc::kDatabaseOpen:
        return "database already open";
      case Errc::kInvalidDatabase:
        return "invalid database";
      case Errc::kVersionMismatch:
        return "version mismatch";
      case Errc::kChecksumMismatch:
        return "checksum mismatch";
      case Errc::kTimeout:
        return "timeout";
      case Errc::kTxNotWritable:
        return "transaction not writable";
      case Errc::kTxClosed:
        return "transaction closed";
      case Errc::kDatabaseReadOnly:
        return "database is read-only";
      case Errc::kBucketNotFound:
        return "bucket not found";
      case Errc::kBucketExists:
        return "bucket already exists";
      case Errc::kBucketNameRequired:
        return "bucket name required";
      case Errc::kKeyRequired:
        return "key required";
      case Errc::kKeyTooLarge:
        return "key too large";
      case Errc::kValueTooLarge:
        return "value too large";
      case Errc::kIncompatibleValue:
        return "incompatible value";
    }

    return "unknown boltdb error";
  }
};

inline const std::error_category& BoltErrorCategory() noexcept {
  static ErrorCategory category;
  return category;
}

inline std::error_code make_error_code(Errc e) noexcept {
  return {static_cast<int>(e), BoltErrorCategory()};
}

}  // namespace boltdb

namespace std {

template <>
struct is_error_code_enum<boltdb::Errc> : true_type {};

}  // namespace std
