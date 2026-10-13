#include <gtest/gtest.h>

#include "matching/matching_engine.h"
#include "test_helpers.h"

using namespace exchange;
using namespace exchange::core;
using namespace exchange::matching;
using namespace exchange::test;

class PriceTimePriorityTest : public ::testing::Test {
protected:
  MatchingEngine<1024, 128, 4096, 128> engine;
};

// 1. Strict price priority for Bids: incoming Sell matches higher bids first.
TEST_F(PriceTimePriorityTest, BidsPricePriority) {
  // Place three Buy orders at different prices.
  // Order 1 at $100.00
  // Order 2 at $102.00 (highest bid)
  // Order 3 at $101.00
  engine.process_order(make_add_order(1, Side::BUY, to_price(100.0), 10, OrderType::LIMIT, AAPL, 100));
  engine.process_order(make_add_order(2, Side::BUY, to_price(102.0), 10, OrderType::LIMIT, AAPL, 200));
  engine.process_order(make_add_order(3, Side::BUY, to_price(101.0), 10, OrderType::LIMIT, AAPL, 300));

  // Inbound Sell order at $99.00 for 15 units.
  // Must match Order 2 (10 @ $102) first, then Order 3 (5 @ $101).
  auto events = engine.process_order(make_add_order(4, Side::SELL, to_price(99.0), 15, OrderType::LIMIT, AAPL, 400));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);

  // Trade 1: matched with Order 2 at $102.00
  EXPECT_EQ(trades[0].buy_order_id, 2U);
  EXPECT_EQ(trades[0].sell_order_id, 4U);
  EXPECT_EQ(trades[0].price, to_price(102.0));
  EXPECT_EQ(trades[0].qty, 10U);

  // Trade 2: matched with Order 3 at $101.00
  EXPECT_EQ(trades[1].buy_order_id, 3U);
  EXPECT_EQ(trades[1].sell_order_id, 4U);
  EXPECT_EQ(trades[1].price, to_price(101.0));
  EXPECT_EQ(trades[1].qty, 5U);

  // Verify Order 1 (at $100.00) is untouched, and Order 3 has 5 units remaining.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_price(), to_price(101.0));
  EXPECT_EQ(book->best_bid_quantity(), 5U);
}

// 2. Strict price priority for Asks: incoming Buy matches lower asks first.
TEST_F(PriceTimePriorityTest, AsksPricePriority) {
  // Place three Sell orders at different prices.
  // Order 1 at $105.00
  // Order 2 at $103.00 (lowest ask)
  // Order 3 at $104.00
  engine.process_order(make_add_order(1, Side::SELL, to_price(105.0), 20, OrderType::LIMIT, AAPL, 100));
  engine.process_order(make_add_order(2, Side::SELL, to_price(103.0), 20, OrderType::LIMIT, AAPL, 200));
  engine.process_order(make_add_order(3, Side::SELL, to_price(104.0), 20, OrderType::LIMIT, AAPL, 300));

  // Inbound aggressive Buy order at $106.00 for 30 units.
  // Must match Order 2 (20 @ $103) first, then Order 3 (10 @ $104).
  auto events = engine.process_order(make_add_order(4, Side::BUY, to_price(106.0), 30, OrderType::LIMIT, AAPL, 400));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);

  EXPECT_EQ(trades[0].buy_order_id, 4U);
  EXPECT_EQ(trades[0].sell_order_id, 2U);
  EXPECT_EQ(trades[0].price, to_price(103.0));
  EXPECT_EQ(trades[0].qty, 20U);

  EXPECT_EQ(trades[1].buy_order_id, 4U);
  EXPECT_EQ(trades[1].sell_order_id, 3U);
  EXPECT_EQ(trades[1].price, to_price(104.0));
  EXPECT_EQ(trades[1].qty, 10U);

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_ask_price(), to_price(104.0));
  EXPECT_EQ(book->best_ask_quantity(), 10U);
}

// 3. Strict time priority (FIFO queue placement) at the same price on Buy side.
TEST_F(PriceTimePriorityTest, TimePriorityFIFO_SamePrice_BuySide) {
  const Price price = to_price(100.0);

  // Place Order 1 at t=100, Order 2 at t=200, Order 3 at t=300, all at same price.
  engine.process_order(make_add_order(1, Side::BUY, price, 10, OrderType::LIMIT, AAPL, 100));
  engine.process_order(make_add_order(2, Side::BUY, price, 10, OrderType::LIMIT, AAPL, 200));
  engine.process_order(make_add_order(3, Side::BUY, price, 10, OrderType::LIMIT, AAPL, 300));

  // Inbound Sell for 15 units.
  // Must fully fill Order 1 (10 units), then partially fill Order 2 (5 units).
  // Order 3 must not be touched.
  auto events = engine.process_order(make_add_order(4, Side::SELL, price, 15, OrderType::LIMIT, AAPL, 400));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);

  EXPECT_EQ(trades[0].buy_order_id, 1U);
  EXPECT_EQ(trades[0].sell_order_id, 4U);
  EXPECT_EQ(trades[0].qty, 10U);

  EXPECT_EQ(trades[1].buy_order_id, 2U);
  EXPECT_EQ(trades[1].sell_order_id, 4U);
  EXPECT_EQ(trades[1].qty, 5U);

  // Next inbound Sell for 10 units must fill the remaining 5 units of Order 2,
  // then 5 units of Order 3.
  auto events2 = engine.process_order(make_add_order(5, Side::SELL, price, 10, OrderType::LIMIT, AAPL, 500));
  auto trades2 = extract_trades(events2);
  ASSERT_EQ(trades2.size(), 2U);

  EXPECT_EQ(trades2[0].buy_order_id, 2U);
  EXPECT_EQ(trades2[0].sell_order_id, 5U);
  EXPECT_EQ(trades2[0].qty, 5U);

  EXPECT_EQ(trades2[1].buy_order_id, 3U);
  EXPECT_EQ(trades2[1].sell_order_id, 5U);
  EXPECT_EQ(trades2[1].qty, 5U);
}

