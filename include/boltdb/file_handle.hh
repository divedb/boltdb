#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

#include "boltdb/error.hh"
#include "boltdb/expected.hh"
#include "boltdb/options.hh"

namespace boltdb {

class FileHandle {
 public:
#ifdef _WIN32
  using NativeHandle = HANDLE;
  static inline constexpr NativeHandle kInvalidNativeHandle =
      INVALID_HANDLE_VALUE;
#else
  using NativeHandle = int;
  static inline constexpr NativeHandle kInvalidNativeHandle = -1;
#endif

  FileHandle() = default;

  explicit FileHandle(NativeHandle handle) noexcept : handle_(handle) {}

  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  FileHandle(FileHandle&& other) noexcept { MoveFrom(std::move(other)); }

  FileHandle& operator=(FileHandle&& other) noexcept {
    if (this != &other) {
      Close();
      MoveFrom(std::move(other));
    }

    return *this;
  }

  ~FileHandle() { Close(); }

  [[nodiscard]] static Expected<FileHandle, std::error_code> Open(
      std::string_view path, const Options& options) {
#ifdef _WIN32
    const auto wide_path = Utf8ToWide(path);
    const DWORD desired_access =
        options.ReadOnly() ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    const DWORD share_mode =
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const DWORD creation_disposition =
        options.CreatedIfNotExists() ? OPEN_ALWAYS : OPEN_EXISTING;

    HANDLE handle =
        ::CreateFileW(wide_path.c_str(), desired_access, share_mode, nullptr,
                      creation_disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      return UnExpected(LastPlatformError());
    }

    return FileHandle(handle);
#else
    int flags = options.ReadOnly() ? O_RDONLY : O_RDWR;
    if (options.CreatedIfNotExists()) {
      flags |= O_CREAT;
    }

    const int fd = ::open(path.data(), flags, options.Mode());

    if (fd == -1) {
      return UnExpected(LastPlatformError());
    }

    return FileHandle(fd);
#endif
  }

  [[nodiscard]] bool IsOpen() const noexcept {
    return handle_ != kInvalidNativeHandle;
  }

  [[nodiscard]] bool IsLocked() const noexcept { return locked_; }

  [[nodiscard]] NativeHandle Get() const noexcept { return handle_; }

  [[nodiscard]] Expected<std::size_t, std::error_code> ReadAt(
      void* buffer, std::size_t size, std::uint64_t offset) const noexcept {
    if (!IsOpen()) {
      return UnExpected(make_error_code(Errc::kDatabaseNotOpen));
    }

    if (size == 0) return std::size_t{0};

#ifdef _WIN32
    OVERLAPPED overlapped{};
    overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);

    DWORD bytes_read = 0;
    if (::ReadFile(handle_, buffer, static_cast<DWORD>(size), &bytes_read,
                   &overlapped) == 0) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::size_t>(bytes_read);
#else
    const auto bytes_read =
        ::pread(handle_, buffer, size, static_cast<off_t>(offset));
    if (bytes_read < 0) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::size_t>(bytes_read);
#endif
  }

  [[nodiscard]] Expected<std::size_t, std::error_code> WriteAt(
      const void* buffer, std::size_t size,
      std::uint64_t offset) const noexcept {
    if (!IsOpen()) {
      return UnExpected(make_error_code(Errc::kDatabaseNotOpen));
    }

    if (size == 0) return std::size_t{0};

#ifdef _WIN32
    OVERLAPPED overlapped{};
    overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);

    DWORD bytes_written = 0;
    if (::WriteFile(handle_, buffer, static_cast<DWORD>(size), &bytes_written,
                    &overlapped) == 0) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::size_t>(bytes_written);
#else
    const auto bytes_written =
        ::pwrite(handle_, buffer, size, static_cast<off_t>(offset));
    if (bytes_written < 0) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::size_t>(bytes_written);
#endif
  }

  [[nodiscard]] Expected<std::uint64_t, std::error_code> Size() const noexcept {
    if (!IsOpen()) {
      return UnExpected(make_error_code(Errc::kDatabaseNotOpen));
    }

#ifdef _WIN32
    LARGE_INTEGER file_size{};

    if (::GetFileSizeEx(handle_, &file_size) == 0) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::uint64_t>(file_size.QuadPart);
#else
    struct stat st{};

    if (::fstat(handle_, &st) == -1) {
      return UnExpected(LastPlatformError());
    }

    return static_cast<std::uint64_t>(st.st_size);
#endif
  }

  std::error_code Resize(std::uint64_t size) const noexcept {
    if (!IsOpen()) {
      return make_error_code(Errc::kDatabaseNotOpen);
    }

#ifdef _WIN32
    LARGE_INTEGER distance{};
    distance.QuadPart = static_cast<LONGLONG>(size);

    LARGE_INTEGER current{};
    if (::SetFilePointerEx(handle_, distance, &current, FILE_BEGIN) == 0) {
      return LastPlatformError();
    }

    if (::SetEndOfFile(handle_) == 0) {
      return LastPlatformError();
    }
#else
    if (::ftruncate(handle_, static_cast<off_t>(size)) == -1) {
      return LastPlatformError();
    }
#endif

    return {};
  }

  std::error_code Sync() const noexcept {
    if (!IsOpen()) {
      return make_error_code(Errc::kDatabaseNotOpen);
    }

#ifdef _WIN32
    if (::FlushFileBuffers(handle_) == 0) {
      return LastPlatformError();
    }
#else
    if (::fsync(handle_) == -1) {
      return LastPlatformError();
    }
#endif

    return {};
  }

  std::error_code LockShared(std::chrono::milliseconds timeout) noexcept {
    return LockImpl(false, timeout);
  }

  std::error_code LockExclusive(std::chrono::milliseconds timeout) noexcept {
    return LockImpl(true, timeout);
  }

  std::error_code Unlock() noexcept {
    if (!IsOpen() || !locked_) {
      return {};
    }

#ifdef _WIN32
    OVERLAPPED overlapped{};
    if (::UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped) == 0) {
      return LastPlatformError();
    }
