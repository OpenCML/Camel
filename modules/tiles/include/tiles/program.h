// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

namespace camel::runtime::fact {
class FactStore;
}

namespace camel::tiles {

using Shape = std::vector<int64_t>;
struct DeviceValue;
struct LoopRegion;
struct Argument {
    slot_t value;
    core::type::Type *type;
};

// A producer has ordered typed results; result nodes project them into value edges.
struct ResultType {
    Shape shape;
    DType dtype = DType::F32;
};

struct Node {
    std::string op;
    Shape shape;
    std::vector<size_t> inputs;
    std::vector<int64_t> attributes;
    // Owned host input or scalar constant. Outer runtime values are excluded from source
    // cache keys; embedded loop-body constants contribute their exact bits.
    std::vector<float> data;
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
        resident;                           // Borrowed immutable input, retained through execution.
    std::shared_ptr<const LoopRegion> loop; // Owned recurrence; its body is compiled once.
    std::vector<ResultType> results;        // Nonempty only on multi-result operation producers.
};

struct Launch {
    size_t node;
    std::vector<size_t> members;
    size_t elements;
    std::string name;
    std::string implementation;
    unsigned blockX = 256, blockY = 1;
    unsigned gridX = 1, gridY = 1;
    std::vector<size_t> outputs; // All materialized values written by this one launch.
};
struct ScheduleOptions {
    bool fuseElementwise = true;
    bool parallelMax     = true;
};
// Planning may precede value binding. Execution always requires complete inputs.
enum class InputBindings { Required, Deferred };
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
    size_t graphFactInstances = 0, specializedInstances = 0;
    // Type-only templates bind exactly these nodes, in function parameter order.
    // Embedded constants and captures are never invocation bindings.
    std::vector<size_t> parameterInputs;
};

// A recurrence owns its subprogram and exact iteration values. Body inputs correspond to
// outer Node.inputs (state first, captures afterwards); each lane retains its own state.
// All next-state values are computed before any current state is updated.
struct LoopRegion {
    Program body;
    std::vector<int64_t> domain;
    std::vector<size_t> stateInputs;
    std::vector<size_t> captureInputs;
    std::vector<size_t> nextStates;
};

size_t elements(const Shape &shape);
size_t bytes(const Node &node);
const void *hostData(const Node &node);
Program compile(
    Function *function, const std::vector<Argument> &arguments, runtime::fact::FactStore &facts);
// Closure-free regions with static f32 tensor or f64 scalar parameters. No input buffers needed.
Program compileTypes(Function *function, runtime::fact::FactStore &facts);
void generate(
    Program &program, ScheduleOptions options = {},
    InputBindings bindings = InputBindings::Required);
bool elementwise(const std::string &op);
void validate(const Program &program, InputBindings bindings = InputBindings::Required);
void schedule(
    Program &program, ScheduleOptions options = {},
    InputBindings bindings = InputBindings::Required);
// Rebind a cached launch plan to this invocation's host/resident allocation needs.
void planStorage(Program &program, InputBindings bindings = InputBindings::Required);
void validatePlan(const Program &program, InputBindings bindings = InputBindings::Required);
// The plan of a program, as JSON. `Deferred` reads a type-only template: the plan of a segment
// the outline generated, whose invocation values the call that runs it binds (compileTypes).
std::string inspect(
    const Program &program, InputBindings bindings = InputBindings::Required);
std::vector<std::byte> execute(std::shared_ptr<const Program> program);
std::string benchmark(std::shared_ptr<const Program> program);
std::string deviceInfo();
bool available();

} // namespace camel::tiles
