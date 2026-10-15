/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * IndexBuilder and indexingFrom (see indexing.h): the vocabulary the instantiation functions of
 * the tensor operators (indexing.cpp) and of other modules (nn's pooling) build instances with.
 */

#include "indexing.h"

#include "support.h"

#include <algorithm>
#include <format>

namespace camel::tensor::ops {

using namespace camel::core::sem;
using Rel = Constraint::Rel;

// ---- IndexBuilder ---------------------------------------------------------------------------

ExprId IndexBuilder::extent(size_t operand, size_t axis, int64_t known) {
    auto &params = instance.domain.params;
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].source == ParamSource::Extent && params[i].operand == operand &&
            params[i].axis == axis) {
            return x().param(static_cast<uint32_t>(i));
        }
    }
    params.push_back(Param{
        .name    = std::format("s{}_{}", operand, axis),
        .source  = ParamSource::Extent,
        .operand = static_cast<uint8_t>(operand),
        .axis    = static_cast<uint8_t>(axis),
        .value   = known >= 0 ? std::optional(known) : std::nullopt,
    });
    return x().param(static_cast<uint32_t>(params.size() - 1));
}

ExprId IndexBuilder::constant(
    std::string name, size_t operand, int64_t value, std::optional<size_t> element) {
    auto &params = instance.domain.params;
    params.push_back(Param{
        .name    = std::move(name),
        .source  = ParamSource::Static,
        .operand = static_cast<uint8_t>(operand),
        .axis    = element ? static_cast<uint8_t>(*element) : uint8_t{0xFF},
        .value   = value,
    });
    return x().param(static_cast<uint32_t>(params.size() - 1));
}

ExprId IndexBuilder::derived(std::string name, std::optional<int64_t> value, ExprId definition) {
    auto &params = instance.domain.params;
    params.push_back(Param{
        .name       = std::move(name),
        .source     = ParamSource::Derived,
        .value      = value,
        .definition = definition,
    });
    return x().param(static_cast<uint32_t>(params.size() - 1));
}

ExprId IndexBuilder::dim(ExprId extent, DimRole role) {
    auto &dims = instance.domain.dims;
    dims.push_back({std::format("d{}", dims.size()), extent, role});
    return x().dim(static_cast<uint32_t>(dims.size() - 1));
}

void IndexBuilder::require(ExprId lhs, Rel rel, ExprId rhs) {
    instance.domain.constraints.push_back({lhs, rel, rhs});
}

Access IndexBuilder::affine(std::vector<ExprId> coords) {
    return Access{.kind = AccessKind::Affine, .coords = std::move(coords)};
}

Access IndexBuilder::none() { return Access{.kind = AccessKind::None}; }

std::optional<std::vector<std::vector<ExprId>>>
IndexBuilder::broadcast(std::span<const StaticShape> shapes, std::span<const size_t> operands) {
    size_t rank = 0;
    for (const StaticShape &s : shapes) {
        rank = std::max(rank, s.size());
    }
    std::vector<std::vector<ExprId>> coords(shapes.size());
    for (size_t i = 0; i < shapes.size(); ++i) {
        coords[i].resize(shapes[i].size(), kNoExpr);
    }
    for (size_t j = 0; j < rank; ++j) {
        // The operands that have this axis, and among them those whose extent may not be 1.
        std::optional<std::pair<size_t, size_t>> source; // (operand, its axis)
        int64_t known      = -1;
        size_t notOne      = 0;
        bool anyUnknown    = false;
        for (size_t i = 0; i < shapes.size(); ++i) {
            const size_t shift = rank - shapes[i].size();
            if (j < shift) {
                continue;
            }
            const int64_t e = shapes[i][j - shift];
            if (e == 1) {
                continue;
            }
            ++notOne;
            anyUnknown |= e == kUnknownDim;
            if (e != kUnknownDim) {
                if (known >= 0 && known != e) {
                    return std::nullopt; // conflicting extents: the call is rejected
                }
                known = e;
            }
            if (!source || (shapes[source->first][source->second] == kUnknownDim && e != kUnknownDim)) {
                source = {i, j - shift};
            }
        }
        // Two operands that may differ, one of them possibly 1: whether this axis broadcasts
        // is decided only by the values (design Q9: no instance until the shape is known).
        if (notOne >= 2 && anyUnknown) {
            return std::nullopt;
        }
        const ExprId extentExpr =
            source ? extent(operands[source->first], source->second, shapes[source->first][source->second])
                   : x().constant(1);
        const ExprId d = dim(extentExpr);
        for (size_t i = 0; i < shapes.size(); ++i) {
            const size_t shift = rank - shapes[i].size();
            if (j >= shift) {
                coords[i][j - shift] = shapes[i][j - shift] == 1 ? x().constant(0) : d;
            }
        }
    }
    return coords;
}

IndexedInstance IndexBuilder::finish(std::vector<ExprId> coords, WriteMode mode, uint8_t law) {
    instance.output = Write{.coords = std::move(coords), .mode = mode};
    instance.law    = law;
    return std::move(instance);
}

InstantiateFn indexingFrom(IndexFn fn) {
    return [fn = std::move(fn)](const InstanceContext &cx) -> std::optional<IndexedInstance> {
        if (!cx.with.empty()) {
            return std::nullopt;
        }
        const StaticFacts facts(cx.norm, cx.statics);
        return fn(InferContext(cx.norm, facts.constants, facts.partials));
    };
}

} // namespace camel::tensor::ops
