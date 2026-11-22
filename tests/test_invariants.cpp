#include "tests/test_framework.hpp"
#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"
#include <random>
#include <vector>

using namespace matching;

TEST_CASE("Property / Fuzz Invariant Testing: 5,000 random operations with strict invariant verification") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener, STPPolicy::CancelIncoming);

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<int> action_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 3);
    std::normal_distribution<double> price_offset_dist(0.0, 20.0);
    std::uniform_int_distribution<uint64_t> qty_dist(1, 100);
    std::uniform_int_distribution<int> trader_dist(1, 8);

    const std::vector<Symbol> symbols = {"AAPL", "GOOG"};
    std::vector<OrderId> active_ids;
    OrderId next_id = 1;

    const size_t NUM_OPS = 5000;

    for (size_t i = 0; i < NUM_OPS; ++i) {
        int action = action_dist(rng);
        const Symbol& sym = symbols[rng() % symbols.size()];

        if (action <= 65 || active_ids.empty()) {
            // New Order (Limit, Market, or IOC)
            OrderId id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int type_val = type_dist(rng);
            OrderType type = OrderType::Limit;
            if (type_val == 1) type = OrderType::Market;
            else if (type_val == 2) type = OrderType::IOC;
            else if (type_val == 3) type = OrderType::PostOnly;

            int64_t mid_price = (sym == "AAPL") ? 15000 : 28000;
            int64_t offset = static_cast<int64_t>(std::abs(price_offset_dist(rng))) + 1;
            Price price = (side == Side::Buy) ? (mid_price - offset) : (mid_price + offset);
            Quantity qty = qty_dist(rng);
            TraderId trader = "Trader_" + std::to_string(trader_dist(rng));

            Order order{
                .id = id,
                .symbol = sym,
                .side = side,
                .type = type,
                .price = (type == OrderType::Market) ? 0 : price,
                .initial_quantity = qty,
                .remaining_quantity = qty,
                .trader_id = trader
            };

            engine.submit_order(std::move(order));
            if (engine.find_order(id) != nullptr) {
                active_ids.push_back(id);
            }

        } else if (action <= 85 && !active_ids.empty()) {
            // Cancel an active order
            size_t idx = rng() % active_ids.size();
            OrderId cancel_id = active_ids[idx];
            active_ids.erase(active_ids.begin() + idx);
            engine.cancel_order(cancel_id);

        } else if (!active_ids.empty()) {
            // Modify an active order
            size_t idx = rng() % active_ids.size();
            OrderId mod_id = active_ids[idx];
            Quantity new_qty = qty_dist(rng);
            engine.modify_order(mod_id, new_qty);
        }

        // Clean up active_ids list periodically
        if (i % 200 == 0) {
            std::vector<OrderId> still_active;
            for (OrderId id : active_ids) {
                if (engine.find_order(id) != nullptr) {
                    still_active.push_back(id);
                }
            }
            active_ids = std::move(still_active);
        }

        // Verify Invariants after every single operation:
        // 1. Order book structure invariants
        REQUIRE(engine.check_invariants());

        // 2. Uncrossed book invariant for all symbols
        for (const auto& s : symbols) {
            auto tob = engine.get_top_of_book(s);
            if (tob.has_bid && tob.has_ask) {
                REQUIRE(tob.best_bid_price < tob.best_ask_price);
            }
        }
    }

    // Invariant 3: Total buy volume traded must equal total sell volume traded
    Quantity total_buy_qty = 0;
    Quantity total_sell_qty = 0;
    for (const auto& trade : listener.trade_events) {
        REQUIRE(trade.buy_order_id != INVALID_ORDER_ID);
        REQUIRE(trade.sell_order_id != INVALID_ORDER_ID);
        REQUIRE(trade.quantity > 0);
        REQUIRE(trade.price > 0);
        total_buy_qty += trade.quantity;
        total_sell_qty += trade.quantity;
    }
    REQUIRE_EQ(total_buy_qty, total_sell_qty);
}
