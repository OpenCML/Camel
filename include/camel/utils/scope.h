// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <iomanip>
#include <iostream>
#include <sstream>

#define _red(x) "\033[31m" << x << "\033[0m"
#define _blue(x) "\033[34m" << x << "\033[0m"
#define _green(x) "\033[32m" << x << "\033[0m"
#define _yellow(x) "\033[33m" << x << "\033[0m"

extern size_t __depth__;

inline std::string rainbowPattern(int depth) {
    static const std::string colors[] = {
        "\033[31m", // Red
        "\033[33m", // Yellow
        "\033[32m", // Green
        "\033[36m", // Cyan
        "\033[34m", // Blue
        "\033[35m", // Magenta
    };
    const int colorCount = sizeof(colors) / sizeof(colors[0]);

    std::stringstream ss;
    for (int i = 0; i < depth; ++i) {
        ss << colors[i % colorCount] << "| " << "\033[0m";
    }
    return ss.str();
}

// #define DEBUG_LEVEL -1

#define ENTER(target)                                                                              \
    do {                                                                                           \
        if (DEBUG_LEVEL >= 0) {                                                                    \
            std::cout << rainbowPattern(__depth__) << _blue("[enter] ") << target << std::endl;    \
        }                                                                                          \
        __depth__++;                                                                               \
    } while (false)

#define LEAVE(target)                                                                              \
    do {                                                                                           \
        __depth__--;                                                                               \
        if (DEBUG_LEVEL >= 0) {                                                                    \
            std::cout << rainbowPattern(__depth__) << _green("[leave] ") << target << std::endl;   \
        }                                                                                          \
    } while (false)
