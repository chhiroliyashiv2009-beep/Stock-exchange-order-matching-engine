#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/types.h"
#include "matching/events.h"
#include "matching/matching_engine.h"

namespace exchange::test {

inline const core::Symbol AAPL = core::make_symbol("AAPL");
inline const core::Symbol MSFT = core::make_symbol("MSFT");

constexpr core::Price to_price(double dollars) noexcept {
  return static_cast<core::Price>(dollars * core::PRICE_SCALE);
}

inline matching::InboundOrder make_add_order(
    core::OrderId id, core::Side side, core::Price price, core::Quantity qty,
    core::OrderType type = core::OrderType::LIMIT,
    core::Symbol symbol = AAPL, core::Timestamp timestamp = 1,
    core::ParticipantId participant = 1, core::Price trigger_price = 0,
    core::Quantity display_qty = 0) {
  return matching::InboundOrder{
      .action = matching::RequestAction::ADD,
      .symbol = symbol,
      .order_id = id,
      .side = side,
      .price = price,
      .qty = qty,
      .order_type = type,
      .timestamp = timestamp,
      .participant_id = participant,
      .trigger_price = trigger_price,
      .display_qty = display_qty,
  };
}

inline matching::InboundOrder make_cancel_order(
    core::OrderId id, core::Symbol symbol = AAPL,
    core::Timestamp timestamp = 1) {
  return matching::InboundOrder{
      .action = matching::RequestAction::CANCEL,
      .symbol = symbol,
      .order_id = id,
      .timestamp = timestamp,
  };
}

inline matching::InboundOrder make_modify_order(
    core::OrderId id, core::Quantity new_qty, core::Price new_price,
    core::Symbol symbol = AAPL, core::Timestamp timestamp = 1) {
  return matching::InboundOrder{
      .action = matching::RequestAction::MODIFY,
      .symbol = symbol,
      .order_id = id,
      .price = new_price,
      .qty = new_qty,
      .timestamp = timestamp,
  };
}

inline std::vector<matching::Event> filter_events(
    std::span<const matching::Event> events, matching::EventType type) {
  std::vector<matching::Event> result;
  for (const auto &ev : events) {
    if (ev.type == type) {
      result.push_back(ev);
    }
  }
  return result;
}

inline std::vector<matching::Trade> extract_trades(
    std::span<const matching::Event> events) {
  std::vector<matching::Trade> trades;
  for (const auto &ev : events) {
    if (ev.type == matching::EventType::TRADE) {
      trades.push_back(ev.payload.trade);
    }
  }
  return trades;
}

inline bool has_event_type(std::span<const matching::Event> events,
                           matching::EventType type) {
  return std::any_of(events.begin(), events.end(),
                     [type](const auto &e) { return e.type == type; });
}

} // namespace exchange::test
