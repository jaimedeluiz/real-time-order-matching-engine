#include "tests/test_framework.hpp"
#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"
#include <random>

using namespace matching;

TEST_CASE("Determinism Test: Identical order stream yields exact identical trade execution stream") {
    // Generate deterministic order sequence
    std::mt19937_64 rng(999);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> action_dist(0, 10);
    std::uniform_int_distribution<int64_t> price_dist(100, 120);
    std::uniform_int_distribution<uint64_t> qty_dist(10, 100);

    struct Action {
        int kind; // 0: new, 1: cancel, 2: modify
        Order order;
        OrderId target_id;
        Quantity new_qty;
    };

    std::vector<Action> script;
    std::vector<OrderId> active_ids;
    OrderId id_counter = 1;

    for (size_t i = 0; i < 2000; ++i) {
        int act = action_dist(rng);
        if (act < 7 || active_ids.empty()) {
            OrderId id = id_counter++;
            Side s = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            Order ord{
                .id = id,
                .symbol = "AAPL",
                .side = s,
                .type = OrderType::Limit,
                .price = price_dist(rng),
                .initial_quantity = qty_dist(rng),
                .remaining_quantity = 0,
                .trader_id = "T_" + std::to_string(id % 5)
            };
            script.push_back(Action{.kind = 0, .order = ord, .target_id = 0, .new_qty = 0});
            active_ids.push_back(id);
        } else if (act < 9) {
            size_t idx = rng() % active_ids.size();
            script.push_back(Action{.kind = 1, .order = {}, .target_id = active_ids[idx], .new_qty = 0});
        } else {
            size_t idx = rng() % active_ids.size();
            script.push_back(Action{.kind = 2, .order = {}, .target_id = active_ids[idx], .new_qty = qty_dist(rng)});
        }
    }

    // Run on Engine 1
    RecordingEventListener listener1;
    MatchingEngine engine1(&listener1);
    for (const auto& a : script) {
        if (a.kind == 0) engine1.submit_order(a.order);
        else if (a.kind == 1) engine1.cancel_order(a.target_id);
        else if (a.kind == 2) engine1.modify_order(a.target_id, a.new_qty);
    }

    // Run on Engine 2
    RecordingEventListener listener2;
    MatchingEngine engine2(&listener2);
    for (const auto& a : script) {
        if (a.kind == 0) engine2.submit_order(a.order);
        else if (a.kind == 1) engine2.cancel_order(a.target_id);
        else if (a.kind == 2) engine2.modify_order(a.target_id, a.new_qty);
    }

    // Verify bit-for-bit event equality
    REQUIRE_EQ(listener1.trade_events.size(), listener2.trade_events.size());
    for (size_t i = 0; i < listener1.trade_events.size(); ++i) {
        const auto& t1 = listener1.trade_events[i];
        const auto& t2 = listener2.trade_events[i];
        REQUIRE_EQ(t1.price, t2.price);
        REQUIRE_EQ(t1.quantity, t2.quantity);
        REQUIRE_EQ(t1.maker_order_id, t2.maker_order_id);
        REQUIRE_EQ(t1.taker_order_id, t2.taker_order_id);
        REQUIRE_EQ(t1.buy_order_id, t2.buy_order_id);
        REQUIRE_EQ(t1.sell_order_id, t2.sell_order_id);
    }
}
