#include "TestFramework.h"

#include <cstring>

// Usage: tactus_tests [substring]   -- runs every test whose name contains the substring.
int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : "";
    int run = 0, failedTests = 0;

    for (const auto& test : tt::registry())
    {
        if (std::strstr (test.name, filter) == nullptr)
            continue;

        const int before = tt::failureCount();
        std::printf ("[ RUN  ] %s\n", test.name);
        test.function();
        ++run;
        if (tt::failureCount() != before)
        {
            ++failedTests;
            std::printf ("[ FAIL ] %s\n", test.name);
        }
        else
        {
            std::printf ("[  OK  ] %s\n", test.name);
        }
    }

    std::printf ("\n%d tests run, %d failed\n", run, failedTests);
    if (run == 0)
    {
        std::printf ("No test matched \"%s\".\n", filter);
        return 2;
    }
    return failedTests == 0 ? 0 : 1;
}
