#pragma once

#include "matching_engine/types.hpp"
#include <vector>
#include <iostream>
#include <sstream>

namespace matching {

struct AcceptedEvent {
    OrderId order_id{INVALID_ORDER_ID};
    Timestamp timestamp{0};
};

struct RejectedEvent {
    OrderId order_id{INVALID_ORDER_ID};
    RejectReason reason{RejectReason::None};
    Timestamp timestamp{0};
};

struct TradeEvent {
    Price price{0};
    Quantity quantity{0};
    OrderId maker_order_id{INVALID_ORDER_ID};
    OrderId taker_order_id{INVALID_ORDER_ID};
    OrderId buy_order_id{INVALID_ORDER_ID};
    OrderId sell_order_id{INVALID_ORDER_ID};
    TraderId maker_trader_id{};
    TraderId taker_trader_id{};
    Timestamp timestamp{0};
};

struct CanceledEvent {
    OrderId order_id{INVALID_ORDER_ID};
    Quantity remaining_quantity{0};
    Timestamp timestamp{0};
};

struct ModifiedEvent {
    OrderId order_id{INVALID_ORDER_ID};
    Quantity new_quantity{0};
    Price new_price{0};
    Timestamp timestamp{0};
};

// Abstract Event Listener Interface for dynamic/poly callbacks.
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void on_accepted(const AcceptedEvent& event) = 0;
    virtual void on_rejected(const RejectedEvent& event) = 0;
    virtual void on_trade(const TradeEvent& event) = 0;
    virtual void on_canceled(const CanceledEvent& event) = 0;
    virtual void on_modified(const ModifiedEvent& event) = 0;
};

// Zero-overhead Null Listener (useful for pure matching benchmarks)
struct NullEventListener : public IEventListener {
    void on_accepted(const AcceptedEvent&) override {}
    void on_rejected(const RejectedEvent&) override {}
    void on_trade(const TradeEvent&) override {}
    void on_canceled(const CanceledEvent&) override {}
    void on_modified(const ModifiedEvent&) override {}
};

// Recording Event Listener for unit testing, determinism verification, and replay
class RecordingEventListener : public IEventListener {
public:
    std::vector<AcceptedEvent> accepted_events;
    std::vector<RejectedEvent> rejected_events;
    std::vector<TradeEvent> trade_events;
    std::vector<CanceledEvent> canceled_events;
    std::vector<ModifiedEvent> modified_events;

    void on_accepted(const AcceptedEvent& event) override {
        accepted_events.push_back(event);
    }
    void on_rejected(const RejectedEvent& event) override {
        rejected_events.push_back(event);
    }
    void on_trade(const TradeEvent& event) override {
        trade_events.push_back(event);
    }
    void on_canceled(const CanceledEvent& event) override {
        canceled_events.push_back(event);
    }
    void on_modified(const ModifiedEvent& event) override {
        modified_events.push_back(event);
    }

    void clear() {
        accepted_events.clear();
        rejected_events.clear();
        trade_events.clear();
        canceled_events.clear();
        modified_events.clear();
    }
};

// Formatted Stream Listener (for CLI / terminal logging)
class StreamEventListener : public IEventListener {
private:
    std::ostream& os_;
public:
    explicit StreamEventListener(std::ostream& os = std::cout) : os_(os) {}

    void on_accepted(const AcceptedEvent& event) override {
        os_ << "[EVENT ACCEPTED] id=" << event.order_id
            << " ts=" << event.timestamp << "\n";
    }

    void on_rejected(const RejectedEvent& event) override {
        os_ << "[EVENT REJECTED] id=" << event.order_id
            << " reason=" << reject_reason_to_string(event.reason)
            << " ts=" << event.timestamp << "\n";
    }

    void on_trade(const TradeEvent& event) override {
        os_ << "\033[1;32m[EVENT TRADE]\033[0m "
            << event.quantity << " @ " << event.price
            << " (Buy #" << event.buy_order_id << " vs Sell #" << event.sell_order_id
            << ") [Maker: " << event.maker_order_id << " (" << event.maker_trader_id
            << "), Taker: " << event.taker_order_id << " (" << event.taker_trader_id
            << ")] ts=" << event.timestamp << "\n";
    }

    void on_canceled(const CanceledEvent& event) override {
        os_ << "[EVENT CANCELED] id=" << event.order_id
            << " remaining=" << event.remaining_quantity
            << " ts=" << event.timestamp << "\n";
    }

    void on_modified(const ModifiedEvent& event) override {
        os_ << "[EVENT MODIFIED] id=" << event.order_id
            << " new_qty=" << event.new_quantity
            << " new_price=" << event.new_price
            << " ts=" << event.timestamp << "\n";
    }
};

} // namespace matching
