#include "matching_engine/engine.hpp"
#include "matching_engine/events.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>

namespace {

using namespace matching;

void print_order_book(const OrderBook& book, size_t max_levels = 10) {
    auto depth = book.get_depth(max_levels);
    auto tob = book.get_top_of_book();

    std::cout << "\n=======================================================\n";
    std::cout << " ORDER BOOK: " << book.symbol() << " (Total resting orders: " << book.order_count() << ")\n";
    std::cout << "=======================================================\n";
    std::cout << std::left << std::setw(12) << "ORDERS"
              << std::setw(14) << "BID QTY"
              << std::setw(12) << "PRICE"
              << std::setw(14) << "ASK QTY"
              << std::setw(12) << "ORDERS" << "\n";
    std::cout << "-------------------------------------------------------\n";

    // Print Asks (in reverse order, highest ask at top, down to best ask)
    std::vector<LevelSnapshot> reversed_asks = depth.asks;
    std::reverse(reversed_asks.begin(), reversed_asks.end());

    for (const auto& ask : reversed_asks) {
        std::cout << std::left << std::setw(12) << ""
                  << std::setw(14) << ""
                  << "\033[1;31m" << std::setw(12) << ask.price << "\033[0m"
                  << "\033[1;31m" << std::setw(14) << ask.total_volume << "\033[0m"
                  << std::setw(12) << ask.order_count << "\n";
    }

    // Spread line
    if (tob.has_bid && tob.has_ask) {
        std::cout << "------- Spread: " << tob.spread()
                  << " (Best Bid: " << tob.best_bid_price
                  << ", Best Ask: " << tob.best_ask_price << ") -------\n";
    } else {
        std::cout << "------- Spread: N/A (One or both sides empty) -------\n";
    }

    // Print Bids (highest bid at top, descending)
    for (const auto& bid : depth.bids) {
        std::cout << std::left << std::setw(12) << bid.order_count
                  << "\033[1;32m" << std::setw(14) << bid.total_volume << "\033[0m"
                  << "\033[1;32m" << std::setw(12) << bid.price << "\033[0m"
                  << std::setw(14) << ""
                  << std::setw(12) << "" << "\n";
    }

    std::cout << "=======================================================\n\n";
}

void print_help() {
    std::cout << "\nSupported Commands:\n"
              << "  NEW <BUY|SELL> <LIMIT|MARKET|IOC|FOK|POST_ONLY> <SYMBOL> <QTY> [@ <PRICE>] id=<ID> [trader=<TRADER>]\n"
              << "    e.g.: NEW BUY LIMIT AAPL 100 @ 15000 id=1 trader=Alice\n"
              << "    e.g.: NEW SELL MARKET AAPL 50 id=2 trader=Bob\n"
              << "    e.g.: NEW BUY IOC AAPL 200 @ 15020 id=3 trader=Charlie\n"
              << "  CANCEL id=<ID>\n"
              << "    e.g.: CANCEL id=1\n"
              << "  MODIFY id=<ID> qty=<NEW_QTY> [price=<NEW_PRICE>]\n"
              << "    e.g.: MODIFY id=1 qty=80\n"
              << "    e.g.: MODIFY id=1 qty=120 price=15050\n"
              << "  PRINT <SYMBOL> [DEPTH]\n"
              << "    e.g.: PRINT AAPL 10\n"
              << "  STATS\n"
              << "  HELP\n"
              << "  QUIT / EXIT\n\n";
}

Side parse_side(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s == "BUY" || s == "B") return Side::Buy;
    return Side::Sell;
}

OrderType parse_order_type(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s == "MARKET" || s == "MKT") return OrderType::Market;
    if (s == "IOC") return OrderType::IOC;
    if (s == "FOK") return OrderType::FOK;
    if (s == "POST_ONLY" || s == "POSTONLY") return OrderType::PostOnly;
    return OrderType::Limit;
}

