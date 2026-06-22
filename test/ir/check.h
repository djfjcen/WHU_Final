#pragma once

#include <iostream>
#include <string>

namespace toyc::test {

inline int g_failures = 0;
inline int g_checks = 0;

inline void check(bool cond, const std::string& msg) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "  FAIL: " << msg << "\n";
    }
}

inline void check_eq_str(const std::string& expected, const std::string& actual, const std::string& msg) {
    ++g_checks;
    if (expected == actual) {
        return;
    }
    ++g_failures;
    std::cerr << "  FAIL: " << msg << "\n--- expected ---\n" << expected
              << "\n--- actual ---\n" << actual << "\n";
}

inline int report() {
    std::cerr << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}

}  // namespace toyc::test
