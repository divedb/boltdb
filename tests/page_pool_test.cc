#include "boltdb/page_pool.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace boltdb {

namespace {

class PagePoolTest : public ::testing::Test {
 protected:
  using PagePool = boltdb::PagePool<64>;
};

TEST_F(PagePoolTest, BasicAllocationAndDeallocation) {
  PagePool& pool = PagePool::Instance();
  PageData* page = pool.Get();

  ASSERT_NE(page, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(page) % alignof(PageData), 0);

  std::memset(page->data, 0x42, 128);
  pool.Put(page);

  // After putting the page back, it should be reusable.
  PageData* page2 = pool.Get();
  EXPECT_EQ(page, page2) << "Page should be reused from pool";
  pool.Put(page2);
}

TEST_F(PagePoolTest, ConcurrentAccess) {
  constexpr int kNumThreads = 8;
  constexpr int kOperationsPerThread = 10000;

  PagePool& pool = PagePool::Instance();
  std::atomic<int> completed_threads{0};
  std::vector<std::thread> threads;
  std::atomic<int> get_count{0};
  std::atomic<int> put_count{0};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      std::mt19937 rng(std::random_device{}());
      std::uniform_int_distribution<> dist(1, 5);
      std::vector<PageData*> pages;
      pages.reserve(10);

      for (int j = 0; j < kOperationsPerThread; ++j) {
        if (pages.empty() || dist(rng) > 2) {
          auto* page = pool.Get();
          EXPECT_NE(page, nullptr);

          *reinterpret_cast<int*>(page->data) = i;
          pages.push_back(page);
          get_count.fetch_add(1, std::memory_order_relaxed);
        } else {
          auto* page = pages.back();
          EXPECT_EQ(*reinterpret_cast<int*>(page->data), i);

          pool.Put(page);
          pages.pop_back();
          put_count.fetch_add(1, std::memory_order_relaxed);
        }

        // Sleep occasionally to increase thread interleaving.
        if (dist(rng) == 1) {
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      }

      for (auto* page : pages) {
        pool.Put(page);
        put_count.fetch_add(1, std::memory_order_relaxed);
      }

      completed_threads.fetch_add(1, std::memory_order_relaxed);
    });
  }

  for (auto& t : threads) t.join();

  EXPECT_EQ(completed_threads.load(), kNumThreads);
  EXPECT_EQ(get_count.load(), put_count.load());
}

TEST_F(PagePoolTest, HighContention) {
  constexpr int kNumThreads = 16;
  constexpr int kOperations = 500;
  std::vector<std::thread> threads;
  PagePool& pool = PagePool::Instance();
  std::atomic<int> errors{0};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, thread_id = i]() {
      for (int j = 0; j < kOperations; ++j) {
        auto* page = pool.Get();

        if (!page) {
          errors.fetch_add(1);
          continue;
        }

        std::memcpy(page->data, &thread_id, sizeof(thread_id));
        pool.Put(page);

        if (j % 100 == 0) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto& t : threads) t.join();

  EXPECT_EQ(errors.load(), 0);
}

}  // namespace

}  // namespace boltdb