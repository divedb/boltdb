#pragma once

#include <array>
#include <atomic>
#include <cstddef>  // std::byte
#include <cstring>  // std::memcpy

#include "boltdb/config.hh"

namespace boltdb {

struct alignas(64) PageData {
  union {
    PageData* next;
    std::byte data[kPageSize];
  };
};

template <size_t N = 32>
class PagePool {
  static void Reset(PageData*) noexcept {}

 public:
  using ResetFn = void (*)(PageData*) noexcept;

  static PagePool& Instance() {
    static PagePool instance;

    return instance;
  }

  PagePool(const PagePool&) = delete;
  PagePool& operator=(const PagePool&) = delete;
  PagePool(PagePool&&) = delete;
  PagePool& operator=(PagePool&&) = delete;

  ~PagePool() { DrainVictimList(); }

  /// \brief Get a page from the pool.
  ///
  /// If the local cache is empty, try to steal from the global victim list.
  /// If that also fails, allocate a new page.
  ///
  /// \return A pointer to a PageData.
  /// \note   The returned page is uninitialized and must returned to the pool
  ///         using Put() when no longer needed.
  PageData* Get() {
    if (auto* page = TryAcquireFromLocalCache()) return page;
    if (auto stolen = StealFromVictimList()) return stolen;

    return new PageData;
  }

  /// \brief Return a page to the pool.
  ///
  /// \param page The page to return. If nullptr, the function does nothing.
  void Put(PageData* page) {
    if (!page) return;

    reset_fn_(page);

    if (ReturnToLocalCache(page)) [[likely]] {
      return;
    }

    // Local cache is full, add to global victim list.
    AddToVictimList(page);
  }

 private:
  explicit PagePool(ResetFn reset = &Reset) : reset_fn_(reset) {}

  struct ThreadLocalCache {
    ThreadLocalCache() = default;
    ~ThreadLocalCache() noexcept { Drain(); }
    ThreadLocalCache(const ThreadLocalCache&) = delete;
    ThreadLocalCache& operator=(const ThreadLocalCache&) = delete;
    ThreadLocalCache(ThreadLocalCache&&) = delete;
    ThreadLocalCache& operator=(ThreadLocalCache&&) = delete;

    [[nodiscard]] bool IsEmpty() const { return top == 0; }
    [[nodiscard]] bool IsFull() const { return top >= N; }

    /// \brief Try to pop a page from the local cache stack.
    ///
    /// \return A pointer to a PageData if available; otherwise nullptr.
    [[nodiscard]] PageData* TryPop() {
      if (IsEmpty()) return nullptr;

      return stack[--top];
    }

    /// \brief Try to push a page onto the local cache stack.
    ///
    /// \param p The page to push.
    /// \return  True if the page was successfully pushed; otherwise false.
    [[nodiscard]] bool TryPush(PageData* p) {
      if (IsFull()) return false;

      stack[top] = p;
      top++;

      return true;
    }

    /// \brief Drain the local cache, deleting all pages.
    void Drain() noexcept {
      for (size_t i = 0; i < top; ++i) delete stack[i];

      top = 0;
    }

    std::array<PageData*, N> stack;
    size_t top = 0;
  };

  /// \brief Try to acquire a page from the local thread cache.
  ///
  /// \return A pointer to a PageData if available; otherwise nullptr.
  static PageData* TryAcquireFromLocalCache() {
    auto& cache = local_cache_;

    return cache.TryPop();
  }

  /// \brief Return a page to the local thread cache.
  ///
  /// \param p The page to return.
  /// \return  True if the page was successfully returned; otherwise false.
  static bool ReturnToLocalCache(PageData* p) {
    auto& cache = local_cache_;

    return cache.TryPush(p);
  }

  /// \brief Steal a page from the global victim list.
  ///
  /// \return A pointer to a PageData if available; otherwise nullptr.
  PageData* StealFromVictimList() {
    PageData* head = victim_head_.load(std::memory_order_acquire);

    while (head) {
      PageData* next = nullptr;
      std::memcpy(&next, head->data, sizeof(PageData*));

      if (victim_head_.compare_exchange_weak(head, next,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        return head;
      }
    }

    return nullptr;
  }

  /// \brief Add a page to the global victim list.
  ///
  /// \param p The page to add.
  void AddToVictimList(PageData* p) {
    PageData* head = victim_head_.load(std::memory_order_acquire);

    do {
      std::memcpy(p->data, &head, sizeof(PageData*));
    } while (!victim_head_.compare_exchange_weak(
        head, p, std::memory_order_release, std::memory_order_relaxed));
  }

  void DrainVictimList() noexcept {
    PageData* head = victim_head_.exchange(nullptr, std::memory_order_acquire);

    while (head) {
      PageData* next = head->next;
      delete head;
      head = next;
    }
  }

  static thread_local ThreadLocalCache local_cache_;

  std::atomic<PageData*> victim_head_{};
  ResetFn reset_fn_;
};

template <size_t N>
thread_local typename PagePool<N>::ThreadLocalCache PagePool<N>::local_cache_;

}  // namespace boltdb