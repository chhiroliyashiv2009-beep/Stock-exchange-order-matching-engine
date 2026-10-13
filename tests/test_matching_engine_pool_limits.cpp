#include <gtest/gtest.h>

#include "matching/matching_engine.h"
#include "test_helpers.h"

using namespace exchange;
using namespace exchange::core;
using namespace exchange::matching;
using namespace exchange::test;

// Test fixture with tightly constrained capacity for deterministic pool limit verification.
class EnginePoolLimitsTest : public ::testing::Test {
protected:
  // OrderCapacity = 4, LevelCapacity = 4, EventCapacity = 64, StopCapacity = 4
  MatchingEngine<4, 4, 64, 4> engine;
};

// 1. Order pool exhaustion properly rejects incoming orders without crashing.
TEST_F(EnginePoolLimitsTest, OrderPoolExhaustionRejection) {
  // Fill all 4 slots in the order pool.
  for (uint64_t i = 1; i <= 4; ++i) {
    auto events = engine.process_order(
        make_add_order(i, Side::BUY, to_price(100.0 + i), 10, OrderType::LIMIT, AAPL, i));
    EXPECT_TRUE(has_event_type(events, EventType::ORDER_ACCEPTED));
    EXPECT_FALSE(has_event_type(events, EventType::ORDER_REJECTED));
  }

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->active_order_count(), 4U);

  // 5th order must be rejected with ReasonCode::ORDER_POOL_EXHAUSTED!
  auto events_overflow = engine.process_order(
      make_add_order(5, Side::BUY, to_price(105.0), 10, OrderType::LIMIT, AAPL, 5));

  auto rejects = filter_events(events_overflow, EventType::ORDER_REJECTED);
  ASSERT_EQ(rejects.size(), 1U);
  EXPECT_EQ(rejects[0].payload.order_rejected.order_id, 5U);
  EXPECT_EQ(rejects[0].payload.order_rejected.reason_code, ReasonCode::ORDER_POOL_EXHAUSTED);

  // Pool remains at 4 items.
  EXPECT_EQ(book->active_order_count(), 4U);
}

// 2. Canceling a resting order frees its slot in the pool, allowing new orders.
TEST_F(EnginePoolLimitsTest, CancelOrderFreesPoolSlot) {
  // Fill the pool to capacity (4 orders).
  for (uint64_t i = 1; i <= 4; ++i) {
    engine.process_order(
        make_add_order(i, Side::BUY, to_price(90.0 + i), 10, OrderType::LIMIT, AAPL, i));
  }

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->active_order_count(), 4U);

  // Attempting 5th order fails.
  auto fail_ev = engine.process_order(
      make_add_order(5, Side::BUY, to_price(95.0), 10, OrderType::LIMIT, AAPL, 5));
  EXPECT_TRUE(has_event_type(fail_ev, EventType::ORDER_REJECTED));

  // Cancel order 2 -> Frees a slot!
  auto cancel_ev = engine.process_order(make_cancel_order(2, AAPL, 6));
  EXPECT_TRUE(has_event_type(cancel_ev, EventType::ORDER_CANCELED));
  EXPECT_EQ(book->active_order_count(), 3U);

  // Now, 6th order should succeed!
  auto success_ev = engine.process_order(
      make_add_order(6, Side::BUY, to_price(95.0), 10, OrderType::LIMIT, AAPL, 7));
  EXPECT_TRUE(has_event_type(success_ev, EventType::ORDER_ACCEPTED));
  EXPECT_EQ(book->active_order_count(), 4U);
}