// 4. Strict time priority (FIFO queue placement) at the same price on Sell side.
TEST_F(PriceTimePriorityTest, TimePriorityFIFO_SamePrice_SellSide) {
  const Price price = to_price(150.0);

  // Place Sell 10 at t=1, Sell 20 at t=2, Sell 30 at t=3, all at $150.00.
  engine.process_order(make_add_order(10, Side::SELL, price, 25, OrderType::LIMIT, AAPL, 1));
  engine.process_order(make_add_order(20, Side::SELL, price, 25, OrderType::LIMIT, AAPL, 2));
  engine.process_order(make_add_order(30, Side::SELL, price, 25, OrderType::LIMIT, AAPL, 3));

  // Inbound Buy for 40 units.
  // Fills Sell 10 (25 units), then Sell 20 (15 units).
  auto events = engine.process_order(make_add_order(40, Side::BUY, price, 40, OrderType::LIMIT, AAPL, 4));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 2U);

  EXPECT_EQ(trades[0].sell_order_id, 10U);
  EXPECT_EQ(trades[0].qty, 25U);

  EXPECT_EQ(trades[1].sell_order_id, 20U);
  EXPECT_EQ(trades[1].qty, 15U);

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  // Remaining volume at $150 should be 10 (from Sell 20) + 25 (from Sell 30) = 35.
  EXPECT_EQ(book->best_ask_quantity(), 35U);
}

// 5. Passive order price protection (execution occurs at resting passive price).
TEST_F(PriceTimePriorityTest, PassivePriceImprovementExecution) {
  // Resting Ask at $100.00.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 50, OrderType::LIMIT, AAPL, 10));

  // Aggressive Buy willing to pay up to $105.00.
  // Execution MUST occur at the passive resting price $100.00 (price improvement for buyer).
  auto events = engine.process_order(make_add_order(2, Side::BUY, to_price(105.0), 30, OrderType::LIMIT, AAPL, 20));
  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].price, to_price(100.0));
  EXPECT_EQ(trades[0].qty, 30U);

  // Resting Bid at $95.00.
  engine.process_order(make_add_order(3, Side::BUY, to_price(95.0), 50, OrderType::LIMIT, AAPL, 30));

  // Aggressive Sell willing to sell down to $90.00.
  // Execution MUST occur at the passive resting price $95.00 (price improvement for seller).
  auto events2 = engine.process_order(make_add_order(4, Side::SELL, to_price(90.0), 20, OrderType::LIMIT, AAPL, 40));
  auto trades2 = extract_trades(events2);
  ASSERT_EQ(trades2.size(), 1U);
  EXPECT_EQ(trades2[0].price, to_price(95.0));
  EXPECT_EQ(trades2[0].qty, 20U);
}

// 6. Multi-level sweep in exact price priority order.
TEST_F(PriceTimePriorityTest, MultiplePriceLevelsSweepInPriceOrder) {
  // Place asks at $101, $102, $103, $104 with 10 units each.
  for (int p = 101; p <= 104; ++p) {
    engine.process_order(make_add_order(static_cast<OrderId>(p), Side::SELL, to_price(p), 10, OrderType::LIMIT, AAPL, p));
  }

  // Aggressive Buy for 35 units at $105.
  // Should sweep $101 (10), $102 (10), $103 (10), and partially fill $104 (5).
  auto events = engine.process_order(make_add_order(999, Side::BUY, to_price(105.0), 35, OrderType::LIMIT, AAPL, 200));
  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 4U);

  EXPECT_EQ(trades[0].price, to_price(101.0));
  EXPECT_EQ(trades[0].qty, 10U);

  EXPECT_EQ(trades[1].price, to_price(102.0));
  EXPECT_EQ(trades[1].qty, 10U);

  EXPECT_EQ(trades[2].price, to_price(103.0));
  EXPECT_EQ(trades[2].qty, 10U);

  EXPECT_EQ(trades[3].price, to_price(104.0));
  EXPECT_EQ(trades[3].qty, 5U);

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_ask_price(), to_price(104.0));
  EXPECT_EQ(book->best_ask_quantity(), 5U);
}
