# Real-Time Order Matching Engine (Modern C++20)

[![CI](https://github.com/jaimedeluiz/real-time-order-matching-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/jaimedeluiz/real-time-order-matching-engine/actions/workflows/ci.yml)
[![Language](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Performance](https://img.shields.io/badge/Throughput-2.75M%2B%20ops%2Fsec-brightgreen.svg)](#benchmarks)

An ultra-high-performance, deterministic, price-time priority (FIFO) order matching engine implemented in modern **C++20**. Designed as the core execution venue of an electronic financial exchange, it accepts Buy and Sell orders, enforces strict execution rules, and executes trades with sub-microsecond latencies.

Built without external runtime dependencies (using only the ISO C++ standard library), the engine is architected for zero-allocation matching loops, cache locality, and algorithmic efficiency.

---

## Architecture Overview

```
                                [ Ingress Feed / CLI ]
                                          │
                                          ▼
                         ┌─────────────────────────────────┐
                         │   Lock-Free SPSC Ring Buffer    │
                         │ (Wait-free Ingest Decoupling)   │
                         └─────────────────────────────────┘
                                          │
                                          ▼
                         ┌─────────────────────────────────┐
                         │   Matching Engine Core (C++20)  │
                         │  Single-Threaded Deterministic  │
                         └─────────────────────────────────┘
                                    │             │
                    ┌───────────────┘             └───────────────┐
                    ▼                                             ▼
       ┌─────────────────────────┐                   ┌─────────────────────────┐
       │   OrderBook (Symbol)    │                   │   Event Notification    │
       │                         │                   │ (Accepted, Trade, Cxl)  │
       │  Bids: std::map (Desc)  │                   └─────────────────────────┘
       │  Asks: std::map (Asc)   │                                │
       │                         │                                ▼
       │  Levels: FIFO Queues    │                   ┌─────────────────────────┐
       │  Index:  std::unordered │                   │ Dynamic or Zero-Cost    │
       │          _map (O(1))    │                   │ Streaming Listeners     │
       └─────────────────────────┘                   └─────────────────────────┘
```

---

## Core Data Structures & Big-O Complexity

### 1. Dual-Sided Book Structure
Each trading symbol maintains an isolated `OrderBook` with two sides:
- **Bid Side**: `std::map<Price, PriceLevel, std::greater<Price>>` (Sorted descending: highest bid price is at `begin()`, O(1) top-of-book).
- **Ask Side**: `std::map<Price, PriceLevel, std::less<Price>>` (Sorted ascending: lowest ask price is at `begin()`, O(1) top-of-book).

### 2. Price Levels as FIFO Queues
Each price level aggregates resting volume and contains a `std::list<Order>` doubly-linked FIFO queue.
- Incoming limit orders append to the tail in $O(1)$ time, preserving arrival time priority.
- Orders at the same price execute strictly in arrival order (earliest first).

### 3. Fast Indexing for $O(1)$ Arbitrary Cancellations
A hash map `std::unordered_map<OrderId, OrderLocation>` maps each `OrderId` to:
- Side (`Buy` or `Sell`)
- Price level key
- Direct bidirectional iterator `std::list<Order>::iterator` pointing directly to the order node.

This yields guaranteed $O(1)$ lookup and $O(1)$ node removal during cancellations. If the price level becomes empty, removing the price node from the red-black tree takes $O(\log M)$.

### Big-O Complexity Summary

| Operation | Time Complexity | Details |
|---|---|---|
| **Add Limit Order (Resting)** | $\mathcal{O}(\log M)$ | $M$ = active price levels. $\mathcal{O}(\log M)$ red-black tree lookup/insertion + $\mathcal{O}(1)$ queue append + $\mathcal{O}(1)$ hash index insertion. |
| **Cancel Order** | $\mathcal{O}(1)$ amortized | $\mathcal{O}(1)$ hash lookup + $\mathcal{O}(1)$ list node splice/removal. $\mathcal{O}(\log M)$ only when the price level becomes completely empty. |
| **Modify Order (Size Down)** | $\mathcal{O}(1)$ | Preserves FIFO priority. Updates remaining volume in place without moving nodes. |
| **Modify Order (Size Up / Price)** | $\mathcal{O}(\log M)$ | Loses FIFO priority. Erases node and re-inserts at the tail of the new/existing level. |
| **Match at Best Price** | $\mathcal{O}(1)$ per fill | Top of book is at `begin()`. Popping/updating front order takes $\mathcal{O}(1)$. |
| **Top-of-Book Snapshot** | $\mathcal{O}(1)$ | Direct inspection of `bids_.begin()` and `asks_.begin()`. |
| **Book Depth Snapshot (Top $K$)** | $\mathcal{O}(K)$ | Sequential traversal of the first $K$ price levels. |

---

## Design Choices & Alternatives Considered

### Why NOT a Binary Heap / `std::priority_queue`?
A binary heap or `std::priority_queue` provides $\mathcal{O}(1)$ top-of-book lookup and $\mathcal{O}(\log N)$ insertion. However:
1. **Inefficient Arbitrary Cancellations ($\mathcal{O}(N)$)**: In real markets, over 80–90% of submitted limit orders are canceled before execution. In a standard binary heap, locating and removing an arbitrary order requires an $\mathcal{O}(N)$ linear scan, destroying matching throughput.
2. **Lack of Level Aggregation**: Priority queues store individual orders rather than aggregated price levels, making market depth generation ($\mathcal{O}(K)$) expensive and requiring duplicate price nodes.

### Alternative 1: Sorted Flat Vectors
Storing price levels in a contiguous `std::vector` sorted by price enables $\mathcal{O}(\log M)$ binary search (`std::lower_bound`) and excellent CPU cache prefetching. However:
- Inserting a new price level in the middle requires shifting trailing elements ($\mathcal{O}(M)$ memmove). While efficient for shallow books ($M < 50$), it degrades significantly on deep books ($M > 10,000$).

### Alternative 2: Direct-Indexed Dense Arrays (Tick Arrays / Bounded Buckets)
For equities or contracts with a tightly bounded price range (e.g., tick sizes within $\pm 2,000$ ticks of current mid), a direct-indexed array provides $\mathcal{O}(1)$ level lookups:
- `PriceLevel levels[MAX_TICKS]`
- **Pros**: Direct $O(1)$ indexing by `price - base_price`, optimal cache locality.
- **Trade-off**: High memory overhead for sparse books (illiquid assets with wide spreads), and requires re-centering dynamic circular buffers as market prices drift.
- **Conclusion**: The red-black tree (`std::map`) combined with a hash index provides the most robust, general-purpose balance of strict complexity bounds across arbitrary price scales.

---

## Matching Rules & Invariants

1. **Strict Price-Time Priority**:
   - Best price executes first (highest bid, lowest ask).
   - At the same price, earliest arrival time executes first (FIFO).
2. **Passive Execution Price**:
   - All trades execute at the price of the **resting (passive) order**, rewarding liquidity providers with price improvement when crossing.
3. **Partial Fills**:
   - Supported natively. For limit orders, unfilled remaining quantities rest in the book.
4. **Order Types**:
   - **Limit**: Matches immediately against crossing resting liquidity; unfilled remainder rests on the book.
   - **Market**: Executes aggressively against available depth; any unfilled quantity is immediately canceled (never rests).
   - **IOC (Immediate-Or-Cancel)**: Fills as much as possible immediately upon arrival; unfilled remainder is canceled.
   - **FOK (Fill-Or-Kill)**: Tests whether the full order size can be satisfied across current depth. If yes, executes in full; if not, rejected immediately with zero fills.
   - **Post-Only**: Liquidity-provider order. If it would execute immediately against resting orders, it is rejected with `POST_ONLY_WOULD_CROSS`.
5. **Modification Rule**:
   - **Quantity decrease** (same price): **Preserves time priority** (remains in same FIFO position).
   - **Quantity increase or price change**: **Loses time priority** (re-queued at the back of the level).
6. **Self-Trade Prevention (STP)**:
   - Configurable policy (`CancelIncoming`, `CancelResting`, `CancelBoth`, `None`) prevents participant wash trading against their own resting orders.
7. **Strict Integer Arithmetic**:
   - Prices are stored as `int64_t` ticks and quantities as `uint64_t` units. Zero floating-point math prevents IEEE-754 nondeterminism and rounding errors.

---

## Benchmarks & Performance Metrics

Benchmarks executed on Apple Silicon (M-series / ARM64, Clang 21, `-O3 -std=c++20`):

```
========================================================================================================
                                  MATCHING ENGINE PERFORMANCE BENCHMARK                                  
========================================================================================================
Benchmark Scenario                  Iterations    Throughput (ops/s)  p50 (ns)  p90 (ns)  p99 (ns)  p99.9 (ns)
--------------------------------------------------------------------------------------------------------
Add Limit Order (Varying Levels)       1,000,000           5,268,032     125.0     208.0    1084.0      2625.0
Cancel Order (O(1) Hash Map)             500,000           8,460,249      83.0     125.0     208.0       334.0
Aggressive Match Execution               500,000           5,010,786     125.0     125.0     125.0       250.0
Realistic Mixed Workload               1,000,000           2,752,744     167.0     625.0     959.0      2333.0
  (60% Add / 25% Cxl / 10% Match / 5% Mod)
Lock-Free SPSC Queue (Push+Pop)        2,000,000          19,949,030      42.0      42.0      42.0       125.0
========================================================================================================
```

- **Mixed Realistic Workload**: **2.75M+ operations/sec** with **167 ns median latency**.
- **Pure Order Cancellation**: **8.46M operations/sec** with **83 ns median latency**.
- **SPSC Lock-Free Ring Buffer**: **19.9M transfers/sec** (42 ns per push+pop cycle).

---

## Project Structure

```
├── CMakeLists.txt                # Production CMake configuration
├── Makefile                      # Fast zero-dependency developer build system
├── LICENSE                       # MIT License
├── README.md                     # Documentation and architecture specification
├── .gitignore
├── .github/
│   └── workflows/
│       └── ci.yml                # Multi-platform CI (GCC/Clang, ASan/UBSan, benchmarks)
├── include/
│   └── matching_engine/
│       ├── types.hpp             # Strongly-typed identifiers, tick prices, order types, enums
│       ├── order.hpp             # Order state, quantities, trader ID
│       ├── price_level.hpp       # FIFO order queue and aggregated volume
│       ├── order_book.hpp        # Dual-sided book, red-black tree levels, hash index, snapshots
│       ├── engine.hpp            # Matching engine, order routing, STP, validation
│       ├── events.hpp            # Event listeners (Accepted, Trade, Canceled, Modified)
│       ├── memory_pool.hpp       # Slab object allocator for zero heap allocation
│       └── spsc_queue.hpp        # Lock-free single-producer single-consumer ring buffer
├── src/
│   ├── order_book.cpp            # OrderBook insertion, cancellation, modify, snapshots
│   ├── engine.cpp                # Matching logic, sweeps, FOK/IOC/Post-Only execution
│   ├── cli/
│   │   └── main.cpp              # Interactive & file CLI with formatted ASCII depth display
│   └── generator/
│       └── order_gen.cpp         # Synthetic Gaussian/Pareto order flow generator
├── tests/
│   ├── test_framework.hpp        # Lightweight assertion and test harness
│   ├── test_main.cpp             # Test runner entry point
│   ├── test_priority.cpp         # Price & time priority, multi-level sweeps, partial fills
│   ├── test_order_types.cpp      # Market, IOC, FOK, Post-Only, and modify rules
│   ├── test_stp.cpp              # Self-trade prevention policies
│   ├── test_invariants.cpp       # Property/fuzz invariant tests (5,000 random operations)
│   └── test_determinism.cpp      # Deterministic replay and byte-for-byte trade equality
└── bench/
    └── bench_engine.cpp          # Throughput and latency percentile (p50/p90/p99/p99.9) harness
```

---

## Build & Run Instructions

### Prerequisites
- Modern C++20 compiler (`clang++` $\ge 14$ or `g++` $\ge 11$)
- `make` or `cmake` $\ge 3.20$

### 1. Compile and Run with Make (Quickstart)

```bash
# Build all targets (CLI, Generator, Tests, Benchmark)
make

# Run the complete test suite (Unit, Invariants, Determinism)
make test

# Run the performance benchmark suite
make bench

# Run with AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)
make test ASAN=1

# Run the animated order book demo
make run-demo
```

### 2. Compile and Run with CMake

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

# Run tests
ctest --output-on-failure
./engine_tests

# Run benchmark
./engine_bench
```

---

## Interactive CLI & Demo Usage

Launch the interactive CLI driver:
```bash
./bin/engine_cli
```

### Commands:
```
NEW BUY LIMIT AAPL 100 @ 15000 id=1 trader=Alice
NEW SELL LIMIT AAPL 50 @ 15020 id=2 trader=Bob
NEW BUY MARKET AAPL 25 id=3 trader=Charlie
MODIFY id=1 qty=80
PRINT AAPL 10
CANCEL id=1
STATS
HELP
QUIT
```

### Piping Synthetic Order Flow into the CLI:
Generate 500 synthetic orders with mid-price clustering and pipe directly into the CLI:
```bash
./bin/order_gen 500 42 | ./bin/engine_cli
```

### Formatted Terminal Output:
```
=======================================================
 ORDER BOOK: AAPL (Total resting orders: 85)
=======================================================
ORDERS      BID QTY       PRICE       ASK QTY       ORDERS      
-------------------------------------------------------
                          15008       185           2           
                          15007       434           4           
                          15006       283           3           
                          15005       626           4           
                          15004       104           1           
------- Spread: 5 (Best Bid: 14999, Best Ask: 15004) -------
2           200           14999                                 
2           315           14998                                 
4           322           14997                                 
6           544           14996                                 
1           155           14995                                 
=======================================================
```

---

## Possible Extensions

1. **Multi-Symbol Sharding & Thread Pinning**:
   - Pin one matching core thread per core/symbol using `pthread_setaffinity_np`, communicating via dedicated SPSC lock-free queues for zero cross-core cache invalidation.
2. **Binary Market Data Protocol**:
   - Implement NASDAQ ITCH (market data tick-by-tick feed) and OUCH (order entry protocol) encoders/decoders over UDP multicast.
3. **Event Sourcing & AOF Persistence**:
   - Append-only log (AOF) with snapshot checkpointing for instantaneous state recovery and audit compliance.
4. **Hardware Acceleration (Kernel Bypass)**:
   - Integration with Solarflare OpenOnload or DPDK for zero-copy userspace networking.

---

## License
MIT License. Free for educational and commercial portfolio use.
