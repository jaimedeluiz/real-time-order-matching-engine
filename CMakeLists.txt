cmake_minimum_required(VERSION 3.20)
project(RealTimeOrderMatchingEngine
    VERSION 1.0.0
    DESCRIPTION "Ultra-High Performance Price-Time Priority Real-Time Order Matching Engine in Modern C++20"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler Warning Flags
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O3)
endif()

# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if (ENABLE_ASAN)
    message(STATUS "AddressSanitizer and UBSan enabled")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if (ENABLE_TSAN)
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

# Core Library
add_library(matching_engine STATIC
    src/order_book.cpp
    src/engine.cpp
)
target_link_libraries(matching_engine PUBLIC Threads::Threads)

target_include_directories(matching_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# CLI Driver
add_executable(engine_cli
    src/cli/main.cpp
)
target_link_libraries(engine_cli PRIVATE matching_engine)

# Order Flow Generator
add_executable(order_gen
    src/generator/order_gen.cpp
)

# Tests
add_executable(engine_tests
    tests/test_main.cpp
    tests/test_priority.cpp
    tests/test_order_types.cpp
    tests/test_stp.cpp
    tests/test_invariants.cpp
    tests/test_determinism.cpp
)
target_include_directories(engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine_tests PRIVATE matching_engine)

enable_testing()
add_test(NAME AllTests COMMAND engine_tests)

# Benchmark Suite
add_executable(engine_bench
    bench/bench_engine.cpp
)
target_include_directories(engine_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(engine_bench PRIVATE matching_engine)
