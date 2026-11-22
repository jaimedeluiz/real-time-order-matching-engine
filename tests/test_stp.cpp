#include "tests/test_framework.hpp"
#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"

using namespace matching;

TEST_CASE("Self-Trade Prevention: CancelIncoming policy") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener, STPPolicy::CancelIncoming);
    const Symbol sym = "AAPL";

    // Trader Alice places resting Ask 100 @ 150
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 150, .initial_quantity = 100, .trader_id = "Alice"});

    // Trader Alice submits Buy 50 @ 150 (would self-trade!)
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 150, .initial_quantity = 50, .trader_id = "Alice"});

    // 0 trades generated
    REQUIRE_EQ(listener.trade_events.size(), 0u);

    // Incoming order #2 should be canceled
    REQUIRE_EQ(listener.canceled_events.size(), 1u);
    REQUIRE_EQ(listener.canceled_events[0].order_id, 2u);

    // Resting order #1 remains intact
    const Order* resting = engine.find_order(1);
    REQUIRE(resting != nullptr);
    REQUIRE_EQ(resting->remaining_quantity, 100u);
}

TEST_CASE("Self-Trade Prevention: CancelResting policy") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener, STPPolicy::CancelResting);
    const Symbol sym = "AAPL";

    // Trader Alice places resting Ask 50 @ 150
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 150, .initial_quantity = 50, .trader_id = "Alice"});

    // Trader Bob places resting Ask 50 @ 152
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 152, .initial_quantity = 50, .trader_id = "Bob"});

    // Trader Alice submits Buy 50 @ 155
    // Under CancelResting: Alice's resting order #1 is canceled, and Alice's incoming order matches Bob's order #2 at 152!
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 155, .initial_quantity = 50, .trader_id = "Alice"});

    // Order 1 was canceled
    REQUIRE_EQ(engine.find_order(1), nullptr);

    // Trade occurred with Bob (#2)
    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 2u);
    REQUIRE_EQ(listener.trade_events[0].taker_order_id, 3u);
    REQUIRE_EQ(listener.trade_events[0].price, 152);
    REQUIRE_EQ(listener.trade_events[0].quantity, 50u);
}

TEST_CASE("Self-Trade Prevention: CancelBoth policy") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener, STPPolicy::CancelBoth);
    const Symbol sym = "AAPL";

    // Trader Alice places resting Ask 100 @ 150
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 150, .initial_quantity = 100, .trader_id = "Alice"});

    // Trader Alice submits Buy 100 @ 150
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 150, .initial_quantity = 100, .trader_id = "Alice"});

    // Both orders must be canceled, 0 trades
    REQUIRE_EQ(listener.trade_events.size(), 0u);
    REQUIRE_EQ(engine.find_order(1), nullptr);
    REQUIRE_EQ(engine.find_order(2), nullptr);
    REQUIRE_EQ(listener.canceled_events.size(), 2u);
}