// 3. Fully filling an order deallocates it and frees its slot in the pool.
TEST_F(EnginePoolLimitsTest, FillOrderFreesPoolSlot) {
  // Place 3 resting Sell orders: ids 1, 2, 3 at $100.00 (qty 10 each).
  for (uint64_t i = 1; i <= 3; ++i) {
    engine.process_order(
        make_add_order(i, Side::SELL, to_price(100.0), 10, OrderType::LIMIT, AAPL, i));
  }

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->active_order_count(), 3U);

  // Place 4th order: Buy at $90.00 (resting, pool is now full at 4).
  engine.process_order(
      make_add_order(4, Side::BUY, to_price(90.0), 10, OrderType::LIMIT, AAPL, 4));
  EXPECT_EQ(book->active_order_count(), 4U);

  // Try 5th order: rejected because pool is full.
  auto rejected_ev = engine.process_order(
      make_add_order(5, Side::BUY, to_price(89.0), 10, OrderType::LIMIT, AAPL, 5));
  EXPECT_TRUE(has_event_type(rejected_ev, EventType::ORDER_REJECTED));

  // Now cancel order 4 to make room for an aggressor.
  engine.process_order(make_cancel_order(4, AAPL, 6));
  EXPECT_EQ(book->active_order_count(), 3U);

  // Aggressive Buy for 30 units at $100.00.
  // Consumes and fills orders 1, 2, 3 entirely!
  // Aggressor itself is fully filled (30 units) and retired!
  auto fill_ev = engine.process_order(
      make_add_order(7, Side::BUY, to_price(100.0), 30, OrderType::LIMIT, AAPL, 7));
  auto trades = extract_trades(fill_ev);
  ASSERT_EQ(trades.size(), 3U);

  // All 3 resting sells are filled and retired. Aggressor is filled and retired.
  // Active orders in book must now be ZERO!
  EXPECT_EQ(book->active_order_count(), 0U);
  EXPECT_TRUE(book->order_pool().is_empty());

  // Can now add 4 brand new orders with zero issues.
  for (uint64_t i = 10; i < 14; ++i) {
    auto ev = engine.process_order(
        make_add_order(i, Side::BUY, to_price(50.0), 5, OrderType::LIMIT, AAPL, i));
    EXPECT_TRUE(has_event_type(ev, EventType::ORDER_ACCEPTED));
  }
  EXPECT_EQ(book->active_order_count(), 4U);
}

// 4. Canceling non-existent or inactive orders does not alter memory pool count.
TEST_F(EnginePoolLimitsTest, DoubleCancelOrInvalidOrderNoPoolCorruption) {
  // Place order 1.
  engine.process_order(make_add_order(1, Side::BUY, to_price(100.0), 10, OrderType::LIMIT, AAPL, 1));
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->active_order_count(), 1U);

  // Cancel non-existent order 999 -> ORDER_NOT_FOUND, active count stays 1.
  auto ev1 = engine.process_order(make_cancel_order(999, AAPL, 2));
  auto rej1 = filter_events(ev1, EventType::ORDER_REJECTED);
  ASSERT_EQ(rej1.size(), 1U);
  EXPECT_EQ(rej1[0].payload.order_rejected.reason_code, ReasonCode::ORDER_NOT_FOUND);
  EXPECT_EQ(book->active_order_count(), 1U);

  // Cancel order 1 -> succeeds, active count becomes 0.
  auto ev2 = engine.process_order(make_cancel_order(1, AAPL, 3));
  EXPECT_TRUE(has_event_type(ev2, EventType::ORDER_CANCELED));
  EXPECT_EQ(book->active_order_count(), 0U);

  // Cancel order 1 again -> ORDER_NOT_ACTIVE, active count remains 0 without underflow!
  auto ev3 = engine.process_order(make_cancel_order(1, AAPL, 4));
  auto rej3 = filter_events(ev3, EventType::ORDER_REJECTED);
  ASSERT_EQ(rej3.size(), 1U);
  EXPECT_EQ(rej3[0].payload.order_rejected.reason_code, ReasonCode::ORDER_NOT_ACTIVE);
  EXPECT_EQ(book->active_order_count(), 0U);
}

// 5. High-volume order churn leak verification.
TEST(HighVolumeLeakTest, ChurnLeakCheck) {
  MatchingEngine<16384, 1024, 65536, 1024> engine;
  const auto sym = core::make_symbol("TEST");

  // Run 10,000 add & cancel cycles.
  for (uint64_t i = 1; i <= 10'000; ++i) {
    engine.process_order(make_add_order(i, Side::BUY, to_price(100.0), 10, OrderType::LIMIT, sym, i));
    engine.process_order(make_cancel_order(i, sym, i));
  }

  const auto *book = engine.find_book(sym);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->active_order_count(), 0U);
  EXPECT_TRUE(book->order_pool().is_empty());
}
