/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Preserve Camel FILL conventions: normal input is the template, with-inputs are fields.
#include "tiles/internal/graph_values.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include <numeric>
#include <stdexcept>
#include <vector>
namespace camel::tiles {
runtime::gc_node_ref_t
packGraphTuple(runtime::GraphDraft &draft, std::span<const runtime::gc_node_ref_t> values) {
    std::vector<core::type::Type *> types;
    for (auto value : values) {
        const auto *node = draft.header(value);
        if (!node || !node->dataType)
            throw std::runtime_error("Tiles tuple requires typed graph values");
        types.push_back(node->dataType);
    }
    auto *type      = core::type::TupleType::create(types);
    auto *empty     = ::Tuple::create(values.size(), core::mm::autoSpace());
    const auto base = draft.materializeStaticValue(core::rtdata::toSlot(empty), type);
    std::vector<size_t> slots(values.size());
    std::iota(slots.begin(), slots.end(), 0);
    const auto output = draft.addFillNode(
        type,
        runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots),
        draft.allocateRuntimeSlot(type));
    draft.setNormInputs(output, std::span(&base, 1));
    draft.setWithInputs(output, values);
    return output;
}
} // namespace camel::tiles
