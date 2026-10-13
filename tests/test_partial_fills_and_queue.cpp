#include <gtest/gtest.h>

#include "matching/matching_engine.h"
#include "test_helpers.h"

using namespace exchange;
using namespace exchange::core;
using namespace exchange::matching;
using namespace exchange::test;

class PartialFillsAndQueueTest : public ::testing::Test {
protected:
  MatchingEngine<1024, 128, 4096, 128> engine;
};

// 1. Passive order partial fill keeps its position at the head of the queue.
TEST_F(PartialFillsAndQueueTest, PassivePartialFillHeadRetention) {
  const Price price = to_price(50.0);

  // Order 1 has 100 units at $50.00.
  // Order 2 has 50 units at $50.00 behind Order 1.
  engine.process_order(make_add_order(1, Side::BUY, price, 100, OrderType::LIMIT, AAPL, 10));
  engine.process_order(make_add_order(2, Side::BUY, price, 50, OrderType::LIMIT, AAPL, 20));

  // Inbound Sell 3 for 40 units partially fills Order 1.
  auto events = engine.process_order(make_add_order(3, Side::SELL, price, 40, OrderType::LIMIT, AAPL, 30));

  // Verify events: Trade and OrderPartiallyFilled for Order 1.
  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].buy_order_id, 1U);
  EXPECT_EQ(trades[0].qty, 40U);

  auto part_fills = filter_events(events, EventType::ORDER_PARTIALLY_FILLED);
  ASSERT_FALSE(part_fills.empty());
  EXPECT_EQ(part_fills[0].payload.order_partially_filled.order_id, 1U);
  EXPECT_EQ(part_fills[0].payload.order_partially_filled.remaining_qty, 60U);

  // Now, incoming Sell 4 for 30 units MUST match the remaining volume of Order 1, NOT Order 2!
  auto events2 = engine.process_order(make_add_order(4, Side::SELL, price, 30, OrderType::LIMIT, AAPL, 40));
  auto trades2 = extract_trades(events2);
  ASSERT_EQ(trades2.size(), 1U);
  EXPECT_EQ(trades2[0].buy_order_id, 1U);
  EXPECT_EQ(trades2[0].qty, 30U);

  // Remaining at price level 50: Order 1 has 30, Order 2 has 50. Total = 80.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_quantity(), 80U);

  // Inbound Sell 5 for 40 units finishes Order 1 (30) and touches Order 2 for 10.
  auto events3 = engine.process_order(make_add_order(5, Side::SELL, price, 40, OrderType::LIMIT, AAPL, 50));
  auto trades3 = extract_trades(events3);
  ASSERT_EQ(trades3.size(), 2U);
  EXPECT_EQ(trades3[0].buy_order_id, 1U);
  EXPECT_EQ(trades3[0].qty, 30U);
  EXPECT_EQ(trades3[1].buy_order_id, 2U);
  EXPECT_EQ(trades3[1].qty, 10U);

  // Remaining at price level 50: Order 2 has 40.
  EXPECT_EQ(book->best_bid_quantity(), 40U);
}

// 2. Aggressor partial fill: remainder is placed into the order book queue as resting order.
TEST_F(PartialFillsAndQueueTest, AggressorPartialFillAndResting) {
  const Price price = to_price(100.0);

  // Resting Sell for 30 units at $100.00.
  engine.process_order(make_add_order(1, Side::SELL, price, 30, OrderType::LIMIT, AAPL, 10));

  // Inbound Buy for 100 units at $100.00.
  // Should match 30 units, and the remaining 70 units should rest as a Bid at $100.00.
  auto events = engine.process_order(make_add_order(2, Side::BUY, price, 100, OrderType::LIMIT, AAPL, 20));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].qty, 30U);

  // Must have an ORDER_RESTED event for the remaining 70 units of Order 2.
  auto rested_events = filter_events(events, EventType::ORDER_RESTED);
  ASSERT_EQ(rested_events.size(), 1U);
  EXPECT_EQ(rested_events[0].payload.order_rested.order_id, 2U);
  EXPECT_EQ(rested_events[0].payload.order_rested.qty, 70U);
  EXPECT_EQ(rested_events[0].payload.order_rested.side, Side::BUY);

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_price(), price);
  EXPECT_EQ(book->best_bid_quantity(), 70U);
  EXPECT_EQ(book->best_ask(), nullptr);
}

