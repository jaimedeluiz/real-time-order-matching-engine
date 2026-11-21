#pragma once

#include "matching_engine/types.hpp"
#include "matching_engine/order.hpp"
#include "matching_engine/order_book.hpp"
#include "matching_engine/events.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

namespace matching {

class MatchingEngine {
public:
    explicit MatchingEngine(IEventListener* listener = nullptr,
                           STPPolicy stp_policy = STPPolicy::CancelIncoming)
        : listener_(listener), stp_policy_(stp_policy) {}

    void set_event_listener(IEventListener* listener) noexcept {
        listener_ = listener;
    }

    void set_stp_policy(STPPolicy policy) noexcept {
        stp_policy_ = policy;
    }

    [[nodiscard]] STPPolicy stp_policy() const noexcept {
        return stp_policy_;
    }

    // Register / configure symbols
    bool register_symbol(const Symbol& symbol);
    [[nodiscard]] bool has_symbol(const Symbol& symbol) const;
    OrderBook* get_order_book(const Symbol& symbol);
    const OrderBook* get_order_book(const Symbol& symbol) const;

    // Core Matching API
    // 1. Submit New Order
    bool submit_order(Order order);

    // 2. Cancel Order by OrderId
    bool cancel_order(OrderId order_id);

    // 3. Modify Order quantity and/or price
    bool modify_order(OrderId order_id, Quantity new_qty, Price new_price = 0);

    // Query & Snapshot API
    [[nodiscard]] TopOfBook get_top_of_book(const Symbol& symbol) const;
    [[nodiscard]] BookDepth get_depth(const Symbol& symbol, size_t max_depth = 10) const;
    [[nodiscard]] const Order* find_order(OrderId order_id) const;

    // Verification
    [[nodiscard]] bool check_invariants() const;

    // Reset state
    void reset();

    // Statistics
    [[nodiscard]] size_t total_orders_processed() const noexcept { return orders_processed_; }
    [[nodiscard]] size_t total_trades_generated() const noexcept { return trades_generated_; }
    [[nodiscard]] Quantity total_volume_traded() const noexcept { return volume_traded_; }

private:
    bool validate_order(const Order& order, RejectReason& reason) const;
    bool can_fully_fill_fok(const OrderBook& book, const Order& order) const;
    void match_order(OrderBook& book, Order& order);

    void notify_accepted(const AcceptedEvent& ev) {
        if (listener_) listener_->on_accepted(ev);
    }
    void notify_rejected(const RejectedEvent& ev) {
        if (listener_) listener_->on_rejected(ev);
    }
    void notify_trade(const TradeEvent& ev) {
        trades_generated_++;
        volume_traded_ += ev.quantity;
        if (listener_) listener_->on_trade(ev);
    }
    void notify_canceled(const CanceledEvent& ev) {
        if (listener_) listener_->on_canceled(ev);
    }
    void notify_modified(const ModifiedEvent& ev) {
        if (listener_) listener_->on_modified(ev);
    }

private:
    IEventListener* listener_{nullptr};
    STPPolicy stp_policy_{STPPolicy::CancelIncoming};

    std::unordered_map<Symbol, OrderBook> books_{};
    std::unordered_map<OrderId, Symbol> order_symbol_map_{};

    size_t orders_processed_{0};
    size_t trades_generated_{0};
    Quantity volume_traded_{0};
};

} // namespace matching
