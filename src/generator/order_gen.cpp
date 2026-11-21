#include <iostream>
#include <random>
#include <vector>
#include <string>
#include <cstdlib>

int main(int argc, char* argv[]) {
    size_t count = 1000;
    if (argc > 1) {
        count = std::stoull(argv[1]);
    }

    unsigned int seed = 42;
    if (argc > 2) {
        seed = static_cast<unsigned int>(std::stoul(argv[2]));
    }

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> action_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> price_offset_dist(0.0, 15.0);
    std::uniform_int_distribution<uint64_t> qty_dist(5, 200);
    std::uniform_int_distribution<int> trader_dist(1, 10);

    const int64_t mid_price = 15000;
    const std::string symbol = "AAPL";

    std::vector<uint64_t> active_order_ids;
    uint64_t next_id = 1;

    for (size_t i = 0; i < count; ++i) {
        int action = action_dist(rng);

        if (action <= 60 || active_order_ids.empty()) {
            // New Limit order
            uint64_t id = next_id++;
            std::string side = (side_dist(rng) == 0) ? "BUY" : "SELL";
            int64_t offset = std::abs(static_cast<int64_t>(price_offset_dist(rng))) + 1;
            int64_t price = (side == "BUY") ? (mid_price - offset) : (mid_price + offset);
            uint64_t qty = qty_dist(rng);
            int trader_num = trader_dist(rng);

            std::cout << "NEW " << side << " LIMIT " << symbol << " " << qty
                      << " @ " << price << " id=" << id << " trader=T" << trader_num << "\n";
            active_order_ids.push_back(id);

        } else if (action <= 80) {
            // Aggressive Market or Crossing Limit order
            uint64_t id = next_id++;
            std::string side = (side_dist(rng) == 0) ? "BUY" : "SELL";
            uint64_t qty = qty_dist(rng) / 2 + 1;
            int trader_num = trader_dist(rng);

            if (rng() % 2 == 0) {
                std::cout << "NEW " << side << " MARKET " << symbol << " " << qty
                          << " id=" << id << " trader=T" << trader_num << "\n";
            } else {
                int64_t crossing_price = (side == "BUY") ? (mid_price + 5) : (mid_price - 5);
                std::cout << "NEW " << side << " LIMIT " << symbol << " " << qty
                          << " @ " << crossing_price << " id=" << id << " trader=T" << trader_num << "\n";
            }
            active_order_ids.push_back(id);

        } else if (action <= 95 && !active_order_ids.empty()) {
            // Cancel an active order
            size_t idx = rng() % active_order_ids.size();
            uint64_t cancel_id = active_order_ids[idx];
            active_order_ids.erase(active_order_ids.begin() + idx);
            std::cout << "CANCEL id=" << cancel_id << "\n";

        } else if (!active_order_ids.empty()) {
            // Modify an active order
            size_t idx = rng() % active_order_ids.size();
            uint64_t mod_id = active_order_ids[idx];
            uint64_t new_qty = qty_dist(rng);
            std::cout << "MODIFY id=" << mod_id << " qty=" << new_qty << "\n";
        }

        // Periodically print snapshot
        if ((i + 1) % 250 == 0) {
            std::cout << "PRINT " << symbol << " 5\n";
        }
    }

    std::cout << "PRINT " << symbol << " 10\n";
    std::cout << "STATS\n";
    return 0;
}
