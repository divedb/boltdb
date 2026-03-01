#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace boltdb {

inline std::size_t GetPageSize() noexcept {
  static std::once_flag once;
  static std::size_t cached = 0;

  std::call_once(once, []() noexcept {
    std::size_t ps = 0;

#if defined(_WIN32)
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    ps = static_cast<std::size_t>(si.dwPageSize);
#else
    long v = -1;

#ifdef _SC_PAGESIZE
      v = ::sysconf(_SC_PAGESIZE);
#endif
#if defined(_SC_PAGE_SIZE)
      if (v <= 0) v = ::sysconf(_SC_PAGE_SIZE);
#endif

    if (v > 0) {
      ps = static_cast<std::size_t>(v);
    } else {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
        int gp = ::getpagesize();

        if (gp > 0) ps = static_cast<std::size_t>(gp);
#endif
    }
#endif

    if (ps == 0) ps = 4096;

    cached = ps;
  });

  return cached;
}

}  // namespace boltdb