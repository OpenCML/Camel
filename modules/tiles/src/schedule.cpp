// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Checked tensor schedules and interval-based storage reuse for one ordered stream.

#include "camel/core/operator_table.h"
#include "tiles/device.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/row_kernel.h"
#include "tiles/program.h"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace camel::tiles {
const void *hostData(const Node &node) {
    if (node.scalarBits)
        return &*node.scalarBits;
    return node.data.empty() ? nullptr : node.data.data();
}
size_t bytes(const Node &node) {
    if (!node.results.empty())
        return 0; // Operation producers carry dependencies; only their results hold values.
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
    return unary(op) || binary(op) || op == "reshape" || op == "reindex" || op == "cast_f32" ||
           op == "cast_f64";
}
bool scalar64(const std::string &op) {
    return op == "constant64" || op == "scalar_add" || op == "scalar_sub" || op == "scalar_mul" ||
           op == "scalar_div" || op == "scalar_neg";
}
bool orderedAxis(const std::string &op) { return op == "sum_axis" || op == "mean_axis"; }
bool parallelMaximum(const Program &p, const Node &n) {
    if (!n.indexing || n.semantic >= p.contracts.size() || !elements(n.shape))
        return false;
    const auto &shape = p.nodes[n.inputs[0]].shape;
    // The block kernel reads through reductionAccess(row, ordinal). Its only
    // shape-derived traversal bound must equal the core contribution count;
    // repeated or otherwise mapped contributions remain valid parallel inputs.
    if (shape.empty() || n.attributes.size() != 1 || n.attributes[0] < 0 ||
        size_t(n.attributes[0]) + 1 != shape.size() ||
        kernel::reductionAccess(p, n, kernel::IndexExpr(0), kernel::IndexExpr(0)).count !=
            size_t(shape.back()))
        return false;
    const auto *declaration = p.contracts[n.semantic].declaration;
    if (!declaration || n.indexing->law >= declaration->laws.size())
        return false;
    const auto &law    = declaration->laws[n.indexing->law];
    const auto freedom = core::sem::reorderFreedom(law, n.permissions);
    return law.kind == core::sem::LawKind::Max && law.input == core::type::TypeCode::Float32 &&
           law.accumulator == core::type::TypeCode::Float32 &&
           law.result == core::type::TypeCode::Float32 &&
           law.finalize == core::sem::LawFinalize::None && law.initBits == 0xff800000U &&
           freedom.regroup && freedom.permute && freedom.blockIdentity;
}
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
        for (auto output : launch.outputs)
            life[output] = {stage + 1, stage + 1, true};
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

