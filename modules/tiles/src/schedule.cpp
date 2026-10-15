/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Checked tensor schedules and interval-based storage reuse for one ordered stream.
#include "tiles/device.h"
#include "tiles/program.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace camel::tiles {
size_t bytes(const Node &node) {
    const size_t width = node.dtype == DType::F64 ? sizeof(double) : sizeof(float);
    const size_t count = elements(node.shape);
    if (count > SIZE_MAX / width)
        throw std::runtime_error("typed tensor byte size overflows");
    return count * width;
}
size_t elements(const Shape &shape) {
    size_t count = 1;
    for (int64_t d : shape) {
        if (d < 0 || static_cast<uint64_t>(d) >
                         std::numeric_limits<size_t>::max() / std::max(count, size_t(1)))
            throw std::runtime_error("invalid or overflowing tensor shape");
        count *= static_cast<size_t>(d);
    }
    if (count > std::numeric_limits<size_t>::max() / sizeof(float))
        throw std::runtime_error("tensor byte size overflows");
    return count;
}

namespace {
bool unary(const std::string &op) {
    return std::string("|neg|abs|exp|log|sqrt|rsqrt|relu|").find("|" + op + "|") !=
           std::string::npos;
}
bool binary(const std::string &op) {
    return std::string("|add|subtract|multiply|divide|maximum|minimum|").find("|" + op + "|") !=
           std::string::npos;
}
bool indexedScalar(const std::string &op) {
    return unary(op) || binary(op) || op == "reshape" || op == "reindex";
}
bool scalar64(const std::string &op) {
    return op == "constant64" || op == "scalar_add" || op == "scalar_sub" || op == "scalar_mul" ||
           op == "scalar_div" || op == "scalar_neg";
}
bool orderedAxis(const std::string &op) { return op == "sum_axis" || op == "mean_axis"; }
void require(bool valid, const std::string &message) {
    if (!valid)
        throw std::runtime_error("invalid tiles plan: " + message);
}
Shape broadcast(const Shape &a, const Shape &b) {
    Shape result(std::max(a.size(), b.size()), 1);
    for (size_t i = 0; i < result.size(); ++i) {
        int64_t x = i < a.size() ? a[a.size() - 1 - i] : 1;
        int64_t y = i < b.size() ? b[b.size() - 1 - i] : 1;
        require(x == y || x == 1 || y == 1, "incompatible broadcast extents");
        result[result.size() - 1 - i] = x == 1 ? y : x;
    }
    return result;
}
struct Lifetime {
    size_t begin = 0, end = 0;
    bool materialized = false;
};
std::vector<Lifetime> lifetimes(const Program &p) {
    std::vector<Lifetime> life(p.nodes.size());
    for (size_t id = 0; id < p.nodes.size(); ++id)
        if (p.nodes[id].op == "input")
            life[id].materialized = true;
    for (size_t stage = 0; stage < p.launches.size(); ++stage) {
        const auto &launch = p.launches[stage];
        life[launch.node]  = {stage + 1, stage + 1, true};
        for (auto id : launch.members)
            for (auto in : p.nodes[id].inputs)
                if (std::find(launch.members.begin(), launch.members.end(), in) ==
                    launch.members.end())
                    life[in].end = std::max(life[in].end, stage + 1);
    }
    for (auto output : p.outputs)
        life[output].end = p.launches.size() + 1;
    return life;
}
} // namespace

bool elementwise(const std::string &op) { return unary(op) || binary(op); }

