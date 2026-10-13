#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "core/scsc_queue.h"

using namespace exchange::core;

struct TestPayload {
  std::uint64_t sequence;
  std::uint64_t payload_data;
};

static_assert(std::is_trivially_copyable_v<TestPayload>);

// 1. Single-threaded basic operations: empty, capacity, size, push, pop, full.
TEST(SPSCQueueTest, BasicPushPopEmptyFull) {
  constexpr std::size_t CAPACITY = 4;
  SPSCQueue<TestPayload, CAPACITY> queue;

  EXPECT_TRUE(queue.empty());
  EXPECT_FALSE(queue.full());
  EXPECT_EQ(queue.size(), 0U);
  EXPECT_EQ(queue.capacity(), CAPACITY);

  // Pop from empty queue returns false.
  TestPayload out{};
  EXPECT_FALSE(queue.pop(out));

  // Push 1 item.
  EXPECT_TRUE(queue.push({.sequence = 1, .payload_data = 100}));
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1U);

  // Push 3 more to reach capacity of 4.
  EXPECT_TRUE(queue.push({.sequence = 2, .payload_data = 200}));
  EXPECT_TRUE(queue.push({.sequence = 3, .payload_data = 300}));
  EXPECT_TRUE(queue.push({.sequence = 4, .payload_data = 400}));

  EXPECT_TRUE(queue.full());
  EXPECT_EQ(queue.size(), 4U);

  // Push to full queue returns false.
  EXPECT_FALSE(queue.push({.sequence = 5, .payload_data = 500}));

  // Pop all items and verify FIFO order.
  EXPECT_TRUE(queue.pop(out));
  EXPECT_EQ(out.sequence, 1U);
  EXPECT_EQ(out.payload_data, 100U);

  EXPECT_TRUE(queue.pop(out));
  EXPECT_EQ(out.sequence, 2U);

  EXPECT_TRUE(queue.pop(out));
  EXPECT_EQ(out.sequence, 3U);

  EXPECT_TRUE(queue.pop(out));
  EXPECT_EQ(out.sequence, 4U);

  EXPECT_TRUE(queue.empty());
  EXPECT_FALSE(queue.pop(out));
}

// 2. Queue saturation rejection preserves integrity of contained items.
TEST(SPSCQueueTest, QueueSaturationRejection) {
  constexpr std::size_t CAPACITY = 8;
  SPSCQueue<uint32_t, CAPACITY> queue;

  for (uint32_t i = 1; i <= CAPACITY; ++i) {
    EXPECT_TRUE(queue.push(i));
  }
  EXPECT_TRUE(queue.full());

  // Subsequent pushes must fail without corrupting existing elements.
  for (uint32_t i = 100; i < 110; ++i) {
    EXPECT_FALSE(queue.push(i));
  }

  // Pop elements and check intact values.
  for (uint32_t i = 1; i <= CAPACITY; ++i) {
    uint32_t val = 0;
    ASSERT_TRUE(queue.pop(val));
    EXPECT_EQ(val, i);
  }
  EXPECT_TRUE(queue.empty());
}

// 3. Ring buffer wrap-around across multiple cycles through capacity.
TEST(SPSCQueueTest, RingBufferWrapAround) {
  constexpr std::size_t CAPACITY = 16;
  SPSCQueue<uint64_t, CAPACITY> queue;

  constexpr uint64_t TOTAL_ITEMS = 100'000;
  for (uint64_t i = 0; i < TOTAL_ITEMS; ++i) {
    ASSERT_TRUE(queue.push(i));
    uint64_t val = 0;
    ASSERT_TRUE(queue.pop(val));
    ASSERT_EQ(val, i);
  }
  EXPECT_TRUE(queue.empty());
}

// 4. Concurrent Producer-Consumer Stress Test:
// Dedicated producer pushes 500,000 items while dedicated consumer pops.
// Verifies lock-free thread safety, zero data loss, zero duplication, and strict FIFO sequence.
TEST(SPSCQueueTest, ConcurrentProducerConsumerStressTest) {
  constexpr std::size_t CAPACITY = 1024;
  constexpr uint64_t MESSAGE_COUNT = 500'000;

  SPSCQueue<TestPayload, CAPACITY> queue;
  std::atomic<bool> producer_done{false};

  std::vector<uint64_t> received_sequences;
  received_sequences.reserve(MESSAGE_COUNT);

  // Producer thread
  std::thread producer([&queue, &producer_done]() {
    for (uint64_t i = 1; i <= MESSAGE_COUNT; ++i) {
      TestPayload payload{.sequence = i, .payload_data = i * 7};
      while (!queue.push(payload)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  // Consumer thread
  std::thread consumer([&queue, &producer_done, &received_sequences]() {
    TestPayload out{};
    while (true) {
      if (queue.pop(out)) {
        received_sequences.push_back(out.sequence);
        if (out.sequence % 7 != 0 && out.payload_data != out.sequence * 7) {
          // Data corruption check
          FAIL() << "Data corruption detected at sequence " << out.sequence;
        }
      } else if (producer_done.load(std::memory_order_acquire)) {
        // Drain any remaining items in queue
        while (queue.pop(out)) {
          received_sequences.push_back(out.sequence);
        }
        break;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(received_sequences.size(), MESSAGE_COUNT);
  for (uint64_t i = 0; i < MESSAGE_COUNT; ++i) {
    ASSERT_EQ(received_sequences[i], i + 1);
  }
}

// 5. Concurrent Burst and Sleep Stress:
// Producer sends in irregular bursts with intermittent yields.
TEST(SPSCQueueTest, ConcurrentBurstAndSleepStress) {
  constexpr std::size_t CAPACITY = 256;
  constexpr uint64_t BURSTS = 500;
  constexpr uint64_t BURST_SIZE = 50;

  SPSCQueue<uint32_t, CAPACITY> queue;
  std::atomic<bool> done{false};
  std::vector<uint32_t> received;

  std::thread producer([&queue, &done]() {
    uint32_t counter = 1;
    for (uint64_t b = 0; b < BURSTS; ++b) {
      for (uint64_t i = 0; i < BURST_SIZE; ++i) {
        while (!queue.push(counter)) {
          std::this_thread::yield();
        }
        ++counter;
      }
      if (b % 50 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
    done.store(true, std::memory_order_release);
  });

  std::thread consumer([&queue, &done, &received]() {
    uint32_t val = 0;
    while (!done.load(std::memory_order_acquire) || !queue.empty()) {
      if (queue.pop(val)) {
        received.push_back(val);
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();

  ASSERT_EQ(received.size(), BURSTS * BURST_SIZE);
  for (std::size_t i = 0; i < received.size(); ++i) {
    ASSERT_EQ(received[i], static_cast<uint32_t>(i + 1));
  }
}
