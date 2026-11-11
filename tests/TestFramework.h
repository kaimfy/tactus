#pragma once

// A deliberately tiny test harness: no dependency to download, nothing to learn.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace tt
{

struct TestCase
{
    const char* name;
    void (*function)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount()
{
    static int failures = 0;
    return failures;
}

struct Registrar
{
    Registrar (const char* name, void (*function)()) { registry().push_back ({ name, function }); }
};

inline void reportFailure (const char* file, int line, const std::string& message)
{
    ++failureCount();
    std::printf ("    FAILED %s:%d: %s\n", file, line, message.c_str());
}

} // namespace tt

#define TEST(name)                                              \
    static void name();                                         \
    static const tt::Registrar registrar_##name (#name, name);  \
    static void name()

#define CHECK(condition)                                                    \
    do { if (! (condition)) tt::reportFailure (__FILE__, __LINE__, #condition); } while (false)

#define CHECK_EQ(actual, expected)                                                              \
    do { const auto a_ = (actual); const auto e_ = (expected);                                   \
         if (! (a_ == e_)) tt::reportFailure (__FILE__, __LINE__,                                \
             std::string (#actual " == " #expected "  (got ") + std::to_string (a_)              \
             + ", expected " + std::to_string (e_) + ")"); } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                 \
    do { const double a_ = (double) (actual); const double e_ = (double) (expected);             \
         if (! (std::abs (a_ - e_) <= (tolerance))) tt::reportFailure (__FILE__, __LINE__,       \
             std::string (#actual " ~= " #expected "  (got ") + std::to_string (a_)              \
             + ", expected " + std::to_string (e_) + ")"); } while (false)
