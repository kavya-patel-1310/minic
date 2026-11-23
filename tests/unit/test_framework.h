#pragma once
// A tiny dependency-free unit test framework.
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Failure {
    std::string message;
};

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <typename A, typename B>
void checkEqual(const A& actual, const B& expected, const char* exprA, const char* exprB, const char* file, int line) {
    if (actual == expected) return;
    std::ostringstream os;
    os << file << ":" << line << ": CHECK_EQ(" << exprA << ", " << exprB << ") failed\n"
       << "      actual: " << actual << "\n"
       << "    expected: " << expected;
    throw Failure{os.str()};
}

inline int runAll() {
    int passed = 0, failed = 0;
    for (const Case& c : registry()) {
        try {
            c.fn();
            ++passed;
        } catch (const Failure& f) {
            ++failed;
            std::cout << "FAIL  " << c.name << "\n  " << f.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL  " << c.name << "\n  unexpected exception: " << e.what() << "\n";
        }
    }
    std::cout << passed << " passed, " << failed << " failed (" << registry().size() << " unit tests)\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace test

#define TEST_CONCAT2(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT2(a, b)
#define TEST(name)                                                                \
    static void TEST_CONCAT(test_fn_, __LINE__)();                                \
    static test::Registrar TEST_CONCAT(test_reg_, __LINE__)(name, TEST_CONCAT(test_fn_, __LINE__)); \
    static void TEST_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) throw test::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) +   \
                                         ": CHECK(" #cond ") failed"};                              \
    } while (0)

#define CHECK_EQ(a, b) test::checkEqual((a), (b), #a, #b, __FILE__, __LINE__)
