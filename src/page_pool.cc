#include "boltdb/page_pool.hh"

namespace boltdb {

template <size_t N>
thread_local typename PagePool<N>::ThreadLocalCache PagePool<N>::local_cache_;

}
