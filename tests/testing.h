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

// Whether a sanitizer is instrumenting this build.
//
// The same idea as onSharedHardware and for the same reason: a wall clock is
// measuring the instrumentation as much as the code under it, and a budget that
// holds is not the property being asserted. AddressSanitizer puts a check on
// every load and store, which is a slowdown of five to twenty times -- so a
// timing check under one does not report that the code got slower, it reports
// that the sanitizer is on, which was already known.
//
// **Only ever guard the clock with this.** A sanitized run is exactly the run
// whose correctness assertions matter most, and stepping a whole test aside
// because one check in it is a stopwatch would give up the coverage that the
// sanitizer was built for. See theSolveStaysBoundedOnALargeDrawing in
// test_ctg.cpp, which is the one place this is used: the solve is still checked
// for being valid, large and correctly coloured, and only "under two seconds" is
// let go of.
//
// Address and thread are the two that reach a timing budget. GCC and MSVC both
// define __SANITIZE_ADDRESS__; Clang answers __has_feature instead. UBSan costs
// a few percent rather than a multiple and is deliberately not detected here --
// it is not what breaks a budget, and a build with UBSan alone should still be
// held to one.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
inline constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
inline constexpr bool kSanitized = true;
#else
inline constexpr bool kSanitized = false;
#endif
#else
inline constexpr bool kSanitized = false;
#endif

inline void skip(const char* why) { std::printf("    skipped: %s\n", why); }

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
