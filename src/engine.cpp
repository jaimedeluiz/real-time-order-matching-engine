#include "matching_engine/engine.hpp"
#include <algorithm>

namespace matching {

bool MatchingEngine::register_symbol(const Symbol& symbol) {
    if (symbol.empty() || books_.find(symbol) != books_.end()) {
        return false;
    }
    books_.emplace(symbol, OrderBook(symbol));
    return true;
}

bool MatchingEngine::has_symbol(const Symbol& symbol) const {
    return books_.find(symbol) != books_.end();
}

OrderBook* MatchingEngine::get_order_book(const Symbol& symbol) {
    auto it = books_.find(symbol);
    return (it != books_.end()) ? &(it->second) : nullptr;
}

const OrderBook* MatchingEngine::get_order_book(const Symbol& symbol) const {
    auto it = books_.find(symbol);
    return (it != books_.end()) ? &(it->second) : nullptr;
}

bool MatchingEngine::validate_order(const Order& order, RejectReason& reason) const {
    if (order.id == INVALID_ORDER_ID) {
        reason = RejectReason::OrderNotFound;
        return false;
    }

    if (order.initial_quantity == 0 || order.remaining_quantity == 0) {
        reason = RejectReason::InvalidQuantity;
        return false;
    }

    if (order.type != OrderType::Market && order.price <= 0) {
        reason = RejectReason::InvalidPrice;
        return false;
    }

    // Check duplicate order ID
    if (order_symbol_map_.find(order.id) != order_symbol_map_.end()) {
        reason = RejectReason::DuplicateOrderId;
        return false;
    }

    // Symbol check
    if (!order.symbol.empty() && books_.find(order.symbol) == books_.end()) {
        reason = RejectReason::SymbolNotFound;
        return false;
    }

    reason = RejectReason::None;
    return true;
}

bool MatchingEngine::can_fully_fill_fok(const OrderBook& book, const Order& order) const {
    Quantity available_volume = 0;

    if (order.side == Side::Buy) {
        for (const auto& [price, level] : book.asks()) {
            if (order.type != OrderType::Market && price > order.price) {
                break; // Beyond limit price
            }
            for (const auto& resting : level.orders()) {
                // If STP applies, skip or handle accordingly
                if (stp_policy_ != STPPolicy::None && !order.trader_id.empty() &&
                    order.trader_id == resting.trader_id) {
                    continue;
                }
                available_volume += resting.remaining_quantity;
                if (available_volume >= order.remaining_quantity) {
                    return true;
                }
            }
        }
    } else {
        for (const auto& [price, level] : book.bids()) {
            if (order.type != OrderType::Market && price < order.price) {
                break; // Below limit price
            }
            for (const auto& resting : level.orders()) {
                if (stp_policy_ != STPPolicy::None && !order.trader_id.empty() &&
                    order.trader_id == resting.trader_id) {
                    continue;
                }
                available_volume += resting.remaining_quantity;
                if (available_volume >= order.remaining_quantity) {
                    return true;
                }
            }
        }
    }

    return available_volume >= order.remaining_quantity;
}

bool MatchingEngine::submit_order(Order order) {
    orders_processed_++;
    if (order.timestamp == 0) {
        order.timestamp = current_timestamp_ns();
    }
    if (order.remaining_quantity == 0 && order.initial_quantity > 0) {
        order.remaining_quantity = order.initial_quantity;
    }

    // Auto-register symbol if not present
    if (!order.symbol.empty() && books_.find(order.symbol) == books_.end()) {
        register_symbol(order.symbol);
    }

    RejectReason reason = RejectReason::None;
    if (!validate_order(order, reason)) {
        notify_rejected(RejectedEvent{order.id, reason, order.timestamp});
        return false;
    }

    OrderBook& book = books_[order.symbol];

    // Check Post-Only crossing
    if (order.type == OrderType::PostOnly) {
        if (order.side == Side::Buy && book.has_asks() && order.price >= book.best_ask()) {
            notify_rejected(RejectedEvent{order.id, RejectReason::PostOnlyCrossed, order.timestamp});
            return false;
        }
        if (order.side == Side::Sell && book.has_bids() && order.price <= book.best_bid()) {
            notify_rejected(RejectedEvent{order.id, RejectReason::PostOnlyCrossed, order.timestamp});
            return false;
        }
    }

    // Check Market order against empty book
    if (order.type == OrderType::Market) {
        if ((order.side == Side::Buy && !book.has_asks()) ||
            (order.side == Side::Sell && !book.has_bids())) {
            notify_rejected(RejectedEvent{order.id, RejectReason::EmptyBookForMarket, order.timestamp});
            return false;
        }
    }

    // Check FOK liquidity
    if (order.type == OrderType::FOK) {
        if (!can_fully_fill_fok(book, order)) {
            notify_rejected(RejectedEvent{order.id, RejectReason::InsufficientLiquidity, order.timestamp});
            return false;
        }
    }

    // Order accepted
    notify_accepted(AcceptedEvent{order.id, order.timestamp});

    // Run matching
    match_order(book, order);
    return true;
}

void MatchingEngine::match_order(OrderBook& book, Order& order) {
    if (order.side == Side::Buy) {
        while (order.remaining_quantity > 0 && book.has_asks()) {
            auto ask_level_it = book.asks().begin();
            Price best_ask_price = ask_level_it->first;

            // If limit order and limit price < best ask, no match
            if (order.type != OrderType::Market && order.price < best_ask_price) {
                break;
            }

            PriceLevel& level = ask_level_it->second;
            auto& order_queue = level.orders();
            if (order_queue.empty()) {
                book.asks().erase(ask_level_it);
                continue;
            }

            Order& resting = order_queue.front();

            // Self-Trade Prevention check
            if (stp_policy_ != STPPolicy::None && !order.trader_id.empty() &&
                order.trader_id == resting.trader_id) {
                if (stp_policy_ == STPPolicy::CancelIncoming) {
                    notify_canceled(CanceledEvent{order.id, order.remaining_quantity, current_timestamp_ns()});
                    order.remaining_quantity = 0;
                    return;
                } else if (stp_policy_ == STPPolicy::CancelResting) {
                    OrderId resting_id = resting.id;
                    Quantity resting_rem = resting.remaining_quantity;
                    book.remove_order(resting_id);
                    order_symbol_map_.erase(resting_id);
                    notify_canceled(CanceledEvent{resting_id, resting_rem, current_timestamp_ns()});
                    continue; // Try matching next resting order
                } else if (stp_policy_ == STPPolicy::CancelBoth) {
                    OrderId resting_id = resting.id;
                    Quantity resting_rem = resting.remaining_quantity;
                    book.remove_order(resting_id);
                    order_symbol_map_.erase(resting_id);
                    notify_canceled(CanceledEvent{resting_id, resting_rem, current_timestamp_ns()});
                    notify_canceled(CanceledEvent{order.id, order.remaining_quantity, current_timestamp_ns()});
                    order.remaining_quantity = 0;
                    return;
                }
            }

            // Execute fill at the passive (resting) order's price
            Quantity match_qty = std::min(order.remaining_quantity, resting.remaining_quantity);
            Price trade_price = resting.price;
            Timestamp now = current_timestamp_ns();

            order.remaining_quantity -= match_qty;
            order.filled_quantity += match_qty;
            resting.remaining_quantity -= match_qty;
            resting.filled_quantity += match_qty;

            level.adjust_volume(match_qty, true);

            notify_trade(TradeEvent{
                .price = trade_price,
                .quantity = match_qty,
                .maker_order_id = resting.id,
                .taker_order_id = order.id,
                .buy_order_id = order.id,
                .sell_order_id = resting.id,
                .maker_trader_id = resting.trader_id,
                .taker_trader_id = order.trader_id,
                .timestamp = now
            });

            if (resting.remaining_quantity == 0) {
                OrderId resting_id = resting.id;
                book.remove_order(resting_id);
                order_symbol_map_.erase(resting_id);
            }
        }
    } else { // Side::Sell
        while (order.remaining_quantity > 0 && book.has_bids()) {
            auto bid_level_it = book.bids().begin();
            Price best_bid_price = bid_level_it->first;

            if (order.type != OrderType::Market && order.price > best_bid_price) {
                break;
            }

            PriceLevel& level = bid_level_it->second;
            auto& order_queue = level.orders();
            if (order_queue.empty()) {
                book.bids().erase(bid_level_it);
                continue;
            }

            Order& resting = order_queue.front();

            // Self-Trade Prevention check
            if (stp_policy_ != STPPolicy::None && !order.trader_id.empty() &&
                order.trader_id == resting.trader_id) {
                if (stp_policy_ == STPPolicy::CancelIncoming) {
                    notify_canceled(CanceledEvent{order.id, order.remaining_quantity, current_timestamp_ns()});
                    order.remaining_quantity = 0;
                    return;
                } else if (stp_policy_ == STPPolicy::CancelResting) {
                    OrderId resting_id = resting.id;
                    Quantity resting_rem = resting.remaining_quantity;
                    book.remove_order(resting_id);
                    order_symbol_map_.erase(resting_id);
                    notify_canceled(CanceledEvent{resting_id, resting_rem, current_timestamp_ns()});
                    continue;
                } else if (stp_policy_ == STPPolicy::CancelBoth) {
                    OrderId resting_id = resting.id;
                    Quantity resting_rem = resting.remaining_quantity;
                    book.remove_order(resting_id);
                    order_symbol_map_.erase(resting_id);
                    notify_canceled(CanceledEvent{resting_id, resting_rem, current_timestamp_ns()});
                    notify_canceled(CanceledEvent{order.id, order.remaining_quantity, current_timestamp_ns()});
                    order.remaining_quantity = 0;
                    return;
                }
            }

            Quantity match_qty = std::min(order.remaining_quantity, resting.remaining_quantity);
            Price trade_price = resting.price;
            Timestamp now = current_timestamp_ns();

            order.remaining_quantity -= match_qty;
            order.filled_quantity += match_qty;
            resting.remaining_quantity -= match_qty;
            resting.filled_quantity += match_qty;

            level.adjust_volume(match_qty, true);

            notify_trade(TradeEvent{
                .price = trade_price,
                .quantity = match_qty,
                .maker_order_id = resting.id,
                .taker_order_id = order.id,
                .buy_order_id = resting.id,
                .sell_order_id = order.id,
                .maker_trader_id = resting.trader_id,
                .taker_trader_id = order.trader_id,
                .timestamp = now
            });

            if (resting.remaining_quantity == 0) {
                OrderId resting_id = resting.id;
                book.remove_order(resting_id);
                order_symbol_map_.erase(resting_id);
            }
        }
    }

    // Post-matching handling
    if (order.remaining_quantity > 0) {
        if (order.type == OrderType::Limit || order.type == OrderType::PostOnly) {
            // Remainder rests on the book
            order.status = (order.filled_quantity > 0) ? OrderStatus::PartiallyFilled : OrderStatus::New;
            book.insert_order(order);
            order_symbol_map_[order.id] = order.symbol;
        } else {
            // Market, IOC, or FOK: unfilled remainder must NEVER rest on book
            notify_canceled(CanceledEvent{order.id, order.remaining_quantity, current_timestamp_ns()});
            order.remaining_quantity = 0;
            order.status = (order.filled_quantity > 0) ? OrderStatus::PartiallyFilled : OrderStatus::Canceled;
        }
    } else {
        order.status = OrderStatus::Filled;
    }
}

bool MatchingEngine::cancel_order(OrderId order_id) {
    auto it = order_symbol_map_.find(order_id);
    if (it == order_symbol_map_.end()) {
        notify_rejected(RejectedEvent{order_id, RejectReason::OrderNotFound, current_timestamp_ns()});
        return false;
    }

    const Symbol& symbol = it->second;
    OrderBook& book = books_[symbol];
    auto removed_opt = book.remove_order(order_id);
    if (!removed_opt.has_value()) {
        order_symbol_map_.erase(it);
        notify_rejected(RejectedEvent{order_id, RejectReason::OrderNotFound, current_timestamp_ns()});
        return false;
    }

    order_symbol_map_.erase(it);
    notify_canceled(CanceledEvent{order_id, removed_opt->remaining_quantity, current_timestamp_ns()});
    return true;
}

bool MatchingEngine::modify_order(OrderId order_id, Quantity new_qty, Price new_price) {
    auto it = order_symbol_map_.find(order_id);
    if (it == order_symbol_map_.end()) {
        notify_rejected(RejectedEvent{order_id, RejectReason::OrderNotFound, current_timestamp_ns()});
        return false;
    }

    const Symbol& symbol = it->second;
    OrderBook& book = books_[symbol];

    if (!book.modify_order(order_id, new_qty, new_price)) {
        notify_rejected(RejectedEvent{order_id, RejectReason::OrderNotFound, current_timestamp_ns()});
        return false;
    }

    if (new_qty == 0) {
        order_symbol_map_.erase(it);
        notify_canceled(CanceledEvent{order_id, 0, current_timestamp_ns()});
    } else {
        notify_modified(ModifiedEvent{order_id, new_qty, new_price, current_timestamp_ns()});
    }
    return true;
}

TopOfBook MatchingEngine::get_top_of_book(const Symbol& symbol) const {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        return TopOfBook{.symbol = symbol};
    }
    return it->second.get_top_of_book();
}

BookDepth MatchingEngine::get_depth(const Symbol& symbol, size_t max_depth) const {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        return BookDepth{.symbol = symbol};
    }
    return it->second.get_depth(max_depth);
}

const Order* MatchingEngine::find_order(OrderId order_id) const {
    auto it = order_symbol_map_.find(order_id);
    if (it == order_symbol_map_.end()) {
        return nullptr;
    }
    const auto& book = books_.at(it->second);
    return book.find_order(order_id);
}

bool MatchingEngine::check_invariants() const {
    for (const auto& [symbol, book] : books_) {
        if (!book.check_invariants()) {
            return false;
        }
    }
    return true;
}

void MatchingEngine::reset() {
    books_.clear();
    order_symbol_map_.clear();
    orders_processed_ = 0;
    trades_generated_ = 0;
    volume_traded_ = 0;
}

} // namespace matching