#else
    struct flock file_lock{};
    file_lock.l_type = F_UNLCK;
    file_lock.l_whence = SEEK_SET;
    file_lock.l_start = 0;
    file_lock.l_len = 0;

    if (::fcntl(handle_, F_SETLK, &file_lock) == -1) {
      return LastPlatformError();
    }
#endif

    locked_ = false;
    exclusive_lock_ = false;
    return {};
  }

  std::error_code Close() noexcept {
    std::error_code ec;

    if (!IsOpen()) {
      locked_ = false;
      exclusive_lock_ = false;
      return ec;
    }

    if (auto unlock_ec = Unlock(); unlock_ec && !ec) {
      ec = unlock_ec;
    }

#ifdef _WIN32
    if (::CloseHandle(handle_) == 0 && !ec) {
      ec = LastPlatformError();
    }
#else
    if (::close(handle_) == -1 && !ec) {
      ec = LastPlatformError();
    }
#endif

    handle_ = kInvalidNativeHandle;
    locked_ = false;
    exclusive_lock_ = false;

    return ec;
  }

 private:
  void MoveFrom(FileHandle&& other) noexcept {
    handle_ = other.handle_;
    locked_ = other.locked_;
    exclusive_lock_ = other.exclusive_lock_;

    other.handle_ = kInvalidNativeHandle;
    other.locked_ = false;
    other.exclusive_lock_ = false;
  }

  [[nodiscard]] static std::error_code LastPlatformError() {
#ifdef _WIN32
    return std::error_code(static_cast<int>(::GetLastError()),
                           std::system_category());
#else
    return std::error_code(errno, std::generic_category());
#endif
  }

#ifdef _WIN32
  [[nodiscard]] static std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
      return {};
    }

    const int required =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                              static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
      return std::wstring(value.begin(), value.end());
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          required);

    return result;
  }
#endif

  std::error_code LockImpl(bool exclusive,
                           std::chrono::milliseconds timeout) noexcept {
    if (!IsOpen()) {
      return make_error_code(Errc::kDatabaseNotOpen);
    }

    if (locked_) {
      if (exclusive_lock_ == exclusive) {
        return {};
      }

      if (auto ec = Unlock()) {
        return ec;
      }
    }

#ifdef _WIN32
    OVERLAPPED overlapped{};
    DWORD flags = exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0;

    if (timeout <= std::chrono::milliseconds::zero()) {
      if (::LockFileEx(handle_, flags, 0, MAXDWORD, MAXDWORD, &overlapped) ==
          0) {
        return LastPlatformError();
      }
    } else {
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while (true) {
        if (::LockFileEx(handle_, flags | LOCKFILE_FAIL_IMMEDIATELY, 0,
                         MAXDWORD, MAXDWORD, &overlapped) != 0) {
          break;
        }

        const auto ec = LastPlatformError();
        if (ec.value() != static_cast<int>(ERROR_LOCK_VIOLATION)) {
          return ec;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
          return make_error_code(Errc::kTimeout);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
#else
    struct flock file_lock{};
    file_lock.l_type = exclusive ? F_WRLCK : F_RDLCK;
    file_lock.l_whence = SEEK_SET;
    file_lock.l_start = 0;
    file_lock.l_len = 0;

    if (timeout <= std::chrono::milliseconds::zero()) {
      while (::fcntl(handle_, F_SETLKW, &file_lock) == -1) {
        if (errno == EINTR) {
          continue;
        }

        return LastPlatformError();
      }
    } else {
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while (::fcntl(handle_, F_SETLK, &file_lock) == -1) {
        if (errno != EACCES && errno != EAGAIN) {
          if (errno == EINTR) {
            continue;
          }

          return LastPlatformError();
        }

        if (std::chrono::steady_clock::now() >= deadline) {
          return make_error_code(Errc::kTimeout);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
#endif

    locked_ = true;
    exclusive_lock_ = exclusive;
    return {};
  }

  NativeHandle handle_{kInvalidNativeHandle};
  bool locked_{false};
  bool exclusive_lock_{false};
};

}  // namespace boltdb
