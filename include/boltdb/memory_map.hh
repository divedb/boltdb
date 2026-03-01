#pragma once

#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "boltdb/error.hh"
#include "boltdb/expected.hh"
#include "boltdb/file_handle.hh"

namespace boltdb {

class MemoryMap {
 public:
  MemoryMap() = default;

  MemoryMap(const MemoryMap&) = delete;
  MemoryMap& operator=(const MemoryMap&) = delete;

  MemoryMap(MemoryMap&& other) noexcept { MoveFrom(std::move(other)); }

  MemoryMap& operator=(MemoryMap&& other) noexcept {
    if (this != &other) {
      Unmap();
      MoveFrom(std::move(other));
    }

    return *this;
  }

  ~MemoryMap() { Unmap(); }

  [[nodiscard]] static Expected<MemoryMap, std::error_code> Map(
      const FileHandle& file, bool writeable, std::uint64_t offset = 0,
      std::size_t length = 0) {
    if (!file.IsOpen()) {
      return UnExpected(make_error_code(Errc::kDatabaseNotOpen));
    }

    auto file_size = file.Size();
    if (!file_size.HasValue()) {
      return UnExpected(file_size.Error());
    }

    if (offset > file_size.Value()) {
      return UnExpected(make_error_code(Errc::kInvalidDatabase));
    }

    const auto remaining = file_size.Value() - offset;
    const std::uint64_t requested_length =
        length == 0 ? remaining : static_cast<std::uint64_t>(length);
    if (requested_length > remaining) {
      return UnExpected(make_error_code(Errc::kInvalidDatabase));
    }

    if (requested_length == 0) {
      return MemoryMap();
    }

    const auto granularity = AllocationGranularity();
    const std::uint64_t aligned_offset = offset - (offset % granularity);
    const std::uint64_t delta = offset - aligned_offset;
    const std::uint64_t mapped_length = requested_length + delta;

    if (mapped_length > static_cast<std::uint64_t>(
                            std::numeric_limits<std::size_t>::max())) {
      return UnExpected(make_error_code(Errc::kInvalidDatabase));
    }

    MemoryMap map;
    const auto ec = map.MapImpl(file, writeable, aligned_offset,
                                static_cast<std::size_t>(mapped_length),
                                static_cast<std::size_t>(delta),
                                static_cast<std::size_t>(requested_length));
    if (ec) {
      return UnExpected(ec);
    }

    return map;
  }

  [[nodiscard]] bool IsMapped() const noexcept { return base_ != nullptr; }

  [[nodiscard]] bool IsWriteable() const noexcept { return writeable_; }

  [[nodiscard]] std::size_t Size() const noexcept { return view_length_; }

  [[nodiscard]] std::uint64_t Offset() const noexcept { return offset_; }

  [[nodiscard]] const std::byte* Data() const noexcept {
    return reinterpret_cast<const std::byte*>(data_);
  }

  [[nodiscard]] std::byte* MutableData() noexcept {
    if (!writeable_) {
      return nullptr;
    }

    return reinterpret_cast<std::byte*>(data_);
  }

  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept {
    return {Data(), view_length_};
  }

  [[nodiscard]] std::span<std::byte> MutableBytes() noexcept {
    auto* ptr = MutableData();
    if (ptr == nullptr) {
      return {};
    }

    return {ptr, view_length_};
  }

  std::error_code Sync() noexcept {
    if (!IsMapped()) {
      return {};
    }

#ifdef _WIN32
    if (::FlushViewOfFile(data_, view_length_) == 0) {
      return LastPlatformError();
    }
#else
    if (::msync(base_, mapped_length_, MS_SYNC) == -1) {
      return LastPlatformError();
    }
#endif

    return {};
  }

  std::error_code Unmap() noexcept {
    std::error_code ec;

    if (!IsMapped()) {
      Reset();
      return ec;
    }

#ifdef _WIN32
    if (::UnmapViewOfFile(base_) == 0) {
      ec = LastPlatformError();
    }

    if (mapping_handle_ != nullptr) {
      if (::CloseHandle(mapping_handle_) == 0 && !ec) {
        ec = LastPlatformError();
      }
    }
#else
    if (::munmap(base_, mapped_length_) == -1) {
      ec = LastPlatformError();
    }
#endif

    Reset();
    return ec;
  }

 private:
  [[nodiscard]] static std::error_code LastPlatformError() {
#ifdef _WIN32
    return std::error_code(static_cast<int>(::GetLastError()),
                           std::system_category());
#else
    return std::error_code(errno, std::generic_category());
#endif
  }

  [[nodiscard]] static std::uint64_t AllocationGranularity() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    return static_cast<std::uint64_t>(info.dwAllocationGranularity);
#else
    const auto page_size = ::sysconf(_SC_PAGE_SIZE);
    if (page_size <= 0) {
      return 4096;
    }

    return static_cast<std::uint64_t>(page_size);
#endif
  }

  std::error_code MapImpl(const FileHandle& file, bool writeable,
                          std::uint64_t aligned_offset,
                          std::size_t mapped_length, std::size_t delta,
                          std::size_t view_length) noexcept {
#ifdef _WIN32
    const DWORD protect = writeable ? PAGE_READWRITE : PAGE_READONLY;
    const DWORD access = writeable ? FILE_MAP_WRITE : FILE_MAP_READ;
    const std::uint64_t mapping_size = aligned_offset + mapped_length;

    mapping_handle_ = ::CreateFileMappingW(
        file.Get(), nullptr, protect,
        static_cast<DWORD>(mapping_size >> 32U),
        static_cast<DWORD>(mapping_size & 0xffffffffULL), nullptr);
    if (mapping_handle_ == nullptr) {
      return LastPlatformError();
    }

    void* mapped = ::MapViewOfFile(
        mapping_handle_, access, static_cast<DWORD>(aligned_offset >> 32U),
        static_cast<DWORD>(aligned_offset & 0xffffffffULL), mapped_length);
    if (mapped == nullptr) {
      const auto ec = LastPlatformError();
      ::CloseHandle(mapping_handle_);
      mapping_handle_ = nullptr;
      return ec;
    }

    base_ = mapped;
#else
    const int prot = writeable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    void* mapped = ::mmap(nullptr, mapped_length, prot, MAP_SHARED, file.Get(),
                          static_cast<off_t>(aligned_offset));
    if (mapped == MAP_FAILED) {
      return LastPlatformError();
    }

    base_ = mapped;
#endif

    data_ = static_cast<void*>(static_cast<std::byte*>(base_) + delta);
    offset_ = aligned_offset + delta;
    mapped_length_ = mapped_length;
    view_length_ = view_length;
    writeable_ = writeable;

    return {};
  }

  void MoveFrom(MemoryMap&& other) noexcept {
    base_ = other.base_;
    data_ = other.data_;
    mapped_length_ = other.mapped_length_;
    view_length_ = other.view_length_;
    offset_ = other.offset_;
    writeable_ = other.writeable_;
#ifdef _WIN32
    mapping_handle_ = other.mapping_handle_;
    other.mapping_handle_ = nullptr;
#endif
    other.Reset();
  }

  void Reset() noexcept {
    base_ = nullptr;
    data_ = nullptr;
    mapped_length_ = 0;
    view_length_ = 0;
    offset_ = 0;
    writeable_ = false;
  }

  void* base_{nullptr};
  void* data_{nullptr};
  std::size_t mapped_length_{0};
  std::size_t view_length_{0};
  std::uint64_t offset_{0};
  bool writeable_{false};
#ifdef _WIN32
  HANDLE mapping_handle_{nullptr};
#endif
};

}  // namespace boltdb
