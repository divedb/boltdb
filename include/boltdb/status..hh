#pragma once

#include <memory>
#include <string>
#include <system_error>

namespace boltdb {

class DBErrorCategory : public std::error_category {
 public:
  const char* name() const noexcept override { return "boltdb"; }
  std::string message(int ev) const override;
};

enum class DBErrorCode {
  kOk = 0,
  kMmapTooLarge,  ///< The mmap size exceeds the maximum allowed size.

};

class Status {
 public:
  /// \brief Creates an OK status.
  ///
  /// \return An OK status.
  static Status OK() noexcept { return Status(); }

  /// \brief Creates an error status.
  ///
  /// \param code    The error code.
  /// \param message The error message.
  /// \return        An error status.
  static Status Error(DBErrorCode code, const char* message) noexcept {
    return Status(code, message);
  }

  /// \brief Wraps an existing status with additional context.
  ///
  /// \param cause   The original status to wrap.
  /// \param context Additional context message.
  /// \return        A new status that wraps the original.
  static Status Wrap(const Status& cause, const char* context) noexcept {
    return Status(cause.code_, context, std::make_unique<Status>(cause));
  }

  Status() noexcept = default;

  /// \brief Checks if the status is OK.
  ///
  /// \return True if the status is OK; otherwise false.
  bool IsOk() const noexcept { return code_ == DBErrorCode::kOk; }

  /// \brief Retrieves the error message.
  ///
  /// \return The error message.
  const char* GetMessage() const noexcept { return message_; }

  /// \brief Retrieves the error code.
  ///
  /// \return The error code.
  DBErrorCode GetCode() const noexcept { return code_; }

  /// \brief Checks if there is a chained cause.
  ///
  /// \return True if there is a cause; otherwise false.
  bool HasCause() const noexcept { return cause_ != nullptr; }

  /// \brief Retrieves the chained cause status.
  ///
  /// \return The cause status, or nullptr if none exists.
  const Status* GetCause() const noexcept { return cause_.get(); }

 private:
  Status(DBErrorCode code, const char* message,
         std::unique_ptr<Status> cause = nullptr)
      : code_(code), message_(message), cause_(std::move(cause)) {}

  DBErrorCode code_;
  const char* message_;
  std::unique_ptr<Status> cause_;  ///< Optional chained cause.
};

inline std::string DBErrorCategory::message(int ev) const {
  switch (static_cast<DBErrorCode>(ev)) {
    case DBErrorCode::kOk:
      return "OK";
    case DBErrorCode::kMmapTooLarge:
      return "mmap too large";
    default:
      return "unknown boltdb error";
  }
}

constexpr std::error_code MakeErrorCode(DBErrorCode e) noexcept {
  return {static_cast<int>(e), DBErrorCategory()};
}

}  // namespace boltdb