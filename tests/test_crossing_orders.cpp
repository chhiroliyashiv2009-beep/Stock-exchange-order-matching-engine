#include <gtest/gtest.h>

#include "matching/matching_engine.h"
#include "test_helpers.h"

using namespace exchange;
using namespace exchange::core;
using namespace exchange::matching;
using namespace exchange::test;

class CrossingOrdersTest : public ::testing::Test {
protected:
  MatchingEngine<1024, 128, 4096, 128> engine;
};

// 1. Immediate crossing when inbound Bid exceeds resting Ask.
TEST_F(CrossingOrdersTest, ImmediateCrossingOverlappingBidAsk) {
  // Resting Ask at $100.00 for 50 units.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 50, OrderType::LIMIT, AAPL, 10));

  // Inbound Buy at $102.00 for 30 units (crossing).
  // Matches 30 @ $100.00 (resting ask price).
  auto events = engine.process_order(make_add_order(2, Side::BUY, to_price(102.0), 30, OrderType::LIMIT, AAPL, 20));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].buy_order_id, 2U);
  EXPECT_EQ(trades[0].sell_order_id, 1U);
  EXPECT_EQ(trades[0].price, to_price(100.0));
  EXPECT_EQ(trades[0].qty, 30U);

  // Inbound Buy at $105.00 for 35 units (crosses and consumes remaining 20 of ask, rests 15 @ $105).
  auto events2 = engine.process_order(make_add_order(3, Side::BUY, to_price(105.0), 35, OrderType::LIMIT, AAPL, 30));
  auto trades2 = extract_trades(events2);
  ASSERT_EQ(trades2.size(), 1U);
  EXPECT_EQ(trades2[0].price, to_price(100.0));
  EXPECT_EQ(trades2[0].qty, 20U);

  // Book invariant: remaining Buy rests at $105.00, Ask side is empty.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_price(), to_price(105.0));
  EXPECT_EQ(book->best_bid_quantity(), 15U);
  EXPECT_EQ(book->best_ask(), nullptr);
}

// 2. Book spread invariant: bid and ask never overlap after crossing orders execute.
TEST_F(CrossingOrdersTest, BookSpreadInvariantAfterCross) {
  // Bids at $98.00 and $99.00
  engine.process_order(make_add_order(1, Side::BUY, to_price(98.0), 20, OrderType::LIMIT, AAPL, 1));
  engine.process_order(make_add_order(2, Side::BUY, to_price(99.0), 20, OrderType::LIMIT, AAPL, 2));

  // Asks at $101.00 and $102.00
  engine.process_order(make_add_order(3, Side::SELL, to_price(101.0), 20, OrderType::LIMIT, AAPL, 3));
  engine.process_order(make_add_order(4, Side::SELL, to_price(102.0), 20, OrderType::LIMIT, AAPL, 4));

  // Inbound Sell at $98.50 for 30 units (crosses with Bid at $99.00).
  // Matches 20 @ $99.00, then rests 10 @ $98.50.
  engine.process_order(make_add_order(5, Side::SELL, to_price(98.5), 30, OrderType::LIMIT, AAPL, 5));

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  ASSERT_NE(book->best_bid(), nullptr);
  ASSERT_NE(book->best_ask(), nullptr);

  // Invariant: Best Bid ($98.00) < Best Ask ($98.50)
  EXPECT_LT(book->best_bid_price(), book->best_ask_price());
  EXPECT_EQ(book->best_bid_price(), to_price(98.0));
  EXPECT_EQ(book->best_ask_price(), to_price(98.5));
}

// 3. Post-Only order is rejected if it would cross the spread.
TEST_F(CrossingOrdersTest, PostOnlyRejectionOnCross) {
  // Resting Ask at $100.00.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 50, OrderType::LIMIT, AAPL, 10));

  // Post-Only Buy at $100.00 would cross/match with resting Ask at $100.00 -> Rejected!
  auto events = engine.process_order(
      make_add_order(2, Side::BUY, to_price(100.0), 20, OrderType::POST_ONLY, AAPL, 20));

  auto rejections = filter_events(events, EventType::ORDER_REJECTED);
  ASSERT_EQ(rejections.size(), 1U);
  EXPECT_EQ(rejections[0].payload.order_rejected.order_id, 2U);
  EXPECT_EQ(rejections[0].payload.order_rejected.reason_code, ReasonCode::POST_ONLY_WOULD_CROSS);

  // Post-Only Buy above resting Ask ($101.00) also rejected.
  auto events2 = engine.process_order(
      make_add_order(3, Side::BUY, to_price(101.0), 20, OrderType::POST_ONLY, AAPL, 30));
  auto rejections2 = filter_events(events2, EventType::ORDER_REJECTED);
  ASSERT_EQ(rejections2.size(), 1U);
  EXPECT_EQ(rejections2[0].payload.order_rejected.reason_code, ReasonCode::POST_ONLY_WOULD_CROSS);

  // Verify Ask at $100.00 was never touched.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_ask_quantity(), 50U);
}

