#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "core/memory_pool.h"

using namespace exchange::core;

struct PoolTestItem {
  uint64_t id{0};
  uint64_t val{0};
  char buffer[48]{};
};

static_assert(sizeof(PoolTestItem) >= sizeof(void *));

// 1. Allocation up to full capacity succeeds.
TEST(MemoryPoolTest, AllocationUpToCapacity) {
  constexpr std::size_t CAPACITY = 16;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  EXPECT_EQ(pool.capacity(), CAPACITY);
  EXPECT_EQ(pool.allocated_count(), 0U);
  EXPECT_EQ(pool.available_count(), CAPACITY);
  EXPECT_TRUE(pool.is_empty());
  EXPECT_FALSE(pool.is_full());

  std::array<PoolTestItem *, CAPACITY> ptrs{};
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    ptrs[i] = pool.allocate();
    ASSERT_NE(ptrs[i], nullptr);
    ptrs[i]->id = i + 1;
    EXPECT_EQ(pool.allocated_count(), i + 1);
  }

  EXPECT_TRUE(pool.is_full());
  EXPECT_FALSE(pool.is_empty());
  EXPECT_EQ(pool.allocated_count(), CAPACITY);
  EXPECT_EQ(pool.available_count(), 0U);

  // Clean up
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    pool.deallocate(ptrs[i]);
  }
  EXPECT_TRUE(pool.is_empty());
}

// 2. Allocation limit exhaustion returns nullptr without crashing.
TEST(MemoryPoolTest, AllocationLimitExhaustion) {
  constexpr std::size_t CAPACITY = 8;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  std::vector<PoolTestItem *> allocated;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    auto *ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    allocated.push_back(ptr);
  }

  // Next allocation must return nullptr (pool is exhausted).
  EXPECT_EQ(pool.allocate(), nullptr);
  EXPECT_EQ(pool.allocate(), nullptr);
  EXPECT_EQ(pool.allocated_count(), CAPACITY);

  // Free one item: allocation should succeed again!
  pool.deallocate(allocated.back());
  allocated.pop_back();
  EXPECT_EQ(pool.allocated_count(), CAPACITY - 1);

  auto *reallocated = pool.allocate();
  ASSERT_NE(reallocated, nullptr);
  EXPECT_EQ(pool.allocated_count(), CAPACITY);

  // Clean up
  allocated.push_back(reallocated);
  for (auto *p : allocated) {
    pool.deallocate(p);
  }
  EXPECT_EQ(pool.allocated_count(), 0U);
}

// 3. Deallocation and recycling: free-list recycling works without leaks.
TEST(MemoryPoolTest, DeallocationAndRecycling) {
  constexpr std::size_t CAPACITY = 4;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  auto *a = pool.allocate();
  auto *b = pool.allocate();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(pool.allocated_count(), 2U);

  pool.deallocate(a);
  EXPECT_EQ(pool.allocated_count(), 1U);

  // Re-allocating should reuse the freed slot.
  auto *c = pool.allocate();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c, a); // Intrusive LIFO free-list returns the most recently deallocated slot
  EXPECT_EQ(pool.allocated_count(), 2U);

  pool.deallocate(b);
  pool.deallocate(c);
  EXPECT_TRUE(pool.is_empty());
}

// 4. Leak prevention: verify allocated_count is strictly 0 after full lifecycle.
TEST(MemoryPoolTest, LeakPreventionZeroAllocatedCount) {
  constexpr std::size_t CAPACITY = 64;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  std::vector<PoolTestItem *> items;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    items.push_back(pool.allocate());
  }
  EXPECT_EQ(pool.allocated_count(), CAPACITY);

  for (auto *p : items) {
    pool.deallocate(p);
  }

  EXPECT_EQ(pool.allocated_count(), 0U);
  EXPECT_EQ(pool.available_count(), CAPACITY);
  EXPECT_TRUE(pool.is_empty());
}

// 5. Arbitrary order deallocation does not corrupt free-list pointers.
TEST(MemoryPoolTest, ArbitraryOrderDeallocation) {
  constexpr std::size_t CAPACITY = 32;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  std::vector<PoolTestItem *> items;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    items.push_back(pool.allocate());
  }

  // Deallocate in reverse order
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    pool.deallocate(*it);
  }
  EXPECT_EQ(pool.allocated_count(), 0U);

  // Re-allocate all items again to verify free-list is intact without cycles
  items.clear();
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    auto *p = pool.allocate();
    ASSERT_NE(p, nullptr);
    items.push_back(p);
  }
  EXPECT_EQ(pool.allocated_count(), CAPACITY);

  // Deallocate alternating elements (even indices first, then odd indices)
  for (std::size_t i = 0; i < CAPACITY; i += 2) {
    pool.deallocate(items[i]);
  }
  EXPECT_EQ(pool.allocated_count(), CAPACITY / 2);

  for (std::size_t i = 1; i < CAPACITY; i += 2) {
    pool.deallocate(items[i]);
  }
  EXPECT_EQ(pool.allocated_count(), 0U);
}

// 6. Multiple churn cycles: 1,000 cycles of allocate all / deallocate all.
TEST(MemoryPoolTest, MultipleChurnCycles) {
  constexpr std::size_t CAPACITY = 128;
  MemoryPool<PoolTestItem, CAPACITY> pool;

  std::array<PoolTestItem *, CAPACITY> ptrs{};

  for (int cycle = 0; cycle < 1000; ++cycle) {
    for (std::size_t i = 0; i < CAPACITY; ++i) {
      ptrs[i] = pool.allocate();
      ASSERT_NE(ptrs[i], nullptr);
    }
    EXPECT_EQ(pool.allocated_count(), CAPACITY);

    for (std::size_t i = 0; i < CAPACITY; ++i) {
      pool.deallocate(ptrs[i]);
    }
    EXPECT_EQ(pool.allocated_count(), 0U);
  }
}
