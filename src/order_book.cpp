#include "matching_engine/order_book.hpp"
#include <cassert>

namespace matching {

PriceLevel::OrderIterator OrderBook::insert_order(Order order) {
    OrderId id = order.id;
    Side side = order.side;
    Price price = order.price;

    if (side == Side::Buy) {
        auto [level_it, _] = bids_.try_emplace(price, price);
        PriceLevel::OrderIterator order_it = level_it->second.append(std::move(order));
        order_index_[id] = OrderLocation{side, price, order_it};
        return order_it;
    } else {
        auto [level_it, _] = asks_.try_emplace(price, price);
        PriceLevel::OrderIterator order_it = level_it->second.append(std::move(order));
        order_index_[id] = OrderLocation{side, price, order_it};
        return order_it;
    }
}

std::optional<Order> OrderBook::remove_order(OrderId id) {
    auto index_it = order_index_.find(id);
    if (index_it == order_index_.end()) {
        return std::nullopt;
    }

    const OrderLocation loc = index_it->second;
    Order removed_order = *(loc.iter);

    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        assert(level_it != bids_.end());
        level_it->second.erase(loc.iter);
        if (level_it->second.empty()) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(loc.price);
        assert(level_it != asks_.end());
        level_it->second.erase(loc.iter);
        if (level_it->second.empty()) {
            asks_.erase(level_it);
        }
    }

    order_index_.erase(index_it);
    return removed_order;
}

bool OrderBook::modify_order(OrderId id, Quantity new_qty, Price new_price) {
    auto index_it = order_index_.find(id);
    if (index_it == order_index_.end()) {
        return false;
    }

    OrderLocation& loc = index_it->second;
    Order& order = *(loc.iter);

    Price target_price = (new_price > 0) ? new_price : order.price;
    if (new_qty == 0) {
        remove_order(id);
        return true;
    }

    // Rule: A quantity decrease at the exact same price preserves time priority.
    // Real exchanges (e.g. NASDAQ, CME) maintain queue position when sizing down.
    if (target_price == order.price && new_qty <= order.remaining_quantity) {
        Quantity diff = order.remaining_quantity - new_qty;
        if (loc.side == Side::Buy) {
            bids_[loc.price].adjust_volume(diff, true);
        } else {
            asks_[loc.price].adjust_volume(diff, true);
        }
        order.remaining_quantity = new_qty;
        return true;
    }

    // Rule: Any price change or quantity increase loses time priority.
    // The order must be removed and re-inserted at the tail of the corresponding price level.
    Order updated_order = order;
    updated_order.remaining_quantity = new_qty;
    updated_order.initial_quantity = std::max(updated_order.initial_quantity, new_qty);
    updated_order.price = target_price;
    updated_order.timestamp = current_timestamp_ns(); // Reset timestamp for new priority

    remove_order(id);
    insert_order(std::move(updated_order));
    return true;
}

TopOfBook OrderBook::get_top_of_book() const {
    TopOfBook tob;
    tob.symbol = symbol_;
    if (!bids_.empty()) {
        const auto& [price, level] = *bids_.begin();
        tob.best_bid_price = price;
        tob.best_bid_quantity = level.total_volume();
        tob.has_bid = true;
    }
    if (!asks_.empty()) {
        const auto& [price, level] = *asks_.begin();
        tob.best_ask_price = price;
        tob.best_ask_quantity = level.total_volume();
        tob.has_ask = true;
    }
    return tob;
}

BookDepth OrderBook::get_depth(size_t max_depth) const {
    BookDepth depth;
    depth.symbol = symbol_;

    size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count++ >= max_depth) break;
        depth.bids.push_back(LevelSnapshot{price, level.total_volume(), level.order_count()});
    }

    count = 0;
    for (const auto& [price, level] : asks_) {
        if (count++ >= max_depth) break;
        depth.asks.push_back(LevelSnapshot{price, level.total_volume(), level.order_count()});
    }

    return depth;
}

bool OrderBook::check_invariants() const {
    // Invariant 1: Crossed book check (best bid must be strictly less than best ask)
    if (!bids_.empty() && !asks_.empty()) {
        if (bids_.begin()->first >= asks_.begin()->first) {
            return false;
        }
    }

    // Invariant 2: Hash map count equals sum of all orders in all levels
    size_t total_orders_in_levels = 0;
    for (const auto& [_, level] : bids_) {
        total_orders_in_levels += level.order_count();
        Quantity calc_vol = 0;
        for (const auto& order : level.orders()) {
            calc_vol += order.remaining_quantity;
            if (order.remaining_quantity == 0) return false;
        }
        if (calc_vol != level.total_volume()) return false;
    }
    for (const auto& [_, level] : asks_) {
        total_orders_in_levels += level.order_count();
        Quantity calc_vol = 0;
        for (const auto& order : level.orders()) {
            calc_vol += order.remaining_quantity;
            if (order.remaining_quantity == 0) return false;
        }
        if (calc_vol != level.total_volume()) return false;
    }

    if (total_orders_in_levels != order_index_.size()) {
        return false;
    }

    return true;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
}

} // namespace matching
