/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Owned, shape-specialized computation and launch plan. No GC pointers survive compilation.
#pragma once

#include "camel/core/rtdata/base.h"
#include <cstdint>
#include <string>
#include <vector>

class Function;
namespace camel::core::type {
class Type;
}

namespace camel::tiles {

using Shape = std::vector<int64_t>;
struct Argument {
    slot_t value;
    core::type::Type *type;
};

struct Node {
    std::string op;
    Shape shape;
    std::vector<size_t> inputs;
    std::vector<int64_t> attributes;
    std::vector<float> data; // Owned host input or scalar constant; never part of a code-cache key.
};

struct Launch {
    size_t node;
    std::vector<size_t> members;
    size_t elements;
    std::string name;
    std::string implementation;
    unsigned blockX = 256, blockY = 1;
    unsigned gridX = 1, gridY = 1;
};
struct ScheduleOptions {
    bool fuseElementwise = true;
    bool parallelMax     = true;
};
struct StoragePlan {
    std::vector<size_t> slots; // Node -> physical allocation; SIZE_MAX means register-only.
    std::vector<size_t> bytes;
};
struct Program {
    std::vector<Node> nodes;
    size_t output = 0;
    std::string source;
    std::vector<Launch> launches;
    StoragePlan storage;
};

size_t elements(const Shape &shape);
Program compile(Function *function, const std::vector<Argument> &arguments);
void generate(Program &program, ScheduleOptions options = {});
bool elementwise(const std::string &op);
void validate(const Program &program);
void schedule(Program &program, ScheduleOptions options = {});
void validatePlan(const Program &program);
std::string inspect(const Program &program);
std::vector<float> execute(const Program &program);
std::string benchmark(const Program &program);
std::string deviceInfo();
bool available();

} // namespace camel::tiles
