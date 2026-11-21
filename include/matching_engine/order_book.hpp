#pragma once

#include "matching_engine/types.hpp"
#include "matching_engine/order.hpp"
#include "matching_engine/price_level.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>

namespace matching {

struct LevelSnapshot {
    Price price{0};
    Quantity total_volume{0};
    size_t order_count{0};
};

struct BookDepth {
    Symbol symbol{};
    std::vector<LevelSnapshot> bids{};
    std::vector<LevelSnapshot> asks{};
};

struct TopOfBook {
    Symbol symbol{};
    Price best_bid_price{0};
    Quantity best_bid_quantity{0};
    Price best_ask_price{0};
    Quantity best_ask_quantity{0};
    bool has_bid{false};
    bool has_ask{false};

    [[nodiscard]] Price spread() const noexcept {
        if (has_bid && has_ask) {
            return best_ask_price - best_bid_price;
        }
        return 0;
    }
};

class OrderBook {
public:
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>; // Descending: best bid highest
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;    // Ascending: best ask lowest

    struct OrderLocation {
        Side side;
        Price price;
        PriceLevel::OrderIterator iter;
    };

    explicit OrderBook(Symbol symbol = "") : symbol_(std::move(symbol)) {}

    [[nodiscard]] const Symbol& symbol() const noexcept { return symbol_; }

    // Direct access to sides
    [[nodiscard]] const BidMap& bids() const noexcept { return bids_; }
    [[nodiscard]] const AskMap& asks() const noexcept { return asks_; }
    BidMap& bids() noexcept { return bids_; }
    AskMap& asks() noexcept { return asks_; }

    [[nodiscard]] bool has_bids() const noexcept { return !bids_.empty(); }
    [[nodiscard]] bool has_asks() const noexcept { return !asks_.empty(); }

    [[nodiscard]] Price best_bid() const noexcept {
        return bids_.empty() ? INVALID_PRICE : bids_.begin()->first;
    }

    [[nodiscard]] Price best_ask() const noexcept {
        return asks_.empty() ? INVALID_PRICE : asks_.begin()->first;
    }

    // O(1) order lookup
    [[nodiscard]] bool contains(OrderId id) const noexcept {
        return order_index_.find(id) != order_index_.end();
    }

    [[nodiscard]] const Order* find_order(OrderId id) const {
        auto it = order_index_.find(id);
        if (it == order_index_.end()) {
            return nullptr;
        }
        return &(*(it->second.iter));
    }

    // Inserts a resting limit order into the book:
    // O(log M) to find or create the price level in the red-black tree,
    // O(1) to append to the level's FIFO queue,
    // O(1) to index in the hash map.
    PriceLevel::OrderIterator insert_order(Order order);

    // Cancels an order:
    // O(1) hash map lookup,
    // O(1) doubly-linked list removal,
    // O(log M) price level removal if the level becomes empty.
    std::optional<Order> remove_order(OrderId id);

    // Modifies an existing order in the book.
    // If quantity is decreased and price remains unchanged:
    //   Preserves time priority; updates remaining quantity in place in O(1).
    // If quantity increases or price changes:
    //   Loses time priority; removes and re-queues at the tail of the level in O(log M).
    bool modify_order(OrderId id, Quantity new_qty, Price new_price);

    // Snapshots
    [[nodiscard]] TopOfBook get_top_of_book() const;
    [[nodiscard]] BookDepth get_depth(size_t max_depth = 10) const;

    // Checks book integrity/invariants (best_bid < best_ask, volume consistency, index consistency)
    [[nodiscard]] bool check_invariants() const;

    // Total active resting orders
    [[nodiscard]] size_t order_count() const noexcept { return order_index_.size(); }

    // Clear book
    void clear();

private:
    Symbol symbol_{};
    BidMap bids_{};
    AskMap asks_{};
    std::unordered_map<OrderId, OrderLocation> order_index_{};
};

} // namespace matching
