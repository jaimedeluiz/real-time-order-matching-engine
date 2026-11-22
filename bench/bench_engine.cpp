#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"
#include "matching_engine/spsc_queue.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <random>

using namespace matching;

namespace {

struct BenchResult {
    std::string name;
    size_t iterations;
    double total_time_ms;
    double throughput_ops_sec;
    double p50_ns;
    double p90_ns;
    double p99_ns;
    double p99_9_ns;
    double max_ns;
};

void print_result(const BenchResult& r) {
    std::cout << std::left << std::setw(34) << r.name
              << std::right
              << std::setw(12) << r.iterations
              << std::setw(16) << std::fixed << std::setprecision(0) << r.throughput_ops_sec
              << std::setw(10) << std::fixed << std::setprecision(1) << r.p50_ns
              << std::setw(10) << std::fixed << std::setprecision(1) << r.p90_ns
              << std::setw(10) << std::fixed << std::setprecision(1) << r.p99_ns
              << std::setw(10) << std::fixed << std::setprecision(1) << r.p99_9_ns
              << std::setw(10) << std::fixed << std::setprecision(1) << r.max_ns
              << "\n";
}

BenchResult compute_metrics(const std::string& name, size_t n, std::vector<uint64_t>& latencies_ns, double elapsed_ms) {
    std::sort(latencies_ns.begin(), latencies_ns.end());
    double throughput = (static_cast<double>(n) / (elapsed_ms / 1000.0));

    auto get_pct = [&](double pct) -> double {
        size_t idx = static_cast<size_t>(pct * static_cast<double>(latencies_ns.size()) / 100.0);
        if (idx >= latencies_ns.size()) idx = latencies_ns.size() - 1;
        return static_cast<double>(latencies_ns[idx]);
    };

    return BenchResult{
        .name = name,
        .iterations = n,
        .total_time_ms = elapsed_ms,
        .throughput_ops_sec = throughput,
        .p50_ns = get_pct(50.0),
        .p90_ns = get_pct(90.0),
        .p99_ns = get_pct(99.0),
        .p99_9_ns = get_pct(99.9),
        .max_ns = static_cast<double>(latencies_ns.back())
    };
}

void bench_limit_order_insert(size_t n = 1000000) {
    NullEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    std::vector<uint64_t> latencies;
    latencies.reserve(n);

    auto start_total = std::chrono::high_resolution_clock::now();

    for (size_t i = 1; i <= n; ++i) {
        Order order{
            .id = i,
            .symbol = sym,
            .side = (i % 2 == 0) ? Side::Buy : Side::Sell,
            .type = OrderType::Limit,
            .price = (i % 2 == 0) ? static_cast<Price>(10000 - (i % 2000)) : static_cast<Price>(10050 + (i % 2000)),
            .initial_quantity = 100,
            .trader_id = "trader"
        };

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(order);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    auto end_total = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    print_result(compute_metrics("Add Limit Order (Varying Levels)", n, latencies, elapsed_ms));
}

void bench_cancel_order(size_t n = 500000) {
    NullEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    // Pre-populate book
    for (size_t i = 1; i <= n; ++i) {
        engine.submit_order(Order{
            .id = i,
            .symbol = sym,
            .side = (i % 2 == 0) ? Side::Buy : Side::Sell,
            .type = OrderType::Limit,
            .price = (i % 2 == 0) ? static_cast<Price>(10000 - (i % 1000)) : static_cast<Price>(10050 + (i % 1000)),
            .initial_quantity = 100,
            .trader_id = "trader"
        });
    }

    std::vector<uint64_t> latencies;
    latencies.reserve(n);

    auto start_total = std::chrono::high_resolution_clock::now();

    for (size_t i = 1; i <= n; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.cancel_order(i);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    auto end_total = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    print_result(compute_metrics("Cancel Order (O(1) Hash Map)", n, latencies, elapsed_ms));
}

void bench_match_crossing(size_t n = 500000) {
    NullEventListener listener;
    MatchingEngine engine(&listener);
    const Symbol sym = "AAPL";

    std::vector<uint64_t> latencies;
    latencies.reserve(n);

    OrderId id_counter = 1;
    auto start_total = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < n; ++i) {
        // Post resting ask
        OrderId resting_id = id_counter++;
        engine.submit_order(Order{
            .id = resting_id,
            .symbol = sym,
            .side = Side::Sell,
            .type = OrderType::Limit,
            .price = 10000,
            .initial_quantity = 100,
            .trader_id = "maker"
        });

        // Aggressive crossing bid
        OrderId taker_id = id_counter++;
        Order taker{
            .id = taker_id,
            .symbol = sym,
            .side = Side::Buy,
            .type = OrderType::Limit,
            .price = 10000,
            .initial_quantity = 100,
            .trader_id = "taker"
        };

        auto t0 = std::chrono::high_resolution_clock::now();
        engine.submit_order(taker);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    auto end_total = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    print_result(compute_metrics("Aggressive Match Execution", n, latencies, elapsed_ms));
}

void bench_mixed_workload(size_t n = 1000000) {
    NullEventListener listener;
    MatchingEngine engine(&listener, STPPolicy::None);
    const Symbol sym = "AAPL";

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> action_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> price_offset_dist(0.0, 15.0);
    std::uniform_int_distribution<uint64_t> qty_dist(10, 100);

    const int64_t mid_price = 10000;
    std::vector<OrderId> active_ids;
    active_ids.reserve(n / 2);
    OrderId next_id = 1;

    std::vector<uint64_t> latencies;
    latencies.reserve(n);

    auto start_total = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < n; ++i) {
        int action = action_dist(rng);

        if (action <= 60 || active_ids.empty()) {
            // Passive Limit order (60%)
            OrderId id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            int64_t offset = static_cast<int64_t>(std::abs(price_offset_dist(rng))) + 1;
            Price price = (side == Side::Buy) ? (mid_price - offset) : (mid_price + offset);
            Quantity qty = qty_dist(rng);

            Order order{
                .id = id,
                .symbol = sym,
                .side = side,
                .type = OrderType::Limit,
                .price = price,
                .initial_quantity = qty,
                .trader_id = "T"
            };

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.submit_order(order);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
            active_ids.push_back(id);

        } else if (action <= 85) {
            // Cancel active order (25%)
            size_t idx = rng() % active_ids.size();
            OrderId cid = active_ids[idx];
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.cancel_order(cid);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));

        } else if (action <= 95) {
            // Crossing Aggressive Order (10%)
            OrderId id = next_id++;
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? (mid_price + 2) : (mid_price - 2);
            Quantity qty = qty_dist(rng);

            Order order{
                .id = id,
                .symbol = sym,
                .side = side,
                .type = OrderType::Limit,
                .price = price,
                .initial_quantity = qty,
                .trader_id = "T"
            };

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.submit_order(order);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));

        } else {
            // Modify order (5%)
            size_t idx = rng() % active_ids.size();
            OrderId mid = active_ids[idx];
            Quantity new_qty = qty_dist(rng);

            auto t0 = std::chrono::high_resolution_clock::now();
            engine.modify_order(mid, new_qty);
            auto t1 = std::chrono::high_resolution_clock::now();

            latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        }
    }

    auto end_total = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    print_result(compute_metrics("Realistic Mixed (60% Add/25% Cxl/10% Match/5% Mod)", n, latencies, elapsed_ms));
}

