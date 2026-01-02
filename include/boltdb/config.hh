#pragma once

#ifdef PAGE_SIZE
inline constexpr int kPageSize = PAGE_SIZE;
#else
inline constexpr int kPageSize = 4096;
#endif
