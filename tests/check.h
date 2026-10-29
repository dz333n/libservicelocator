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