void validate(const Program &p) {
    require(!p.nodes.empty() && !p.outputs.empty(), "missing output");
    size_t outputBytes = 0;
    for (auto output : p.outputs) {
        require(output < p.nodes.size(), "missing output");
        const size_t count = bytes(p.nodes[output]);
        require(count <= SIZE_MAX - outputBytes, "combined output size overflow");
        outputBytes += count;
    }
    for (size_t id = 0; id < p.nodes.size(); ++id) {
        const auto &n      = p.nodes[id];
        const size_t count = elements(n.shape);
        bytes(n);
        for (auto in : n.inputs)
            require(in < id, "dependency is not topologically earlier");
        if (n.op == "input") {
            require(
                n.dtype == DType::F32 || (n.resident && n.shape.empty()),
                "input payload dtype");
            require(n.inputs.empty(), "input has dependencies");
            if (n.resident)
                require(
                    n.data.empty() && n.resident->shape == n.shape &&
                        n.resident->dtype == n.dtype && n.resident->owner && n.resident->address,
                    "invalid resident input");
            else
                require(n.data.size() == count, "invalid input payload");
            continue;
        }
        require(!n.resident, "computed value borrows input storage");
        require(n.data.empty(), "computed value has host payload");
        if (n.op == "scalar_add" || n.op == "scalar_sub" || n.op == "scalar_mul" ||
            n.op == "scalar_div" || n.op == "scalar_neg") {
            require(
                n.dtype == DType::F64 && n.shape.empty() && n.attributes.empty() &&
                    n.inputs.size() == (n.op == "scalar_neg" ? 1 : 2),
                "scalar arithmetic type or arity");
            for (auto in : n.inputs)
                require(
                    p.nodes[in].dtype == DType::F64 && p.nodes[in].shape.empty(),
                    "scalar operand type");
            continue;
        }
        if (n.op == "sum_all" || n.op == "mean_all") {
            require(
                n.dtype == DType::F64 && n.shape.empty() && n.inputs.size() == 1 &&
                    p.nodes[n.inputs[0]].dtype == DType::F32,
                "scalar reduction type");
            require(
                n.op != "mean_all" || elements(p.nodes[n.inputs[0]].shape),
                "mean of empty tensor");
            continue;
        }
        if (n.op == "constant64") {
            require(
                n.dtype == DType::F64 && n.shape.empty() && n.inputs.empty() &&
                    n.attributes.size() == 1,
                "scalar constant type");
            continue;
        }
        if (n.op == "cast_f32") {
            require(
                n.dtype == DType::F32 && n.shape.empty() && n.inputs.size() == 1 &&
                    p.nodes[n.inputs[0]].dtype == DType::F64,
                "scalar conversion type");
            continue;
        }
        require(n.dtype == DType::F32, "unsupported operation dtype");
        for (auto in : n.inputs)
            require(p.nodes[in].dtype == DType::F32, "operand dtype mismatch");
        const bool reduction = n.op == "sum_axis" || n.op == "max_axis" || n.op == "mean_axis";
        require(
            elementwise(n.op) || reduction || n.op == "sum_to" || n.op == "reshape" ||
                n.op == "matmul" || n.op == "reindex",
            "unknown operation");
        require(n.inputs.size() == (binary(n.op) || n.op == "matmul" ? 2 : 1), "operation arity");
        const auto &a = p.nodes[n.inputs[0]].shape;
        if (n.op == "sum_to")
            require(
                broadcast(n.shape, a) == a && n.shape.size() <= a.size(),
                "sum_to broadcast domain");
        if (n.op == "reindex") {
            require(n.strides.size() == n.shape.size(), "index map rank");
            if (count) {
                size_t last = n.offset;
                for (size_t d = 0; d < n.shape.size(); ++d) {
                    const size_t extent = size_t(n.shape[d] - 1);
                    require(
                        !extent || n.strides[d] <= (SIZE_MAX - last) / extent,
                        "index map address overflow");
                    last += extent * n.strides[d];
                }
                require(last < elements(a), "index map outside source domain");
            }
        } else {
            require(n.offset == 0 && n.strides.empty(), "unexpected index map");
        }
        if (unary(n.op))
            require(a == n.shape, "unary result shape");
        if (binary(n.op))
            require(broadcast(a, p.nodes[n.inputs[1]].shape) == n.shape, "binary result shape");
        if (n.op == "reshape")
            require(elements(a) == count, "reshape element count");
        if (n.op == "matmul") {
            const auto &b = p.nodes[n.inputs[1]].shape;
            require(a.size() == 2 && b.size() == 2, "matmul rank");
            require(a[1] == b[0] && n.shape == Shape{a[0], b[1]}, "matmul dimensions");
        }
        if (reduction) {
            require(
                n.attributes.size() == 1 && n.attributes[0] >= 0 &&
                    size_t(n.attributes[0]) < a.size(),
                "reduction axis");
            size_t axis = n.attributes[0];
            require(n.op != "max_axis" || a[axis] != 0, "empty maximum");
            Shape kept = a, removed = a;
            kept[axis] = 1;
            removed.erase(removed.begin() + axis);
            require(n.shape == kept || n.shape == removed, "reduction output shape");
        }
    }
}

