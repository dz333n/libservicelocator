#pragma once

#include <cstdio>
#include <cstdlib>

#define SL_CHECK(condition)                                                        \
    do {                                                                           \
        if (!(condition)) {                                                        \
            std::printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

#define SL_FAIL_ON_LOCATOR_ERROR                                                   \
    [[noreturn]] static void OnLocatorError(const char* message) {                 \
        std::printf("servicelocator error: %s\n", message);                        \
        std::exit(1);                                                              \
    }
