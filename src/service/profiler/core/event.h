// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#ifndef EOF
#define EOF (-1)
#endif

#include "nlohmann/json.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

struct TraceEvent {
    std::string name;
    std::string cat;
    std::string ph;
    uint64_t ts;
    int pid;
    int tid;
    std::unordered_map<std::string, std::string> args;
    std::string scope; // for 'i'
    uint64_t dur = 0;  // for 'X'

    nlohmann::json to_json() const;
};