void validate(const Program &p, InputBindings bindings) {
    require(!p.nodes.empty() && !p.outputs.empty(), "missing output");
    auto &table                 = core::OperatorTable::instance();
    const auto declarationEpoch = table.declarationEpoch();
    for (const auto &contract : p.contracts) {
        const auto id = table.lookup(contract.uri);
        require(id.has_value(), "operator contract is no longer registered");
        require(
            table.semantics(*id) == contract.declaration,
            "operator declaration changed; recompile the graph");
        if (contract.key)
            require(
                contract.key->uri() == contract.uri && table.bind(*contract.key) == id,
                "operator semantics changed; recompile the graph");
        else
            require(
                table.semantics(*id) == nullptr,
                "operator gained a semantic declaration; recompile the graph");
    }
    require(
        table.declarationEpoch() == declarationEpoch,
        "operator declarations changed during validation; recompile the graph");
    size_t outputBytes = 0;
    for (auto output : p.outputs) {
        require(output < p.nodes.size(), "missing output");
        require(p.nodes[output].results.empty(), "operation producer used as output value");
        const size_t count = bytes(p.nodes[output]);
        require(count <= SIZE_MAX - outputBytes, "combined output size overflow");
        outputBytes += count;
    }
    for (size_t id = 0; id < p.nodes.size(); ++id) {
        const auto &n = p.nodes[id];
        require(n.op == "loop" || !n.loop, "nonloop node owns a recurrence");
        require(
            n.semantic == SIZE_MAX || n.semantic < p.contracts.size(),
            "missing operator contract");
        if (n.indexing) {
            require(
                n.semantic < p.contracts.size() && p.contracts[n.semantic].declaration,
                "index instance has no operator declaration");
            const auto problem =
                core::sem::validate(*n.indexing, p.contracts[n.semantic].declaration);
            require(!problem, "invalid core index instance: " + problem.value_or(""));
        }
        const size_t count = elements(n.shape);
        bytes(n);
        for (auto in : n.inputs)
            require(in < id, "dependency is not topologically earlier");
        require(n.op == "loop" || n.results.empty(), "nonproducer owns result descriptors");
        if (n.op == "result") {
            require(
                n.inputs.size() == 1 && n.attributes.size() == 1 && n.attributes[0] >= 0,
                "result projection arity or index");
            const auto &producer = p.nodes[n.inputs[0]];
            require(size_t(n.attributes[0]) < producer.results.size(), "result projection index");
            const auto &type = producer.results[size_t(n.attributes[0])];
            require(n.shape == type.shape && n.dtype == type.dtype, "result projection type");
            require(
                !n.resident && n.data.empty() && !n.scalarBits && !n.indexing && n.offset == 0 &&
                    n.strides.empty(),
                "result projection has payload");
            continue;
        }
        for (auto in : n.inputs)
            require(p.nodes[in].results.empty(), "operation producer used as operand value");
        if (n.op == "input") {
            require(
                n.dtype == DType::F32 || (n.dtype == DType::F64 && n.shape.empty()),
                "input payload dtype");
            require(n.inputs.empty(), "input has dependencies");
            if (bindings == InputBindings::Deferred && !n.resident && n.data.empty() &&
                !n.scalarBits)
                continue;
            if (n.resident)
                require(
                    n.data.empty() && !n.scalarBits && n.resident->shape == n.shape &&
                        n.resident->dtype == n.dtype && n.resident->owner && n.resident->address,
                    "invalid resident input");
            else if (n.dtype == DType::F64)
                require(n.scalarBits.has_value() && n.data.empty(), "invalid scalar input payload");
            else
                require(!n.scalarBits && n.data.size() == count, "invalid input payload");
            continue;
        }
        require(!n.resident, "computed value borrows input storage");
        require(n.data.empty() && !n.scalarBits, "computed value has host payload");
        if (n.op == "loop") {
            kernel::validateLoopRegion(p, n);
            continue;
        }
        if (n.op == "loop_index_f64") {
            require(
                n.dtype == DType::F64 && n.shape.empty() && n.inputs.empty() &&
                    n.attributes.empty(),
                "loop element conversion type");
            continue;
        }
        if (n.op == "scalar_add" || n.op == "scalar_sub" || n.op == "scalar_mul" ||
            n.op == "scalar_div" || n.op == "scalar_neg") {
            // A scalar operation is written at one width and its operands are that width: the
            // element type a whole-tensor reduction answers (`:op/div_f` divides a gradient by a
            // count) is as much a scalar as the f64 one.
            require(
                (n.dtype == DType::F32 || n.dtype == DType::F64) && n.shape.empty() &&
                    n.attributes.empty() && n.inputs.size() == (n.op == "scalar_neg" ? 1 : 2),
                "scalar arithmetic type or arity");
            for (auto in : n.inputs)
                require(
                    p.nodes[in].dtype == n.dtype && p.nodes[in].shape.empty(),
                    "scalar operand type");
            continue;
        }
        if (n.op == "sum_all" || n.op == "mean_all") {
            // The annotation is the elements' type (design principles 7.3) and the kernel casts
            // its f64 accumulator into it once; the device has the two float widths only.
            require(
                (n.dtype == DType::F32 || n.dtype == DType::F64) && n.shape.empty() &&
                    n.inputs.size() == 1 && p.nodes[n.inputs[0]].dtype == DType::F32,
                "scalar reduction type");
            (void)kernel::reductionAccess(p, n, kernel::IndexExpr(0), kernel::IndexExpr(0));
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
        if (n.op == "cast_f64") {
            require(
                n.dtype == DType::F64 && n.shape.empty() && n.inputs.size() == 1 &&
                    p.nodes[n.inputs[0]].dtype == DType::F32,
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
        if (elementwise(n.op) && n.indexing)
            for (size_t input = 0; input < n.inputs.size(); ++input)
                (void)kernel::pointwiseAddress(p, n, input, kernel::IndexExpr(0));
        const auto &a = p.nodes[n.inputs[0]].shape;
        if (n.op == "reindex") {
            if (n.indexing)
                (void)kernel::coreViewAddress(p, n, kernel::IndexExpr(0));
            else {
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
            require(a.size() >= 2 && b.size() >= 2, "matmul rank");
            auto output = broadcast(Shape(a.begin(), a.end() - 2), Shape(b.begin(), b.end() - 2));
            output.push_back(a[a.size() - 2]);
            output.push_back(b.back());
            require(a.back() == b[b.size() - 2] && n.shape == output, "matmul dimensions");
            kernel::validateMatmulContract(p, n);
        }
        if (n.op == "sum_to")
            (void)kernel::reductionAccess(p, n, kernel::IndexExpr(0), kernel::IndexExpr(0));
        if (reduction) {
            (void)kernel::reductionAccess(p, n, kernel::IndexExpr(0), kernel::IndexExpr(0));
        }
    }
}

void schedule(Program &p, ScheduleOptions options, InputBindings bindings) {
    validate(p, bindings);
    const size_t count = p.nodes.size();
    std::vector<size_t> users(count), consumer(count, SIZE_MAX), owner(count), groupSize(count, 1);
    std::iota(owner.begin(), owner.end(), 0);
    for (size_t id = 0; id < count; ++id)
        for (auto in : p.nodes[id].inputs) {
            ++users[in];
            consumer[in] = id;
        }
    for (size_t id = 0; id < count; ++id)
        if (p.nodes[id].op == "result")
            owner[id] = p.nodes[id].inputs[0];
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
                (!elementwise(p.nodes[member].op) || p.nodes[member].shape != p.nodes[id].shape ||
                 !kernel::canonicalPointwise(p, p.nodes[member])))
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
        for (size_t member = 0; member < count; ++member)
            if (owner[member] == id)
                launch.members.push_back(member);
        if (p.nodes[id].results.empty())
            launch.outputs = {id};
        else {
            launch.outputs.assign(p.nodes[id].results.size(), SIZE_MAX);
            for (auto member : launch.members)
                if (p.nodes[member].op == "result") {
                    auto &output = launch.outputs.at(size_t(p.nodes[member].attributes[0]));
                    require(output == SIZE_MAX, "duplicate result projection");
                    output = member;
                }
            require(
                std::find(launch.outputs.begin(), launch.outputs.end(), SIZE_MAX) ==
                    launch.outputs.end(),
                "missing result projection");
        }
        launch.implementation = "standard";
        const auto &n         = p.nodes[id];
        constexpr auto mixed =
            core::sem::kReassociate | core::sem::kChangePrecision | core::sem::kFiniteMath;
        if (n.op == "matmul" && (n.permissions & mixed) == mixed && launch.elements &&
            p.nodes[n.inputs[0]].shape.back() > 0)
            launch.implementation = "cublas_bf16";
        if (launch.members.size() > 1 &&
            std::any_of(launch.members.begin(), launch.members.end(), [&](size_t member) {
                return orderedAxis(p.nodes[member].op);
            })) {
            launch.implementation = "ordered_reduce";
            launch.blockX         = 32;
        }
        if (options.parallelMax && n.op == "max_axis" && parallelMaximum(p, n)) {
            const auto &shape = p.nodes[n.inputs[0]].shape;
            if (size_t(n.attributes[0]) + 1 == shape.size() && shape.back() >= 128)
                launch.implementation = "parallel_max";
        }
        p.launches.push_back(std::move(launch));
    }
    if (options.fuseElementwise && options.parallelMax) {
        // Bound the serial fallback cost. Wide, multi-reduction rows measured slower
        // than separate stages when cancellation defeats the exact-sum proof.
        if (const auto region = kernel::rowKernel(p); region && region->orderedElements <= 1024) {
            p.launches = {Launch{
                .node           = p.outputs[0],
                .members        = region->members(),
                .elements       = elements(p.nodes[p.outputs[0]].shape),
                .name           = "tiles_rows",
                .implementation = "row_region",
                .blockX         = 32,
                .gridX          = static_cast<unsigned>(std::min<size_t>(region->rows, 65535)),
                .outputs        = {p.outputs[0]}}};
        }
    }
    planStorage(p, bindings);
}

void planStorage(Program &p, InputBindings bindings) {
    const size_t count = p.nodes.size();
    const auto life    = lifetimes(p);
    p.storage          = {};
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
    validatePlan(p, bindings);
}

void validatePlan(const Program &p, InputBindings bindings) {
    validate(p, bindings);
    require(p.storage.slots.size() == p.nodes.size(), "storage mapping size");
    std::vector<size_t> stageOf(p.nodes.size(), SIZE_MAX);
    for (size_t i = 0; i < p.nodes.size(); ++i)
        if (p.nodes[i].op == "input")
            stageOf[i] = 0;
    for (size_t stage = 0; stage < p.launches.size(); ++stage) {
        const auto &launch = p.launches[stage];
        require(
            launch.node < p.nodes.size() && !launch.members.empty() &&
                std::find(launch.members.begin(), launch.members.end(), launch.node) !=
                    launch.members.end(),
            "invalid stage root");
        require(launch.elements == elements(p.nodes[launch.node].shape), "launch extent");
        const auto &producer = p.nodes[launch.node];
        const bool multiple  = !producer.results.empty();
        if (multiple) {
            require(
                launch.outputs.size() == producer.results.size() &&
                    launch.members.size() == launch.outputs.size() + 1 &&
                    launch.members.front() == launch.node,
                "multi-result stage arity");
            for (size_t i = 0; i < launch.outputs.size(); ++i) {
                const auto output = launch.outputs[i];
                require(output < p.nodes.size(), "invalid stage output");
                const auto &result = p.nodes[output];
                require(
                    result.op == "result" && result.inputs == std::vector<size_t>{launch.node} &&
                        result.attributes == std::vector<int64_t>{static_cast<int64_t>(i)} &&
                        std::find(launch.members.begin(), launch.members.end(), output) !=
                            launch.members.end(),
                    "stage result projection mismatch");
            }
        } else {
            require(launch.members.back() == launch.node, "invalid stage root");
            require(launch.outputs == std::vector<size_t>{launch.node}, "stage output mismatch");
        }
        require(
            launch.implementation == "standard" || launch.implementation == "parallel_max" ||
                launch.implementation == "ordered_reduce" ||
                launch.implementation == "row_region" || launch.implementation == "cublas_bf16",
            "unknown implementation");
        if (launch.implementation == "cublas_bf16") {
            // The mixed GEMM is the one computation the compiler moves to a lower precision on its
            // own, so its refusal names the permission the node is missing instead of saying "mixed
            // GEMM permissions" -- the author's ruling of 2026-10-23
            // (docs/handover/reviews/questions-2026-10-23.md, "precision-diagnostic-removal"):
            // "`tiles` 拒绝"编译器引入的低精度"时，消息点名缺的是哪个许可（`change_precision` 等），
            // 而不是笼统的 "mixed GEMM permissions"。"
            constexpr auto mixed =
                core::sem::kReassociate | core::sem::kChangePrecision | core::sem::kFiniteMath;
            require(p.nodes[launch.node].op == "matmul", "cublas_bf16 stage is not a matmul");
            const auto permissions = core::sem::PermissionMask(p.nodes[launch.node].permissions);
            const auto missing =
                core::sem::PermissionMask(mixed & ~uint32_t(permissions));
            require(
                missing == core::sem::kNoPermissions,
                "cublas_bf16 needs " + core::sem::permissionsText(mixed) +
                    " on the matmul node and is missing " + core::sem::permissionsText(missing));
        }
        if (launch.implementation == "row_region") {
            const auto region = kernel::rowKernel(p);
            require(
                region && p.launches.size() == 1 && launch.members == region->members() &&
                    launch.blockX == 32 && launch.blockY == 1 && launch.gridY == 1 &&
                    launch.gridX == std::min<size_t>(region->rows, 65535),
                "invalid row region plan");
        }
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
                            p.nodes[member].shape == p.nodes[reduction].shape &&
                            kernel::canonicalPointwise(p, p.nodes[member]),
                        "ordered reduction epilogue domain");
        }
        if (launch.implementation == "parallel_max") {
            const auto &root = p.nodes[launch.node];
            require(
                root.op == "max_axis" && launch.members.size() == 1,
                "parallel maximum operation");
            require(parallelMaximum(p, root), "parallel maximum lacks a compatible reduction law");
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
                multiple || launch.implementation == "row_region" || launch.members.size() == 1 ||
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
                    (multiple && in == launch.node) || stageOf[in] == stage + 1 ||
                        p.storage.slots[in] != SIZE_MAX || p.nodes[in].resident,
                    "register value escapes stage");
                if (stageOf[in] == stage + 1)
                    ++internalUses[in];
            }
            stageOf[id] = stage + 1;
        }
        for (auto id : launch.members)
            require(
                multiple || launch.implementation == "row_region" || id == launch.node ||
                    internalUses[id] == 1,
                "fused value must have one use");
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
        out << "],\"outputs\":[";
        for (size_t j = 0; j < s.outputs.size(); ++j) {
            if (j)
                out << ',';
            out << s.outputs[j];
        }
        out << "]}";
    }
    out << "],\"outputs\":[";
    for (size_t i = 0; i < p.outputs.size(); ++i) {
        if (i)
            out << ',';
        out << p.outputs[i];
    }
    out << "],\"graph_fact_instances\":" << p.graphFactInstances
        << ",\"specialized_instances\":" << p.specializedInstances << ",\"numeric_permissions\":[";
    for (size_t i = 0; i < p.nodes.size(); ++i) {
        if (i)
            out << ',';
        out << static_cast<unsigned>(p.nodes[i].permissions);
    }
    out << "],\"indexed_nodes\":[";
    bool firstIndexed = true;
    for (size_t i = 0; i < p.nodes.size(); ++i) {
        if (!p.nodes[i].indexing)
            continue;
        if (!firstIndexed)
            out << ',';
        firstIndexed = false;
        out << i;
    }
    out << "],\"operator_contracts\":[";
    for (size_t i = 0; i < p.contracts.size(); ++i) {
        if (i)
            out << ',';
        const auto &contract = p.contracts[i];
        out << "{\"identity\":"
            << std::quoted(contract.key ? contract.key->toString() : contract.uri)
            << ",\"declared\":" << (contract.key ? "true" : "false") << ",\"numeric\":"
            << std::quoted(
                   core::sem::numericClassName(
                       contract.declaration ? contract.declaration->numeric.cls
                                            : core::sem::NumericClass::Opaque));
        if (contract.declaration &&
            contract.declaration->numeric.cls == core::sem::NumericClass::Bounded)
            out << ",\"ulp_bound\":" << contract.declaration->numeric.ulpBound;
        out << '}';
    }
    out << "]}";
    return out.str();
}
} // namespace camel::tiles
