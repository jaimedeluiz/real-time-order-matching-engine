#include "tests/test_framework.hpp"
#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"

using namespace matching;

TEST_CASE("Modify Order: Quantity decrease preserves time priority") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Order 1: Buy 100 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T1"});
    // Order 2: Buy 100 @ 100
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T2"});

    // Reduce Order 1 size from 100 down to 40.
    // Rule: Downsizing preserves queue priority at the same price.
    bool mod_ok = engine.modify_order(1, 40, 100);
    REQUIRE(mod_ok);

    // Incoming Sell 50 @ 100: Order 1 must match 40 first, then Order 2 matches remaining 10
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 2u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 40u);
    REQUIRE_EQ(listener.trade_events[1].maker_order_id, 2u);
    REQUIRE_EQ(listener.trade_events[1].quantity, 10u);
}

TEST_CASE("Modify Order: Quantity increase loses time priority") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Order 1: Buy 100 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T1"});
    // Order 2: Buy 100 @ 100
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T2"});

    // Increase Order 1 size from 100 up to 150.
    // Rule: Sizing up loses priority, placing Order 1 behind Order 2!
    bool mod_ok = engine.modify_order(1, 150, 100);
    REQUIRE(mod_ok);

    // Incoming Sell 100 @ 100: Order 2 must match FIRST because Order 1 lost priority
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 100, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].maker_order_id, 2u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 100u);
}

TEST_CASE("IOC (Immediate-Or-Cancel) Order Behavior") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Resting Ask: Sell 50 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T1"});

    // Aggressive IOC: Buy 100 @ 100.
    // Should fill 50 immediately and cancel remaining 50 without resting on the book
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::IOC, .price = 100, .initial_quantity = 100, .trader_id = "T2"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 50u);

    // Remainder should be canceled
    REQUIRE_EQ(listener.canceled_events.size(), 1u);
    REQUIRE_EQ(listener.canceled_events[0].order_id, 2u);
    REQUIRE_EQ(listener.canceled_events[0].remaining_quantity, 50u);

    // Engine must not have Order 2 in book
    REQUIRE_EQ(engine.find_order(2), nullptr);
}

TEST_CASE("FOK (Fill-Or-Kill) Order Behavior: Full execution vs immediate rejection") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Resting Ask: Sell 50 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T1"});

    // FOK 1: Buy 80 @ 100 (needs 80, but book only has 50)
    // Must be rejected with InsufficientLiquidity; 0 trades executed
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::FOK, .price = 100, .initial_quantity = 80, .trader_id = "T2"});

    REQUIRE_EQ(listener.trade_events.size(), 0u);
    REQUIRE_EQ(listener.rejected_events.size(), 1u);
    REQUIRE_EQ(listener.rejected_events[0].order_id, 2u);
    REQUIRE_EQ(listener.rejected_events[0].reason, RejectReason::InsufficientLiquidity);

    // Resting order 1 is still untouched
    const Order* resting = engine.find_order(1);
    REQUIRE(resting != nullptr);
    REQUIRE_EQ(resting->remaining_quantity, 50u);

    // FOK 2: Buy 50 @ 100 (exactly matches available 50)
    // Must execute fully!
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Buy, .type = OrderType::FOK, .price = 100, .initial_quantity = 50, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 50u);
    REQUIRE_EQ(listener.trade_events[0].taker_order_id, 3u);
}

TEST_CASE("Market Order Behavior: Empty book rejection and partial fill cancellation") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Market order against empty book should be rejected
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Buy, .type = OrderType::Market, .price = 0, .initial_quantity = 100, .trader_id = "T1"});

    REQUIRE_EQ(listener.rejected_events.size(), 1u);
    REQUIRE_EQ(listener.rejected_events[0].order_id, 1u);
    REQUIRE_EQ(listener.rejected_events[0].reason, RejectReason::EmptyBookForMarket);

    // Add 30 shares at 100
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 30, .trader_id = "T2"});

    // Market Buy for 50 shares: should consume 30, and cancel remaining 20 (never rest)
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Buy, .type = OrderType::Market, .price = 0, .initial_quantity = 50, .trader_id = "T3"});

    REQUIRE_EQ(listener.trade_events.size(), 1u);
    REQUIRE_EQ(listener.trade_events[0].quantity, 30u);
    REQUIRE_EQ(listener.trade_events[0].price, 100);

    REQUIRE_EQ(listener.canceled_events.size(), 1u);
    REQUIRE_EQ(listener.canceled_events[0].order_id, 3u);
    REQUIRE_EQ(listener.canceled_events[0].remaining_quantity, 20u);

    // Book is now completely empty
    REQUIRE_EQ(engine.get_top_of_book(sym).has_bid, false);
    REQUIRE_EQ(engine.get_top_of_book(sym).has_ask, false);
}

TEST_CASE("Post-Only Order Behavior") {
    RecordingEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Resting Ask: Sell 50 @ 100
    engine.submit_order(Order{.id = 1, .symbol = sym, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .initial_quantity = 50, .trader_id = "T1"});

    // Post-Only Buy @ 100 would cross with resting Ask -> must be rejected
    engine.submit_order(Order{.id = 2, .symbol = sym, .side = Side::Buy, .type = OrderType::PostOnly, .price = 100, .initial_quantity = 50, .trader_id = "T2"});

    REQUIRE_EQ(listener.rejected_events.size(), 1u);
    REQUIRE_EQ(listener.rejected_events[0].order_id, 2u);
    REQUIRE_EQ(listener.rejected_events[0].reason, RejectReason::PostOnlyCrossed);
    REQUIRE_EQ(listener.trade_events.size(), 0u);

    // Post-Only Buy @ 98 does NOT cross -> accepted and rests
    engine.submit_order(Order{.id = 3, .symbol = sym, .side = Side::Buy, .type = OrderType::PostOnly, .price = 98, .initial_quantity = 40, .trader_id = "T3"});

    REQUIRE(engine.find_order(3) != nullptr);
    auto tob = engine.get_top_of_book(sym);
    REQUIRE(tob.has_bid);
    REQUIRE_EQ(tob.best_bid_price, 98);
}
