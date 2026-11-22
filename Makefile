CXX ?= clang++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Wpedantic -pthread -Iinclude -I.
LDFLAGS = -pthread

ifeq ($(ASAN),1)
    CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
    LDFLAGS += -fsanitize=address,undefined
endif

ifeq ($(TSAN),1)
    CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer -g
    LDFLAGS += -fsanitize=thread
endif

BIN_DIR = bin
BUILD_DIR = build

LIB_SRCS = src/order_book.cpp src/engine.cpp
LIB_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SRCS))

TEST_SRCS = tests/test_main.cpp tests/test_priority.cpp tests/test_order_types.cpp tests/test_stp.cpp tests/test_invariants.cpp tests/test_determinism.cpp
TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))

CLI_SRC = src/cli/main.cpp
GEN_SRC = src/generator/order_gen.cpp
BENCH_SRC = bench/bench_engine.cpp

CLI_BIN = $(BIN_DIR)/engine_cli
GEN_BIN = $(BIN_DIR)/order_gen
TEST_BIN = $(BIN_DIR)/engine_tests
BENCH_BIN = $(BIN_DIR)/engine_bench

.PHONY: all clean test bench run-cli run-demo dirs help

all: dirs $(CLI_BIN) $(GEN_BIN) $(TEST_BIN) $(BENCH_BIN)
	@echo "\033[1;32mBuild complete! Binaries located in ./$(BIN_DIR)\033[0m"

dirs:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR) $(BUILD_DIR)/tests

$(BUILD_DIR)/%.o: src/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CLI_BIN): $(LIB_OBJS) $(CLI_SRC) | dirs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(CLI_SRC) $(LIB_OBJS) -o $@

$(GEN_BIN): $(GEN_SRC) | dirs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(GEN_SRC) -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS) | dirs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(TEST_OBJS) $(LIB_OBJS) -o $@

$(BENCH_BIN): $(LIB_OBJS) $(BENCH_SRC) | dirs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(BENCH_SRC) $(LIB_OBJS) -o $@

test: $(TEST_BIN)
	@./$(TEST_BIN)

bench: $(BENCH_BIN)
	@./$(BENCH_BIN)

run-cli: $(CLI_BIN)
	@./$(CLI_BIN)

run-demo: $(CLI_BIN) $(GEN_BIN)
	@./$(GEN_BIN) 200 42 | ./$(CLI_BIN)

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)

help:
	@echo "Available targets:"
	@echo "  make          - Build all targets (engine_cli, order_gen, engine_tests, engine_bench)"
	@echo "  make test     - Compile and run all unit, property, and determinism tests"
	@echo "  make bench    - Compile and run throughput and latency percentile benchmarks"
	@echo "  make run-cli  - Launch interactive matching engine CLI"
	@echo "  make run-demo - Pipe 200 synthetic orders into the CLI to view matching and depth"
	@echo "  make clean    - Remove build artifacts and binaries"
	@echo "  make ASAN=1   - Build with AddressSanitizer and UndefinedBehaviorSanitizer"
