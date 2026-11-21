#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <chrono>
#include <ostream>

namespace matching {

// Strongly-typed identifiers and numerical domains.
// Rule: Prices are strictly integer ticks (int64_t) to eliminate IEEE-754 floating-point
// rounding errors, nondeterminism, and binary representation drift.
// Quantities are unsigned integer units (uint64_t).
using OrderId = uint64_t;
using Price = int64_t;
using Quantity = uint64_t;
using Timestamp = uint64_t; // Nanoseconds since Unix epoch
using Symbol = std::string;
using TraderId = std::string;

// Sentinels
inline constexpr Price INVALID_PRICE = 0;
inline constexpr Price MARKET_ORDER_PRICE = -1; // Marker for market orders
inline constexpr OrderId INVALID_ORDER_ID = 0;

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

[[nodiscard]] constexpr Side opposite_side(Side s) noexcept {
    return (s == Side::Buy) ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view side_to_string(Side s) noexcept {
    switch (s) {
        case Side::Buy:  return "BUY";
        case Side::Sell: return "SELL";
    }
    return "UNKNOWN";
}

enum class OrderType : uint8_t {
    Limit = 0,     // Rests in the book if not matched immediately
    Market = 1,    // Matches aggressively at best available prices; unfilled remainder is canceled
    IOC = 2,       // Immediate-Or-Cancel: fills as much as possible immediately, cancels remainder
    FOK = 3,       // Fill-Or-Kill: must fill completely immediately, or cancels/rejects entirely
    PostOnly = 4   // Must not execute immediately (maker only); rejected if it would cross
};

[[nodiscard]] constexpr std::string_view order_type_to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit:    return "LIMIT";
        case OrderType::Market:   return "MARKET";
        case OrderType::IOC:      return "IOC";
        case OrderType::FOK:      return "FOK";
        case OrderType::PostOnly: return "POST_ONLY";
    }
    return "UNKNOWN";
}

enum class OrderStatus : uint8_t {
    New = 0,
    PartiallyFilled = 1,
    Filled = 2,
    Canceled = 3,
    Rejected = 4
};

[[nodiscard]] constexpr std::string_view order_status_to_string(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::New:             return "NEW";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled:          return "FILLED";
        case OrderStatus::Canceled:        return "CANCELED";
        case OrderStatus::Rejected:        return "REJECTED";
    }
    return "UNKNOWN";
}

enum class RejectReason : uint8_t {
    None = 0,
    InvalidQuantity,
    InvalidPrice,
    DuplicateOrderId,
    OrderNotFound,
    SymbolNotFound,
    InsufficientLiquidity, // FOK orders when book depth cannot satisfy total size
    PostOnlyCrossed,       // Post-Only order would trade immediately
    SelfTradePrevented,    // Trade aborted due to STP rule
    EmptyBookForMarket     // Market order cannot be matched because opposite book is empty
};

[[nodiscard]] constexpr std::string_view reject_reason_to_string(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None:                  return "NONE";
        case RejectReason::InvalidQuantity:       return "INVALID_QUANTITY";
        case RejectReason::InvalidPrice:          return "INVALID_PRICE";
        case RejectReason::DuplicateOrderId:      return "DUPLICATE_ORDER_ID";
        case RejectReason::OrderNotFound:         return "ORDER_NOT_FOUND";
        case RejectReason::SymbolNotFound:        return "SYMBOL_NOT_FOUND";
        case RejectReason::InsufficientLiquidity: return "INSUFFICIENT_LIQUIDITY_FOR_FOK";
        case RejectReason::PostOnlyCrossed:       return "POST_ONLY_WOULD_CROSS";
        case RejectReason::SelfTradePrevented:    return "SELF_TRADE_PREVENTED";
        case RejectReason::EmptyBookForMarket:    return "EMPTY_BOOK_FOR_MARKET";
    }
    return "UNKNOWN";
}

enum class STPPolicy : uint8_t {
    None = 0,            // Allow self-trading
    CancelIncoming = 1,  // Cancel the incoming aggressive order
    CancelResting = 2,   // Cancel the passive order resting on the book
    CancelBoth = 3       // Cancel both resting and incoming
};

[[nodiscard]] inline Timestamp current_timestamp_ns() noexcept {
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

// Stream operators for easy output / debugging
inline std::ostream& operator<<(std::ostream& os, Side s) {
    return os << side_to_string(s);
}

inline std::ostream& operator<<(std::ostream& os, OrderType t) {
    return os << order_type_to_string(t);
}

inline std::ostream& operator<<(std::ostream& os, RejectReason r) {
    return os << reject_reason_to_string(r);
}

} // namespace matching
