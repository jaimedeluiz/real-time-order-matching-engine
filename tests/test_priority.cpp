#include "tests/test_framework.hpp"
#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"

using namespace matching;

TEST_CASE("Price Priority: Higher bid matches before lower bid") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Insert two bids at different prices
    // Order 1: Buy 100 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T1"});
    // Order 2: Buy 100 @ 105 (higher bid, superior price)
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 105, .initial_quantity = 100, .trader_id = "T2"});

    // Incoming Sell 50 @ 95 should match Order 2 (price 105) first despite Order 1 arriving earlier
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 95, .initial_quantity = 50, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 2u);
    REQUIRE_EQ(listener.trade_events[0].price, 105);
    REQUIRE_EQ(listener.trade_events[0].quantity, 50u);
}

TEST_CASE("Time Priority: Earliest order at the same price matches first (FIFO)") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Order 1 arrives first at price 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T1"});
    // Order 2 arrives second at exact same price 100
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T2"});

    // Incoming Sell 100 @ 100 must match Order 1 entirely due to time priority
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 100u);

    // Next Sell 50 @ 100 matches Order 2
    engine.submit_order(Order{.id = 4, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T4"});

    REQUIRE_EQ(listener.trade_events.size(), 2u);
    REQUIRE_EQ(listener.trade_events[1].maker_order_id, 2u);
    REQUIRE_EQ(listener.trade_events[1].quantity, 50u);
}

TEST_CASE("Partial Fills: Remainder rests in the book for limit orders") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Resting Ask: Sell 100 @ 150
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 150, .initial_quantity = 100, .trader_id = "T1"});

    // Aggressive Bid: Buy 40 @ 150
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 150, .initial_quantity = 40, .trader_id = "T2"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 40u);

    // Order 1 should have 60 remaining resting on the book
    const Order* resting = engine.find_order(1);
    REQUIRE(resting != nullptr);
    REQUIRE_EQ(resting->remaining_quantity, 60u);
    REQUIRE_EQ(resting->filled_quantity, 40u);

    auto tob = engine.get_top_of_book(sym);
    REQUIRE(tob.has_ask);
    REQUIRE_EQ(tob.best_ask_price, 150);
    REQUIRE_EQ(tob.best_ask_quantity, 60u);
}

TEST_CASE("Multi-level Sweep: Large aggressive order sweeps multiple price levels") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Resting asks across 3 price levels
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T1"});
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 101, .initial_quantity = 75, .trader_id = "T2"});
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 102, .initial_quantity = 100, .trader_id = "T3"});

    // Sweep: Buy 150 @ 105
    // Should completely fill level 100 (50), completely fill level 101 (75), and partially fill level 102 (25)
    engine.submit_order(Order{.id = 4, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 105, .initial_quantity = 150, .trader_id = "T4"});

    REQUIRE_EQ(listener.trade_events.size(), 3u);
    // Fill 1
    REQUIRE_EQ(listener.trade_events[0].price, 100);
    REQUIRE_EQ(listener.trade_events[0].quantity, 50u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 1u);
    // Fill 2
    REQUIRE_EQ(listener.trade_events[1].price, 101);
    REQUIRE_EQ(listener.trade_events[1].quantity, 75u);
    REQUIRE_EQ(listener.trade_events[1].maker_order_id, 2u);
    // Fill 3
    REQUIRE_EQ(listener.trade_events[2].price, 102);
    REQUIRE_EQ(listener.trade_events[2].quantity, 25u);
    REQUIRE_EQ(listener.trade_events[2].maker_order_id, 3u);

    // Best ask should now be 102 with 75 remaining
    auto tob = engine.get_top_of_book(sym);
    REQUIRE(tob.has_ask);
    REQUIRE_EQ(tob.best_ask_price, 102);
    REQUIRE_EQ(tob.best_ask_quantity, 75u);
}

TEST_CASE("Cancel resting order and cancel unknown id") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T1"});
    REQUIRE_EQ(engine.find_order(1) != nullptr, true);

    // Cancel resting order
    bool cancel_res = engine.cancel_order(1);
    REQUIRE(cancel_res);
    REQUIRE_EQ(engine.find_order(1), nullptr);
    REQUIRE_EQ(listener.canceled_events.size(), 1u);
    REQUIRE_EQ(listener.canceled_events[0].order_id, 1u);
    REQUIRE_EQ(listener.canceled_events[0].remaining_quantity, 100u);

    // Cancel unknown id
    bool cancel_unknown = engine.cancel_order(9999);
    REQUIRE(!cancel_unknown);
    REQUIRE_EQ(listener.rejected_events.size(), 1u);
    REQUIRE_EQ(listener.rejected_events[0].order_id, 9999u);
    REQUIRE_EQ(listener.rejected_events[0].reason, RejectReason::OrderNotFound);
}