void schedule(Program &p, ScheduleOptions options) {
    validate(p);
    const size_t count = p.nodes.size();
    std::vector<size_t> users(count), consumer(count, SIZE_MAX), owner(count), groupSize(count, 1);
    std::iota(owner.begin(), owner.end(), 0);
    for (size_t id = 0; id < count; ++id)
        for (auto in : p.nodes[id].inputs) {
            ++users[in];
            consumer[in] = id;
        }
    // Reverse traversal attaches single-use scalar computations to their consumer.
    // Each dependency carries its own logical index; layouts need not share a domain.
    for (size_t id = count; id-- > 0;) {
        size_t target = consumer[id];
        if (!options.fuseElementwise ||
            std::find(p.outputs.begin(), p.outputs.end(), id) != p.outputs.end() ||
            users[id] != 1 ||
            !((indexedScalar(p.nodes[id].op) &&
               (indexedScalar(p.nodes[target].op) || orderedAxis(p.nodes[target].op))) ||
              (scalar64(p.nodes[id].op) && scalar64(p.nodes[target].op))))
            continue;
        size_t root = owner[target];
        // Ordered reductions expose only output-row parallelism. Keep expensive
        // math in its pointwise stage unless at least 64 warps of rows exist.
        // A few output warps leave most SMs idle while serializing math over
        // the reduction axis (observed on both RTX 4060 Ti and RTX 3090).
        // Cheap producer chains can still fuse on either side of this boundary.
        const auto &op = p.nodes[id].op;
        if (orderedAxis(p.nodes[root].op) && elements(p.nodes[root].shape) < 2048 &&
            (op == "exp" || op == "log" || op == "sqrt" || op == "rsqrt"))
            continue;
        if (groupSize[root] == 64)
            continue;
        owner[id] = root;
        ++groupSize[root];
    }
    // Attach one ordered reduction to a same-domain pointwise epilogue. Domain
    // changes and multiple reductions stay separate, avoiding recomputation.
    for (size_t id = count; id-- > 0;) {
        if (!options.fuseElementwise || !orderedAxis(p.nodes[id].op) || users[id] != 1 ||
            std::find(p.outputs.begin(), p.outputs.end(), id) != p.outputs.end())
            continue;
        const size_t root = owner[consumer[id]];
        if (root == id || groupSize[root] + groupSize[id] > 64)
            continue;
        bool compatible = true;
        for (size_t member = 0; member < count; ++member)
            if (owner[member] == root &&
                (!elementwise(p.nodes[member].op) || p.nodes[member].shape != p.nodes[id].shape))
                compatible = false;
        if (!compatible)
            continue;
        for (auto &group : owner)
            if (group == id)
                group = root;
        groupSize[root] += groupSize[id];
    }
    p.launches.clear();
    for (size_t id = 0; id < count; ++id) {
        if (p.nodes[id].op == "input" || owner[id] != id)
            continue;
        Launch launch{
            .node     = id,
            .elements = elements(p.nodes[id].shape),
            .name     = "tiles_" + std::to_string(id)};
        for (size_t member = 0; member <= id; ++member)
            if (owner[member] == id)
                launch.members.push_back(member);
        launch.implementation = "standard";
        const auto &n         = p.nodes[id];
        if (launch.members.size() > 1 &&
            std::any_of(launch.members.begin(), launch.members.end(), [&](size_t member) {
                return orderedAxis(p.nodes[member].op);
            })) {
            launch.implementation = "ordered_reduce";
            launch.blockX         = 32;
        }
        if (options.parallelMax && n.op == "max_axis") {
            const auto &shape = p.nodes[n.inputs[0]].shape;
            if (size_t(n.attributes[0]) + 1 == shape.size() && shape.back() >= 128)
                launch.implementation = "parallel_max";
        }
        p.launches.push_back(std::move(launch));
    }
    const auto life = lifetimes(p);
    p.storage       = {};
    p.storage.slots.assign(count, SIZE_MAX);
    std::vector<size_t> order;
    for (size_t id = 0; id < count; ++id)
        if (life[id].materialized && !p.nodes[id].resident)
            order.push_back(id);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return life[a].begin < life[b].begin;
    });
    std::vector<size_t> until;
    for (auto id : order) {
        size_t bytes    = std::max(size_t(1), camel::tiles::bytes(p.nodes[id]));
        size_t selected = SIZE_MAX;
        for (size_t slot = 0; slot < until.size(); ++slot)
            if (until[slot] < life[id].begin && p.storage.bytes[slot] >= bytes &&
                (selected == SIZE_MAX || p.storage.bytes[slot] < p.storage.bytes[selected]))
                selected = slot;
        if (selected == SIZE_MAX) {
            selected = until.size();
            until.push_back(0);
            p.storage.bytes.push_back(bytes);
        }
        until[selected]     = life[id].end;
        p.storage.slots[id] = selected;
    }
    validatePlan(p);
}

