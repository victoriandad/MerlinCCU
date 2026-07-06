#include <cstdio>

#include "test_framework.h"

int main()
{
    int total = 0;
    int failing = 0;

    for (const auto& test : host_tests::registry())
    {
        host_tests::g_current_test_name = test.name;
        host_tests::g_failures_in_current_test = 0;
        std::printf("RUN  %s\n", test.name);
        test.fn();
        ++total;
        if (host_tests::g_failures_in_current_test > 0)
        {
            ++failing;
            std::printf("FAIL %s (%d failing check(s))\n", test.name,
                        host_tests::g_failures_in_current_test);
        }
        else
        {
            std::printf("PASS %s\n", test.name);
        }
    }

    std::printf("\n%d/%d test case(s) passed\n", total - failing, total);
    return failing == 0 ? 0 : 1;
}
