// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A test harness small enough to read in one sitting. The core library has no
// external dependencies and its tests should not add one.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace testing {

inline int g_failures = 0;
inline int g_checks = 0;
inline const char* g_current = "";
inline std::chrono::steady_clock::time_point g_tick = std::chrono::steady_clock::now();

// Each TEST() prints its own duration, so a slow test is found by reading the
// output and not by staring at the wall clock. The first TEST in a suite has
// nothing to time yet, so startTest only prints from the second one on.
inline void finishTest() {
    if (*g_current) {
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - g_tick)
                              .count();
        std::printf("    %7.1f ms\n", ms);
    }
}

inline void startTest(const char* name) {
    finishTest();
    g_current = name;
    g_tick = std::chrono::steady_clock::now();
    std::printf("  - %s", name);
}

inline void fail(const char* file, int line, const std::string& what) {
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s\n    %s:%d\n    %s\n", g_current, file, line, what.c_str());
}

inline void check(bool ok, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!ok) fail(file, line, std::string("expected: ") + expr);
}

// std::to_string covers numbers and nothing else, so a failed comparison of two
// strings would not compile rather than reporting.
inline std::string describe(const std::string& value) { return "\"" + value + "\""; }
inline std::string describe(const char* value) { return std::string("\"") + value + "\""; }
template <typename T>
std::string describe(const T& value) {
    return std::to_string(value);
}

template <typename A, typename B>
void checkEqual(const A& a, const B& b, const char* file, int line, const char* expr) {
    ++g_checks;
    if (!(a == b)) {
        fail(file, line, std::string("expected equal: ") + expr + "\n    left  = " +
                             describe(a) + "\n    right = " + describe(b));
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

// A shared build machine is virtualised, short of cores and sitting next to
// somebody else's build, so a check that compares two durations measures the
// neighbours as much as it measures us. Tests that time things say so and step
// aside there rather than reporting a failure they cannot substantiate.
inline bool onSharedHardware() {
    const char* ci = std::getenv("CI");
    return ci && *ci && std::string(ci) != "false";
}

inline void skip(const char* why) { std::printf("    skipped: %s\n", why); }

inline int summarise(const char* suite) {
    finishTest();
    if (g_failures == 0) {
        std::printf("%s: %d checks passed\n", suite, g_checks);
        return 0;
    }
    std::printf("%s: %d of %d checks FAILED\n", suite, g_failures, g_checks);
    return 1;
}

}  // namespace testing

#define TEST(name) testing::startTest(name)

#define CHECK(expr) testing::check(static_cast<bool>(expr), __FILE__, __LINE__, #expr)
#define CHECK_EQ(a, b) testing::checkEqual((a), (b), __FILE__, __LINE__, #a " == " #b)
#define CHECK_NEAR(a, b, tol) testing::checkNear((a), (b), (tol), __FILE__, __LINE__, #a " ~ " #b)