void validatePlan(const Program &p) {
    validate(p);
    require(p.storage.slots.size() == p.nodes.size(), "storage mapping size");
    std::vector<size_t> stageOf(p.nodes.size(), SIZE_MAX);
    for (size_t i = 0; i < p.nodes.size(); ++i)
        if (p.nodes[i].op == "input")
            stageOf[i] = 0;
    for (size_t stage = 0; stage < p.launches.size(); ++stage) {
        const auto &launch = p.launches[stage];
        require(
            launch.node < p.nodes.size() && !launch.members.empty() &&
                launch.members.back() == launch.node,
            "invalid stage root");
        require(launch.elements == elements(p.nodes[launch.node].shape), "launch extent");
        require(
            launch.implementation == "standard" || launch.implementation == "parallel_max" ||
                launch.implementation == "ordered_reduce",
            "unknown implementation");
        size_t reduction = SIZE_MAX, reductions = 0;
        for (auto member : launch.members)
            if (member < p.nodes.size() && orderedAxis(p.nodes[member].op)) {
                reduction = member;
                ++reductions;
            }
        if (launch.implementation == "ordered_reduce") {
            require(
                reductions == 1 && launch.blockX == 32 && launch.blockY == 1 &&
                    launch.members.size() > 1,
                "ordered reduction operation or block");
            for (auto member : launch.members)
                if (member > reduction)
                    require(
                        member < p.nodes.size() && elementwise(p.nodes[member].op) &&
                            p.nodes[member].shape == p.nodes[reduction].shape,
                        "ordered reduction epilogue domain");
        }
        if (launch.implementation == "parallel_max") {
            const auto &root = p.nodes[launch.node];
            require(
                root.op == "max_axis" && launch.members.size() == 1,
                "parallel maximum operation");
            const auto &shape = p.nodes[root.inputs[0]].shape;
            require(
                size_t(root.attributes[0]) + 1 == shape.size() && shape.back() >= 128 &&
                    launch.blockX == 256 && launch.blockY == 1,
                "parallel maximum domain or block");
        }
        require(
            launch.blockX > 0 && launch.blockY > 0 && launch.blockX <= 1024 &&
                launch.blockY <= 1024 / launch.blockX && launch.gridX > 0 &&
                launch.gridX <= 2147483647 && launch.gridY > 0 && launch.gridY <= 65535,
            "launch dimensions");
        std::vector<size_t> internalUses(p.nodes.size());
        for (auto id : launch.members) {
            require(
                id < p.nodes.size() && stageOf[id] == SIZE_MAX,
                "duplicate/invalid scheduled value");
            require(
                launch.members.size() == 1 ||
                    (indexedScalar(p.nodes[id].op) && indexedScalar(p.nodes[launch.node].op)) ||
                    (launch.implementation == "ordered_reduce" &&
                     (id == reduction || indexedScalar(p.nodes[id].op))) ||
                    (scalar64(p.nodes[id].op) && scalar64(p.nodes[launch.node].op)),
                "invalid fused domain");
            for (auto in : p.nodes[id].inputs) {
                require(
                    stageOf[in] != SIZE_MAX && stageOf[in] <= stage + 1,
                    "unscheduled dependency");
                require(
                    stageOf[in] == stage + 1 || p.storage.slots[in] != SIZE_MAX ||
                        p.nodes[in].resident,
                    "register value escapes stage");
                if (stageOf[in] == stage + 1)
                    ++internalUses[in];
            }
            stageOf[id] = stage + 1;
        }
        for (auto id : launch.members)
            require(id == launch.node || internalUses[id] == 1, "fused value must have one use");
    }
    for (auto stage : stageOf)
        require(stage != SIZE_MAX, "unscheduled value");
    const auto life = lifetimes(p);
    for (auto output : p.outputs)
        require(life[output].materialized, "output has no materialized result");
    std::vector<std::vector<size_t>> occupants(p.storage.bytes.size());
    for (size_t id = 0; id < p.nodes.size(); ++id) {
        size_t slot = p.storage.slots[id];
        if (p.nodes[id].resident) {
            require(slot == SIZE_MAX, "borrowed input has writable allocation");
            continue;
        }
        if (!life[id].materialized) {
            require(slot == SIZE_MAX, "register-only value has allocation");
            continue;
        }
        require(
            slot < p.storage.bytes.size() && p.storage.bytes[slot] >= bytes(p.nodes[id]),
            "undersized/missing allocation");
        for (auto other : occupants[slot])
            require(
                life[id].end < life[other].begin || life[other].end < life[id].begin,
                "overlapping buffer lifetimes");
        occupants[slot].push_back(id);
    }
}