void process_line(MatchingEngine& engine, const std::string& line) {
    if (line.empty() || line[0] == '#') return;

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) return;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "NEW") {
        std::string side_str, type_str, symbol;
        Quantity qty = 0;
        if (!(iss >> side_str >> type_str >> symbol >> qty)) {
            std::cerr << "Invalid NEW syntax. Type HELP for examples.\n";
            return;
        }

        Side side = parse_side(side_str);
        OrderType type = parse_order_type(type_str);
        Price price = 0;
        OrderId id = INVALID_ORDER_ID;
        TraderId trader = "default";

        std::string token;
        while (iss >> token) {
            if (token == "@") {
                iss >> price;
            } else if (token.rfind("id=", 0) == 0) {
                id = std::stoull(token.substr(3));
            } else if (token.rfind("trader=", 0) == 0) {
                trader = token.substr(7);
            } else if (token.rfind("price=", 0) == 0) {
                price = std::stoll(token.substr(6));
            } else if (price == 0 && std::isdigit(token[0])) {
                price = std::stoll(token);
            }
        }

        if (id == INVALID_ORDER_ID) {
            static OrderId auto_id = 1000;
            id = ++auto_id;
        }

        Order order{
            .id = id,
            .symbol = symbol,
            .side = side,
            .type = type,
            .price = price,
            .initial_quantity = qty,
            .remaining_quantity = qty,
            .filled_quantity = 0,
            .trader_id = trader,
            .timestamp = current_timestamp_ns(),
            .status = OrderStatus::New
        };

        engine.submit_order(std::move(order));

    } else if (cmd == "CANCEL") {
        OrderId id = INVALID_ORDER_ID;
        std::string token;
        while (iss >> token) {
            if (token.rfind("id=", 0) == 0) {
                id = std::stoull(token.substr(3));
            } else if (std::isdigit(token[0])) {
                id = std::stoull(token);
            }
        }

        if (id == INVALID_ORDER_ID) {
            std::cerr << "Usage: CANCEL id=<ID>\n";
            return;
        }

        engine.cancel_order(id);

    } else if (cmd == "MODIFY") {
        OrderId id = INVALID_ORDER_ID;
        Quantity qty = 0;
        Price price = 0;
        std::string token;
        while (iss >> token) {
            if (token.rfind("id=", 0) == 0) {
                id = std::stoull(token.substr(3));
            } else if (token.rfind("qty=", 0) == 0) {
                qty = std::stoull(token.substr(4));
            } else if (token.rfind("price=", 0) == 0) {
                price = std::stoll(token.substr(6));
            }
        }

        if (id == INVALID_ORDER_ID || qty == 0) {
            std::cerr << "Usage: MODIFY id=<ID> qty=<QTY> [price=<PRICE>]\n";
            return;
        }

        engine.modify_order(id, qty, price);

    } else if (cmd == "PRINT" || cmd == "SNAPSHOT") {
        std::string symbol;
        size_t depth = 10;
        iss >> symbol;
        if (iss >> depth) {}

        const OrderBook* book = engine.get_order_book(symbol);
        if (!book) {
            std::cout << "Symbol '" << symbol << "' not found or no orders submitted yet.\n";
            return;
        }
        print_order_book(*book, depth);

    } else if (cmd == "STATS") {
        std::cout << "\nEngine Statistics:\n"
                  << "  Orders Processed: " << engine.total_orders_processed() << "\n"
                  << "  Trades Generated: " << engine.total_trades_generated() << "\n"
                  << "  Total Volume:     " << engine.total_volume_traded() << "\n\n";

    } else if (cmd == "HELP") {
        print_help();
    } else if (cmd == "QUIT" || cmd == "EXIT") {
        std::exit(0);
    } else {
        std::cerr << "Unknown command: '" << cmd << "'. Type HELP for options.\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    StreamEventListener listener(std::cout);
    MatchingEngine engine(&listener, STPPolicy::CancelIncoming);

    std::cout << "=========================================================\n"
              << "       Real-Time C++20 Order Matching Engine CLI         \n"
              << "   Price-Time Priority (FIFO) | Zero-Allocation Core     \n"
              << "=========================================================\n"
              << "Type 'HELP' for commands or submit orders directly.\n\n";

    if (argc > 1) {
        std::string filename = argv[1];
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open input script file: " << filename << "\n";
            return 1;
        }
        std::string line;
        while (std::getline(file, line)) {
            process_line(engine, line);
        }
        return 0;
    }

    std::string line;
    while (true) {
        std::cout << "engine> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        process_line(engine, line);
    }

    return 0;
}
