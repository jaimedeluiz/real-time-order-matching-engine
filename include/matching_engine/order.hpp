#pragma once

#include "matching_engine/types.hpp"

namespace matching {

struct Order {
    OrderId id{INVALID_ORDER_ID};
    Symbol symbol{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{INVALID_PRICE};
    Quantity initial_quantity{0};
    Quantity remaining_quantity{0};
    Quantity filled_quantity{0};
    TraderId trader_id{};
    Timestamp timestamp{0};
    OrderStatus status{OrderStatus::New};

    [[nodiscard]] constexpr bool is_filled() const noexcept {
        return remaining_quantity == 0 && initial_quantity > 0;
    }

    [[nodiscard]] constexpr bool is_active() const noexcept {
        return (status == OrderStatus::New || status == OrderStatus::PartiallyFilled) && remaining_quantity > 0;
    }
};

} // namespace matching