std::string inspect(const Program &p) {
    validatePlan(p);
    size_t logicalBytes = 0, allocatedBytes = 0, materialized = 0;
    size_t borrowedBytes = 0, borrowedInputs = 0;
    for (const auto &n : p.nodes)
        logicalBytes += bytes(n);
    for (const auto &n : p.nodes)
        if (n.resident) {
            borrowedBytes += bytes(n);
            ++borrowedInputs;
            ++materialized;
        }
    for (auto bytes : p.storage.bytes)
        allocatedBytes += bytes;
    for (auto slot : p.storage.slots)
        materialized += slot != SIZE_MAX;
    std::ostringstream out;
    out << "{\"schema\":1,\"values\":" << p.nodes.size() << ",\"kernels\":" << p.launches.size()
        << ",\"materialized_values\":" << materialized
        << ",\"allocations\":" << p.storage.bytes.size() << ",\"logical_bytes\":" << logicalBytes
        << ",\"allocated_bytes\":" << allocatedBytes << ",\"borrowed_inputs\":" << borrowedInputs
        << ",\"borrowed_bytes\":" << borrowedBytes << ",\"stages\":[";
    for (size_t i = 0; i < p.launches.size(); ++i) {
        if (i)
            out << ',';
        const auto &s = p.launches[i];
        out << "{\"output\":" << s.node << ",\"implementation\":\"" << s.implementation
            << "\",\"members\":[";
        for (size_t j = 0; j < s.members.size(); ++j) {
            if (j)
                out << ',';
            out << s.members[j];
        }
        out << "]}";
    }
    out << "],\"outputs\":[";
    for (size_t i = 0; i < p.outputs.size(); ++i) {
        if (i)
            out << ',';
        out << p.outputs[i];
    }
    out << "]}";
    return out.str();
}
} // namespace camel::tiles
