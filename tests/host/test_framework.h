#pragma once

// Minimal, dependency-free test framework for host-side regression tests
// (issue #14). Deliberately not a vendored library: the whole point of these
// tests is that they build and run with nothing more than a native C++17
// compiler, no network fetch and no Pico toolchain required.

#include <cstdio>
#include <string>
#include <vector>

namespace host_tests
{

using TestFn = void (*)();

struct TestCase
{
    const char* name;
    TestFn fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar(const char* name, TestFn fn) { registry().push_back({name, fn}); }
};

inline int g_failures_in_current_test = 0;
inline const char* g_current_test_name = "";

inline void record_failure(const char* file, int line, const std::string& message)
{
    std::printf("    FAIL %s:%d: %s\n", file, line, message.c_str());
    ++g_failures_in_current_test;
}

} // namespace host_tests

#define HOST_TEST(test_name)                                                                     \
    void test_name();                                                                            \
    static host_tests::Registrar registrar_##test_name(#test_name, &test_name);                  \
    void test_name()

#define EXPECT_TRUE(condition)                                                                    \
    do                                                                                            \
    {                                                                                             \
        if (!(condition))                                                                         \
        {                                                                                         \
            host_tests::record_failure(__FILE__, __LINE__, "expected true: " #condition);         \
        }                                                                                          \
    } while (0)

#define EXPECT_FALSE(condition)                                                                   \
    do                                                                                            \
    {                                                                                             \
        if (condition)                                                                            \
        {                                                                                         \
            host_tests::record_failure(__FILE__, __LINE__, "expected false: " #condition);        \
        }                                                                                          \
    } while (0)

#define EXPECT_EQ(actual, expected)                                                                \
    do                                                                                            \
    {                                                                                             \
        if (!((actual) == (expected)))                                                            \
        {                                                                                         \
            host_tests::record_failure(__FILE__, __LINE__,                                        \
                                        std::string("expected ") + #actual + " == " + #expected +  \
                                            " (actual=" + std::to_string(static_cast<long long>(actual)) + \
                                            ", expected=" + std::to_string(static_cast<long long>(expected)) + ")"); \
        }                                                                                          \
    } while (0)