// 3. Large aggressor sweeps multiple levels, remaining volume rests at limit price.
TEST_F(PartialFillsAndQueueTest, AggressorMultiLevelPartialFillResting) {
  // Asks: 10 @ $101, 15 @ $102, 20 @ $103 (total = 45 units)
  engine.process_order(make_add_order(1, Side::SELL, to_price(101.0), 10, OrderType::LIMIT, AAPL, 10));
  engine.process_order(make_add_order(2, Side::SELL, to_price(102.0), 15, OrderType::LIMIT, AAPL, 20));
  engine.process_order(make_add_order(3, Side::SELL, to_price(103.0), 20, OrderType::LIMIT, AAPL, 30));

  // Aggressor Buy for 60 units with limit $104.00.
  // Consumes all 45 ask units and rests 15 units at $104.00.
  auto events = engine.process_order(make_add_order(4, Side::BUY, to_price(104.0), 60, OrderType::LIMIT, AAPL, 40));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 3U);
  EXPECT_EQ(trades[0].qty, 10U);
  EXPECT_EQ(trades[1].qty, 15U);
  EXPECT_EQ(trades[2].qty, 20U);

  auto rested_events = filter_events(events, EventType::ORDER_RESTED);
  ASSERT_EQ(rested_events.size(), 1U);
  EXPECT_EQ(rested_events[0].payload.order_rested.order_id, 4U);
  EXPECT_EQ(rested_events[0].payload.order_rested.qty, 15U);
  EXPECT_EQ(rested_events[0].payload.order_rested.price, to_price(104.0));

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_price(), to_price(104.0));
  EXPECT_EQ(book->best_bid_quantity(), 15U);
  EXPECT_EQ(book->best_ask(), nullptr);
}

// 4. Modifying order to reduce quantity preserves queue priority.
TEST_F(PartialFillsAndQueueTest, ModifyOrderReduceQuantityPreservesPriority) {
  const Price price = to_price(200.0);

  // Order 1 for 50 units, Order 2 for 50 units at same price.
  engine.process_order(make_add_order(1, Side::BUY, price, 50, OrderType::LIMIT, AAPL, 10));
  engine.process_order(make_add_order(2, Side::BUY, price, 50, OrderType::LIMIT, AAPL, 20));

  // Reduce Order 1 quantity to 20 units.
  auto mod_events = engine.process_order(make_modify_order(1, 20, price, AAPL, 30));
  auto red_events = filter_events(mod_events, EventType::ORDER_REDUCED);
  ASSERT_EQ(red_events.size(), 1U);
  EXPECT_EQ(red_events[0].payload.order_reduced.order_id, 1U);
  EXPECT_EQ(red_events[0].payload.order_reduced.new_qty, 20U);

  // Inbound Sell for 30 units:
  // Must execute 20 against Order 1, and 10 against Order 2 (Order 1 maintained priority!).
  auto events = engine.process_order(make_add_order(3, Side::SELL, price, 30, OrderType::LIMIT, AAPL, 40));
  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);
  EXPECT_EQ(trades[0].buy_order_id, 1U);
  EXPECT_EQ(trades[0].qty, 20U);
  EXPECT_EQ(trades[1].buy_order_id, 2U);
  EXPECT_EQ(trades[1].qty, 10U);
}

// 5. Modifying order to increase quantity loses queue priority (cancel and re-insert).
TEST_F(PartialFillsAndQueueTest, ModifyOrderIncreaseQuantityLosesPriority) {
  const Price price = to_price(300.0);

  // Order 1 for 20 units, Order 2 for 20 units at same price.
  engine.process_order(make_add_order(1, Side::BUY, price, 20, OrderType::LIMIT, AAPL, 10));
  engine.process_order(make_add_order(2, Side::BUY, price, 20, OrderType::LIMIT, AAPL, 20));

  // Modify Order 1 to increase quantity from 20 to 50.
  // Order 1 is canceled and re-inserted behind Order 2.
  engine.process_order(make_modify_order(1, 50, price, AAPL, 30));

  // Inbound Sell for 30 units:
  // Must execute 20 against Order 2 first, then 10 against Order 1!
  auto events = engine.process_order(make_add_order(3, Side::SELL, price, 30, OrderType::LIMIT, AAPL, 40));
  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);
  EXPECT_EQ(trades[0].buy_order_id, 2U);
  EXPECT_EQ(trades[0].qty, 20U);
  EXPECT_EQ(trades[1].buy_order_id, 1U);
  EXPECT_EQ(trades[1].qty, 10U);
}
