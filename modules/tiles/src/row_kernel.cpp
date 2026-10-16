/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Generic row-local tensor DAGs; shared values are computed once and reused across reductions.
#include "tiles/internal/row_kernel.h"
#include <algorithm>
#include <sstream>
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
                !n.indexing || n.semantic >= p.contracts.size())
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
            if (!elementwise(n.op) && n.op != "cast_f32")
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
std::string emitRowKernel(const Program &p, const RowKernel &plan, const std::string &name) {
    std::ostringstream code;
    code << "extern \"C\" __global__ void " << name << "(float** v){\n"
         << "__shared__ float values[" << plan.sharedElements << "], scratch[32];\n"
         << "__shared__ double sums[32]; __shared__ int lo[32],hi[32];\n"
         << "unsigned t=threadIdx.x;\nfor(unsigned long long row=blockIdx.x;row<" << plan.rows
         << "ULL;row+=gridDim.x){\n";
    auto read = [&](size_t id, const std::string &col) {
        if (plan.offsets[id] != SIZE_MAX)
            return "values[" + std::to_string(plan.offsets[id]) + "+" +
                   (plan.widths[id] == 1 ? "0" : col) + "]";
        const auto &n     = p.nodes[id];
        const bool perRow = n.shape.size() == 2 && n.shape[0] == static_cast<int64_t>(plan.rows);
        const auto at     = (perRow ? "row*" + std::to_string(plan.widths[id]) + "ULL+" : "") +
                            (plan.widths[id] == 1 ? "0" : col);
        return n.dtype == DType::F64
                   ? "reinterpret_cast<double*>(v[" + std::to_string(id) + "])[" + at + "]"
                   : "v[" + std::to_string(id) + "][" + at + "]";
    };
    for (const auto &stage : plan.stages) {
        const auto &n          = p.nodes[stage.node];
        const auto destination = "values[" + std::to_string(plan.offsets[stage.node]) + "+";
        if (stage.reduction) {
            const auto &law = *stage.reduction;
            if (law.kind == sem::LawKind::Max) {
                code << "{float best=__uint_as_float(0xff800000U);\nfor(unsigned col=t;col<"
                     << plan.width << ";col+=32) best=tiles_maximum(best,"
                     << read(n.inputs[0], "col") << ");\n"
                     << "scratch[t]=best;__syncthreads();\nfor(unsigned s=16;s;s/=2){if(t<s) "
                        "scratch[t]=tiles_maximum(scratch[t],scratch[t+s]);__syncthreads();}\n"
                     << "if(t==0)" << destination << "0]=scratch[0];}\n";
            } else {
                size_t countBits = 0;
                while ((size_t(1) << countBits) < plan.width)
                    ++countBits;
                // Every finite f32 is an integer multiple of 2^lo. If the
                // largest possible partial sum needs at most 53 lattice bits,
                // every f64 addition is exact, in any grouping. This proves
                // equivalence without a reassociation grant. Nonfinite or wide
                // exponent ranges use the canonical ascending chain instead.
                code << "{double partial=0.0; int lower=4096,upper=-4096;\n"
                     << "for(unsigned col=t;col<" << plan.width
                     << ";col+=32){float x=" << read(n.inputs[0], "col")
                     << "; unsigned bits=__float_as_uint(x)&0x7fffffffU; unsigned "
                        "exponent=bits>>23;\n"
                     << "if(exponent==255) upper=4096; else if(bits){int "
                        "l=exponent?(int)exponent-150:-149; int h=exponent?(int)exponent-126:-126; "
                        "lower=lower<l?lower:l;upper=upper>h?upper:h;}\n"
                     << "partial=__dadd_rn(partial,(double)x); }\n"
                     << "sums[t]=partial;lo[t]=lower;hi[t]=upper;__syncthreads();\n"
                     << "for(unsigned "
                        "s=16;s;s/"
                        "=2){if(t<s){sums[t]=__dadd_rn(sums[t],sums[t+s]);lo[t]=lo[t]<lo[t+s]?lo[t]"
                        ":lo[t+s];hi[t]=hi[t]>hi[t+s]?hi[t]:hi[t+s];}__syncthreads();}\n"
                     << "if(t==0){double acc=0.0; if(hi[0]==-4096 || (hi[0]<1024 && hi[0]-lo[0]+"
                     << countBits << "<=53)) acc=sums[0]; else {\nfor(unsigned col=0;col<"
                     << plan.width << ";++col) acc=__dadd_rn(acc,(double)"
                     << read(n.inputs[0], "col") << ");}\n";
                if (law.finalize == sem::LawFinalize::DivideByCount)
                    code << "acc=__ddiv_rn(acc," << plan.width << ".0);\n";
                code << destination << "0]=__double2float_rn(acc); }}\n";
            }
        } else {
            code << "for(unsigned col=t;col<" << plan.widths[stage.node] << ";col+=32){\n"
                 << (n.op == "cast_f32" ? "double" : "float") << " a=" << read(n.inputs[0], "col")
                 << ";\n";
            if (n.inputs.size() == 2)
                code << "float b=" << read(n.inputs[1], "col") << ";\n";
            code << destination << "col]=" << stage.expression->cuda() << ";}\n";
        }
        code << "__syncthreads();\n";
    }
    const auto root = p.outputs[0];
    code << "for(unsigned col=t;col<" << plan.width << ";col+=32) v[" << root << "][row*"
         << plan.width << "ULL+col]=" << read(root, "col") << ";\n__syncthreads();\n}\n}\n";
    return code.str();
}
} // namespace camel::tiles::kernel
