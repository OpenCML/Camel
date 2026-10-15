/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Owned, shape-specialized computation and launch plan. No GC pointers survive compilation.
#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "tiles/dtype.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Function;
namespace camel::core::type {
class Type;
}

namespace camel::tiles {

using Shape = std::vector<int64_t>;
struct DeviceValue;
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
    std::optional<uint64_t>
        scalarBits; // Owned runtime f64 input, including exact special-value bits.
    // reindex: source linear address = offset + sum(output_coordinate[d] * strides[d]).
    // Units are elements, independent of CUDA thread/block placement.
    size_t offset = 0;
    std::vector<size_t> strides;
    DType dtype                           = DType::F32;
    core::sem::PermissionMask permissions = core::sem::kNoPermissions;
    size_t semantic =
        SIZE_MAX; // Originating operator contract; absent for native/synthetic inputs.
    // Owned core declaration instance, specialized to this call; no graph or GC pointers.
    std::optional<core::sem::IndexedInstance> indexing;
    std::shared_ptr<const DeviceValue>
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
    struct OperatorContract {
        std::string uri;
        std::optional<core::sem::OperatorKey> key;
        // Core declaration records are immutable and live until process exit.
        // This non-GC pointer is only a process-local revision token, never persisted.
        const core::sem::OperatorSemantics *declaration = nullptr;
    };
    std::vector<OperatorContract> contracts; // No graph or collector pointers.
    std::vector<Node> nodes;
    std::vector<size_t> outputs = {0}; // Ordered tensor leaves of the Camel result.
    std::string source;
    std::vector<Launch> launches;
    StoragePlan storage;
};

size_t elements(const Shape &shape);
size_t bytes(const Node &node);
const void *hostData(const Node &node);
Program compile(Function *function, const std::vector<Argument> &arguments);
void generate(Program &program, ScheduleOptions options = {});
bool elementwise(const std::string &op);
void validate(const Program &program);
void schedule(Program &program, ScheduleOptions options = {});
void validatePlan(const Program &program);
std::string inspect(const Program &program);
std::vector<std::byte> execute(std::shared_ptr<const Program> program);
std::string benchmark(std::shared_ptr<const Program> program);
std::string deviceInfo();
bool available();

} // namespace camel::tiles