// 4. Post-Only order is accepted and rests when it does not cross.
TEST_F(CrossingOrdersTest, PostOnlyAcceptedWhenNotCrossing) {
  // Resting Ask at $100.00.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 50, OrderType::LIMIT, AAPL, 10));

  // Post-Only Buy at $99.00 does not cross -> Accepted and rests.
  auto events = engine.process_order(
      make_add_order(2, Side::BUY, to_price(99.0), 25, OrderType::POST_ONLY, AAPL, 20));

  EXPECT_FALSE(has_event_type(events, EventType::ORDER_REJECTED));
  EXPECT_TRUE(has_event_type(events, EventType::ORDER_ACCEPTED));
  EXPECT_TRUE(has_event_type(events, EventType::ORDER_RESTED));

  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid_price(), to_price(99.0));
  EXPECT_EQ(book->best_bid_quantity(), 25U);
}

// 5. Immediate-Or-Cancel (IOC) crossing: fills available, cancels unfulfilled remainder.
TEST_F(CrossingOrdersTest, IOC_CrossingPartialFillRemainderCanceled) {
  // Resting Ask at $100.00 for 40 units.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 40, OrderType::LIMIT, AAPL, 10));

  // Inbound IOC Buy at $100.00 for 100 units.
  // Fills 40 units, cancels remaining 60 units (does NOT rest).
  auto events = engine.process_order(
      make_add_order(2, Side::BUY, to_price(100.0), 100, OrderType::IOC, AAPL, 20));

  auto trades = extract_trades(events);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].qty, 40U);

  // Must emit ORDER_CANCELED for the remaining 60 units.
  auto cancels = filter_events(events, EventType::ORDER_CANCELED);
  ASSERT_EQ(cancels.size(), 1U);
  EXPECT_EQ(cancels[0].payload.order_canceled.order_id, 2U);
  EXPECT_EQ(cancels[0].payload.order_canceled.canceled_qty, 60U);

  // Order book must have neither bid nor ask remaining.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_bid(), nullptr);
  EXPECT_EQ(book->best_ask(), nullptr);
}

// 6. Fill-Or-Kill (FOK) crossing: executes fully if enough liquidity, rejects if insufficient.
TEST_F(CrossingOrdersTest, FOK_CrossingFullFillVsInsufficientLiquidity) {
  // Resting Ask for 50 units at $100.00.
  engine.process_order(make_add_order(1, Side::SELL, to_price(100.0), 50, OrderType::LIMIT, AAPL, 10));

  // FOK Buy for 60 units (more than 50 available) -> Rejected with FOK_INSUFFICIENT_LIQUIDITY.
  auto events_fail = engine.process_order(
      make_add_order(2, Side::BUY, to_price(100.0), 60, OrderType::FOK, AAPL, 20));
  auto rejections = filter_events(events_fail, EventType::ORDER_REJECTED);
  ASSERT_EQ(rejections.size(), 1U);
  EXPECT_EQ(rejections[0].payload.order_rejected.reason_code, ReasonCode::FOK_INSUFFICIENT_LIQUIDITY);

  // Ask of 50 units remains untouched.
  const auto *book = engine.find_book(AAPL);
  ASSERT_NE(book, nullptr);
  EXPECT_EQ(book->best_ask_quantity(), 50U);

  // FOK Buy for 50 units (exactly matches available liquidity) -> Executes fully.
  auto events_pass = engine.process_order(
      make_add_order(3, Side::BUY, to_price(100.0), 50, OrderType::FOK, AAPL, 30));
  auto trades = extract_trades(events_pass);
  ASSERT_EQ(trades.size(), 1U);
  EXPECT_EQ(trades[0].qty, 50U);
  EXPECT_EQ(book->best_ask(), nullptr);
}
