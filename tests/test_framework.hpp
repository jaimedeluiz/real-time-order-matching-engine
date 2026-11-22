#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <exception>
#include <sstream>

namespace test_framework {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct AutoRegister {
    AutoRegister(const std::string& name, std::function<void()> func) {
        get_registry().push_back({name, std::move(func)});
    }
};

class TestFailure : public std::runtime_error {
public:
    TestFailure(const std::string& expr, const char* file, int line, const std::string& msg = "")
        : std::runtime_error(format(expr, file, line, msg)) {}

private:
    static std::string format(const std::string& expr, const char* file, int line, const std::string& msg) {
        std::ostringstream oss;
        oss << "\n  \033[1;31mFAILED:\033[0m REQUIRE(" << expr << ") at " << file << ":" << line;
        if (!msg.empty()) {
            oss << "\n  Message: " << msg;
        }
        return oss.str();
    }
};

inline int run_all_tests() {
    auto& tests = get_registry();
    size_t passed = 0;
    size_t failed = 0;

    std::cout << "\n=======================================================\n";
    std::cout << " Running " << tests.size() << " Test Cases in Suite\n";
    std::cout << "=======================================================\n";

    for (const auto& test : tests) {
        std::cout << "  RUNNING: " << test.name << " ... ";
        std::cout.flush();
        try {
            test.func();
            std::cout << "\033[1;32m[PASS]\033[0m\n";
            passed++;
        } catch (const TestFailure& tf) {
            std::cout << "\033[1;31m[FAIL]\033[0m" << tf.what() << "\n";
            failed++;
        } catch (const std::exception& e) {
            std::cout << "\033[1;31m[UNEXPECTED EXCEPTION]\033[0m: " << e.what() << "\n";
            failed++;
        } catch (...) {
            std::cout << "\033[1;31m[UNKNOWN EXCEPTION]\033[0m\n";
            failed++;
        }
    }

    std::cout << "-------------------------------------------------------\n";
    if (failed == 0) {
        std::cout << "\033[1;32mAll " << passed << " tests PASSED successfully!\033[0m\n";
    } else {
        std::cout << "\033[1;31m" << failed << " of " << (passed + failed) << " tests FAILED!\033[0m\n";
    }
    std::cout << "=======================================================\n\n";

    return (failed == 0) ? 0 : 1;
}

} // namespace test_framework

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name) \
    static void TEST_CONCAT(test_func_, __LINE__)(); \
    static ::test_framework::AutoRegister TEST_CONCAT(register_test_, __LINE__)(name, TEST_CONCAT(test_func_, __LINE__)); \
    static void TEST_CONCAT(test_func_, __LINE__)()

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            throw ::test_framework::TestFailure(#expr, __FILE__, __LINE__); \
        } \
    } while (false)

#define REQUIRE_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::ostringstream ss; \
            ss << "Expected " << (a) << " == " << (b); \
            throw ::test_framework::TestFailure(#a " == " #b, __FILE__, __LINE__, ss.str()); \
        } \
    } while (false)

#define CHECK(expr) REQUIRE(expr)