void bench_spsc_queue(size_t n = 2000000) {
    SPSCQueue<Order, 131072> queue;

    std::vector<uint64_t> latencies;
    latencies.reserve(n);

    auto start_total = std::chrono::high_resolution_clock::now();

    for (size_t i = 1; i <= n; ++i) {
        Order order{.id = i, .price = 100, .initial_quantity = 50};

        auto t0 = std::chrono::high_resolution_clock::now();
        queue.push(order);
        Order popped;
        queue.pop(popped);
        auto t1 = std::chrono::high_resolution_clock::now();

        latencies.push_back(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    auto end_total = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    print_result(compute_metrics("Lock-Free SPSC Queue (Push+Pop)", n, latencies, elapsed_ms));
}

} // namespace

int main() {
    std::cout << "\n========================================================================================================\n";
    std::cout << "                                  MATCHING ENGINE PERFORMANCE BENCHMARK                                  \n";
    std::cout << "========================================================================================================\n";
    std::cout << std::left << std::setw(34) << "Benchmark Scenario"
              << std::right
              << std::setw(12) << "Iterations"
              << std::setw(16) << "Throughput (ops/s)"
              << std::setw(10) << "p50 (ns)"
              << std::setw(10) << "p90 (ns)"
              << std::setw(10) << "p99 (ns)"
              << std::setw(10) << "p99.9 (ns)"
              << std::setw(10) << "max (ns)"
              << "\n";
    std::cout << "--------------------------------------------------------------------------------------------------------\n";

    bench_limit_order_insert(1000000);
    bench_cancel_order(500000);
    bench_match_crossing(500000);
    bench_mixed_workload(1000000);
    bench_spsc_queue(2000000);

    std::cout << "========================================================================================================\n\n";
    return 0;
}
