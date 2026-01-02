#pragma once

#include <system_error>

namespace boltdb {

enum class ErrorCode {
  kIOError,
  kCorrupt,
  kKeyTooLarge,
  kValueTooLarge,
};

class ErrorCategory : public std::error_category {
 public:
  const char* name() const noexcept override { return "bolt"; }

  std::string message(int ev) const override {
    // switch (static_cast<ErrorCode>(ev)) {
    //   case ErrorCode::IO_ERROR:
    //     return "I/O error";
    //   case ErrorCode::CORRUPT:
    //     return "Database file is corrupt";
    //   case ErrorCode::INVALID_TX:
    //     return "Invalid transaction";
    //   case ErrorCode::KEY_TOO_LARGE:
    //     return "Key too large";
    //   case ErrorCode::VALUE_TOO_LARGE:
    //     return "Value too large";
    //   case ErrorCode::DB_CLOSED:
    //     return "Database closed";
    //   case ErrorCode::LOCKED:
    //     return "Database locked by another transaction";
    //   default:
    //     return "Unknown error";
    // }

    return "Unknown error";
  }
};

}  // namespace boltdb