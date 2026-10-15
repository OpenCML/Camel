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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Function;
namespace camel::core::type {
class Type;
}

namespace camel::tiles {

using Shape = std::vector<int64_t>;
struct DeviceTensor;
enum class DType { F32, F64 };
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
    // reindex: source linear address = offset + sum(output_coordinate[d] * strides[d]).
    // Units are elements, independent of CUDA thread/block placement.
    size_t offset = 0;
    std::vector<size_t> strides;
    DType dtype = DType::F32;
    std::shared_ptr<const DeviceTensor>
        resident; // Borrowed immutable input, retained through execution.
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
    std::vector<size_t> outputs = {0}; // Ordered tensor leaves of the Camel result.
    std::string source;
    std::vector<Launch> launches;
    StoragePlan storage;
};

size_t elements(const Shape &shape);
size_t bytes(const Node &node);
Program compile(Function *function, const std::vector<Argument> &arguments);
void generate(Program &program, ScheduleOptions options = {});
bool elementwise(const std::string &op);
void validate(const Program &program);
void schedule(Program &program, ScheduleOptions options = {});
void validatePlan(const Program &program);
std::string inspect(const Program &program);
std::vector<std::byte> execute(const Program &program);
std::string benchmark(const Program &program);
std::string deviceInfo();
bool available();

} // namespace camel::tiles
