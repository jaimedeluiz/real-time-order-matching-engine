#pragma once

#include "matching_engine/types.hpp"
#include "matching_engine/order.hpp"
#include <list>
#include <cstddef>

namespace matching {

// Represents an aggregated price level in the order book.
// Stores all resting orders at this exact price in a FIFO doubly-linked list (std::list)
// to strictly guarantee price-time priority: orders that arrived earlier are matched first.
//
// Time Complexity:
// - append_order: O(1)
// - remove_order: O(1) given an iterator
// - front:        O(1)
// - pop_front:    O(1)
class PriceLevel {
public:
    using OrderQueue = std::list<Order>;
    using OrderIterator = OrderQueue::iterator;
    using ConstOrderIterator = OrderQueue::const_iterator;

    explicit PriceLevel(Price price = 0) noexcept
        : price_(price), total_volume_(0) {}

    [[nodiscard]] Price price() const noexcept { return price_; }
    [[nodiscard]] Quantity total_volume() const noexcept { return total_volume_; }
    [[nodiscard]] size_t order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] bool empty() const noexcept { return orders_.empty(); }

    Order& front() noexcept { return orders_.front(); }
    const Order& front() const noexcept { return orders_.front(); }

    // Appends an order to the back of the queue (enforcing time priority)
    OrderIterator append(const Order& order) {
        orders_.push_back(order);
        total_volume_ += order.remaining_quantity;
        return std::prev(orders_.end());
    }

    OrderIterator append(Order&& order) {
        total_volume_ += order.remaining_quantity;
        orders_.push_back(std::move(order));
        return std::prev(orders_.end());
    }

    // Removes an order from anywhere in the queue in O(1) time
    void erase(OrderIterator it) {
        total_volume_ -= it->remaining_quantity;
        orders_.erase(it);
    }

    // Adjusts volume when an order at this level is partially filled or modified
    void adjust_volume(Quantity delta, bool is_decrease) noexcept {
        if (is_decrease) {
            total_volume_ = (total_volume_ >= delta) ? (total_volume_ - delta) : 0;
        } else {
            total_volume_ += delta;
        }
    }

    OrderQueue& orders() noexcept { return orders_; }
    const OrderQueue& orders() const noexcept { return orders_; }

private:
    Price price_{0};
    Quantity total_volume_{0};
    OrderQueue orders_{};
};

} // namespace matching
