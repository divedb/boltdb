#pragma once

#include <cstddef>

namespace boltdb {

/// \brief Aligns the given size to the specified alignment.
///
/// \param size      The size to be aligned.
/// \param alignment The alignment boundary.
/// \return          The aligned size which is a multiple of alignment.
/// \note            The alignment must be a power of two.
constexpr size_t AlignTo(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

}  // namespace boltdb