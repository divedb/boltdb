
//===- AllocatorBase.h - Simple memory allocation abstraction ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines MallocAllocator. MallocAllocator conforms to the LLVM
/// "Allocator" concept which consists of an Allocate method accepting a size
/// and alignment, and a Deallocate accepting a pointer and size. Further, the
/// LLVM "Allocator" concept has overloads of Allocate and Deallocate for
/// setting size and alignment based on the final type. These overloads are
/// typically provided by a base class template \c AllocatorBase.
///
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>
#include <utility>

namespace boltdb {

template <typename DerivedT>
class AllocatorBase {
 public:
  /// Allocate \a size bytes of \a alignment aligned memory. This method
  /// must be implemented by \c DerivedT.
  void *Allocate(size_t size, size_t alignment) {
    return static_cast<DerivedT *>(this)->Allocate(size, alignment);
  }

  /// Deallocate \a ptr to \a size bytes of memory allocated by this
  /// allocator.
  void Deallocate(const void *ptr, size_t size, size_t alignment) {
    return static_cast<DerivedT *>(this)->Deallocate(ptr, size, alignment);
  }

  /// Allocate space for a sequence of objects without constructing them.
  template <typename T>
  T *Allocate(size_t num = 1) {
    return static_cast<T *>(Allocate(num * sizeof(T), alignof(T)));
  }

  /// Deallocate space for a sequence of objects without constructing them.
  template <typename T>
  std::enable_if_t<!std::is_same_v<std::remove_cv_t<T>, void>, void> Deallocate(
      T *ptr, size_t num = 1) {
    Deallocate(static_cast<const void *>(ptr), num * sizeof(T), alignof(T));
  }
};

class MallocAllocator : public AllocatorBase<MallocAllocator> {
 public:
  void Reset() {}

  void *Allocate(size_t Size, size_t Alignment) {
    return allocate_buffer(Size, Alignment);
  }

  // Pull in base class overloads.
  using AllocatorBase<MallocAllocator>::Allocate;

  void Deallocate(const void *Ptr, size_t Size, size_t Alignment) {
    deallocate_buffer(const_cast<void *>(Ptr), Size, Alignment);
  }

  // Pull in base class overloads.
  using AllocatorBase<MallocAllocator>::Deallocate;

  void PrintStats() const {}
};

namespace detail {

template <typename Alloc>
class AllocatorHolder : Alloc {
 public:
  AllocatorHolder() = default;
  AllocatorHolder(const Alloc &A) : Alloc(A) {}
  AllocatorHolder(Alloc &&A) : Alloc(std::move(A)) {}
  Alloc &getAllocator() { return *this; }
  const Alloc &getAllocator() const { return *this; }
};

template <typename Alloc>
class AllocatorHolder<Alloc &> {
  Alloc &A;

 public:
  AllocatorHolder(Alloc &A) : A(A) {}
  Alloc &getAllocator() { return A; }
  const Alloc &getAllocator() const { return A; }
};

}  // namespace detail

}  // namespace boltdb

#endif  // LLVM_SUPPORT_ALLOCATORBASE_H
Generated on  for LLVM by doxygen 1.14.0