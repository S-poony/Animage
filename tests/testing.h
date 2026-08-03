// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A test harness small enough to read in one sitting. The core library has no
// external dependencies and its tests should not add one.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace testing {

inline int g_failures = 0;
inline int g_checks = 0;
inline const char* g_current = "";

inline void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s\n    %s:%d\n    %s\n", g_current, file, line, what.c_str());
}

inline void check(bool ok, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!ok) fail(file, line, std::string("expected: ") + expr);
}

template <typename A, typename B>
void checkEqual(const A& a, const B& b, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!(a == b)) {
        fail(file, line, std::string("expected equal: ") + expr + "\n    left  = " +
                             std::to_string(a) + "\n    right = " + std::to_string(b));
    }
}

inline void checkNear(double a, double b, double tol, const char* file, int line,
                      const char* expr) {
    ++g_checks;
    if (!(std::fabs(a - b) <= tol)) {
        fail(file, line, std::string("expected near: ") + expr + "\n    left  = " +
                             std::to_string(a) + "\n    right = " + std::to_string(b));
    }
}

inline int summarise(const char* suite) {
    if (g_failures == 0) {
        std::printf("%s: %d checks passed\n", suite, g_checks);
        return 0;
    }
    std::printf("%s: %d of %d checks FAILED\n", suite, g_failures, g_checks);
    return 1;
}

}  // namespace testing

#define TEST(name)                     \
    testing::g_current = name;         \
    std::printf("  - %s\n", name);

#define CHECK(expr) testing::check(static_cast<bool>(expr), __FILE__, __LINE__, #expr)
#define CHECK_EQ(a, b) testing::checkEqual((a), (b), __FILE__, __LINE__, #a " == " #b)
#define CHECK_NEAR(a, b, tol) testing::checkNear((a), (b), (tol), __FILE__, __LINE__, #a " ~ " #b)
