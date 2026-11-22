// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Generic row-local tensor DAGs; shared values are computed once and reused across reductions.

#include "tiles/internal/row_kernel.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camel::tiles::kernel {
namespace sem = core::sem;
std::vector<size_t> RowKernel::members() const {
    std::vector<size_t> result;
    for (const auto &stage : stages)
        result.push_back(stage.node);
    return result;
}
std::optional<RowKernel> rowKernel(const Program &p) {
    if (p.outputs.size() != 1 || p.outputs[0] >= p.nodes.size())
        return std::nullopt;
    const auto root   = p.outputs[0];
    const auto &shape = p.nodes[root].shape;
    if (shape.size() != 2 || shape[0] < 1 || shape[1] < 2 || shape[1] > 2048)
        return std::nullopt;
    RowKernel plan{size_t(shape[0]), size_t(shape[1]), 0};
    plan.offsets.assign(p.nodes.size(), SIZE_MAX);
    plan.widths.assign(p.nodes.size(), 0);
    size_t reductions = 0;
    std::vector<size_t> last(p.nodes.size());
    for (size_t id = 0; id < p.nodes.size(); ++id) {
        const auto &n = p.nodes[id];
        for (auto in : n.inputs)
            last[in] = id;
        if (n.shape.empty() || n.shape == Shape{1} || n.shape == Shape{1, 1} ||
            n.shape == Shape{shape[0], 1})
            plan.widths[id] = 1;
        else if (n.shape == shape || n.shape == Shape{shape[1]} || n.shape == Shape{1, shape[1]})
            plan.widths[id] = plan.width;
        else
            return std::nullopt;
        if (n.op == "input")
            continue;
        if (id > root || n.dtype != DType::F32)
            return std::nullopt;
        RowStage stage{.node = id};
        if (n.op == "sum_axis" || n.op == "mean_axis" || n.op == "max_axis") {
            if (n.inputs.size() != 1 || n.attributes != std::vector<int64_t>{1} ||
                p.nodes[n.inputs[0]].shape != shape || n.shape != Shape{shape[0], 1} ||
                !n.indexing || n.semantic >= p.contracts.size() || !canonicalAxisReduction(p, n))
                return std::nullopt;
            const auto *decl = p.contracts[n.semantic].declaration;
            if (!decl || n.indexing->law >= decl->laws.size())
                return std::nullopt;
            const auto &law = decl->laws[n.indexing->law];
            if (law.input != core::type::TypeCode::Float32 ||
                law.result != core::type::TypeCode::Float32 || law.init != sem::LawInit::Constant)
                return std::nullopt;
            if (n.op == "max_axis") {
                const auto freedom = sem::reorderFreedom(law, n.permissions);
                if (law.kind != sem::LawKind::Max ||
                    law.accumulator != core::type::TypeCode::Float32 ||
                    law.initBits != 0xff800000U || law.finalize != sem::LawFinalize::None ||
                    !freedom.regroup || !freedom.permute || !freedom.blockIdentity)
                    return std::nullopt;
            } else if (
                law.kind != sem::LawKind::Add || law.accumulator != core::type::TypeCode::Float64 ||
                law.initBits != 0 ||
                law.finalize != (n.op == "mean_axis" ? sem::LawFinalize::DivideByCount
                                                     : sem::LawFinalize::None))
                return std::nullopt;
            if (law.kind == sem::LawKind::Add)
                plan.orderedElements += plan.width;
            stage.reduction = law;
            ++reductions;
        } else {
            if (!elementwise(n.op) && n.op != "cast_f32" && n.op != "cast_f64")
                return std::nullopt;
            if (elementwise(n.op) && !canonicalPointwise(p, n))
                return std::nullopt;
            stage.expression = elementwiseExpression(n.op);
        }
        plan.stages.push_back(std::move(stage));
    }
    if (!reductions || plan.stages.size() < 3 || plan.stages.size() > 64 ||
        plan.stages.back().node != root)
        return std::nullopt;
    // Intervals are inclusive: an output must never overwrite a value read by its stage.
    struct Slot {
        size_t offset, width, until;
    };
    std::vector<Slot> slots;
    last[root] = p.nodes.size();
    for (const auto &stage : plan.stages) {
        const auto id = stage.node, width = plan.widths[id];
        auto reuse = slots.end();
        for (auto it = slots.begin(); it != slots.end(); ++it)
            if (it->until < id && it->width >= width &&
                (reuse == slots.end() || it->width < reuse->width))
                reuse = it;
        if (reuse == slots.end()) {
            plan.offsets[id] = plan.sharedElements;
            slots.push_back({plan.sharedElements, width, last[id]});
            plan.sharedElements += width;
        } else {
            plan.offsets[id] = reuse->offset;
            reuse->until     = last[id];
        }
    }
    // Portable static shared-memory budget, including the maximum reduction scratch.
    if (plan.sharedElements * sizeof(float) +
            32 * (sizeof(float) + sizeof(double) + 2 * sizeof(int)) >
        48 * 1024)
        return std::nullopt;
    return plan;
}
} // namespace camel::tiles::kernel
