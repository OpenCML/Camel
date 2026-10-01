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
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Type re-inference on a graph draft (see draft_types.h).
 */

#include "camel/runtime/draft_types.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"

#include <functional>
#include <optional>
#include <vector>

namespace camel::runtime {

namespace type = camel::core::type;

namespace {

/// Alive nodes in dependency order (every node after its inputs).
std::vector<gc_node_ref_t> dependencyOrder(const GraphDraft &draft) {
    std::vector<uint8_t> state(draft.nodeSlotCount(), 0);
    std::vector<gc_node_ref_t> order;
    order.reserve(draft.nodeCount());
    std::function<void(gc_node_ref_t)> visit = [&](gc_node_ref_t id) {
        if (id >= state.size() || state[id] != 0 || !draft.alive(id)) {
            return;
        }
        state[id] = 1;
        for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
            for (gc_node_ref_t in : inputs) {
                visit(in);
            }
        }
        state[id] = 2;
        order.push_back(id);
    };
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        visit(id);
    }
    return order;
}

type::Type *typeOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    return h ? h->dataType : nullptr;
}

/// The value of a node that is a constant, for resolvers that take constant arguments.
std::optional<slot_t> staticSlotOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Data || h->dataIndex >= 0) {
        return std::nullopt;
    }
    const auto index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    return draft.staticSlots()[index];
}

std::optional<type::Type *> operResultType(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data());
    const auto resolver = camel::core::OperatorResolverRegistry::instance().find(body->uri());
    if (!resolver) {
        return std::nullopt;
    }
    type::type_vec_t with, norm;
    std::vector<std::optional<slot_t>> statics;
    for (gc_node_ref_t in : draft.withInputsOf(id)) {
        with.push_back(typeOf(draft, in));
    }
    for (gc_node_ref_t in : draft.normInputsOf(id)) {
        norm.push_back(typeOf(draft, in));
        statics.push_back(staticSlotOf(draft, in));
    }
    for (type::Type *t : with) {
        if (!t) {
            return std::nullopt;
        }
    }
    for (type::Type *t : norm) {
        if (!t) {
            return std::nullopt;
        }
    }
    try {
        const auto resolved = resolver->resolveWith(with, norm, statics, Modifier::None);
        if (!resolved || !*resolved) {
            return std::nullopt;
        }
        return (*resolved)->exitType();
    } catch (const std::exception &) {
        return std::nullopt; // the node keeps the type compilation gave it
    }
}

std::optional<type::Type *> accsResultType(const GraphDraft &draft, gc_node_ref_t id) {
    const auto inputs = draft.normInputsOf(id);
    if (inputs.empty()) {
        return std::nullopt;
    }
    type::Type *source = typeOf(draft, inputs.front());
    const auto *accs   = reinterpret_cast<const GCAccsBody *>(draft.payloadOf(id).data());
    if (!source) {
        return std::nullopt;
    }
    if (accs->accsKind == GCAccsKind::TupleIndex && source->code() == type::TypeCode::Tuple) {
        auto *tuple = static_cast<type::TupleType *>(source);
        if (accs->value < tuple->size()) {
            return tuple->typeAt(accs->value);
        }
    }
    if (accs->accsKind == GCAccsKind::StructKey && source->code() == type::TypeCode::Struct) {
        auto *strct = static_cast<type::StructType *>(source);
        if (const auto field = strct->findField(accs->key())) {
            return strct->typeAt(*field);
        }
    }
    return std::nullopt;
}

std::optional<type::Type *> nodeResultType(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    switch (h->kind) {
    case GCNodeKind::Data: {
        const auto value = staticSlotOf(draft, id);
        if (!value || !h->dataType) {
            return std::nullopt;
        }
        return camel::core::ValueTypeRefinerRegistry::instance().refine(*value, h->dataType);
    }
    case GCNodeKind::Oper:
        return operResultType(draft, id);
    case GCNodeKind::Gate: {
        const auto inputs = draft.normInputsOf(id);
        return inputs.empty() ? std::nullopt : std::optional(typeOf(draft, inputs.back()));
    }
    case GCNodeKind::Copy: {
        const auto inputs = draft.normInputsOf(id);
        return inputs.empty() ? std::nullopt : std::optional(typeOf(draft, inputs.front()));
    }
    case GCNodeKind::Accs:
        return accsResultType(draft, id);
    case GCNodeKind::Join: {
        const auto arms = draft.withInputsOf(id);
        if (arms.empty()) {
            return std::nullopt;
        }
        type::Type *first = typeOf(draft, arms.front());
        for (gc_node_ref_t arm : arms) {
            if (typeOf(draft, arm) != first) {
                return std::nullopt;
            }
        }
        return first;
    }
    case GCNodeKind::Func: {
        const auto *body = reinterpret_cast<const GCFuncBody *>(draft.payloadOf(id).data());
        const auto *callee = body->calleeGraph ? body->calleeGraph->funcType() : nullptr;
        if (!callee || !callee->hasExitType()) {
            return std::nullopt;
        }
        return callee->exitType();
    }
    default:
        return std::nullopt;
    }
}

} // namespace

size_t reinferDraftTypes(GraphDraft &draft) {
    size_t changed = 0;
    for (gc_node_ref_t id : dependencyOrder(draft)) {
        const auto *h = draft.header(id);
        if (!h || h->dataIndex == 0) {
            continue; // no value (control-only nodes)
        }
        const auto result = nodeResultType(draft, id);
        // Only ever sharpen a type: a resolver may know less than compilation did.
        if (result && *result && *result != h->dataType &&
            (!h->dataType || h->dataType->assignableFrom(*result))) {
            draft.setNodeDataType(id, *result);
            ++changed;
        }
    }

    // The function type follows the ports and the result.
    type::FunctionType *funcType = draft.funcType();
    if (!funcType) {
        return changed;
    }
    type::param_vec_t withTypes, normTypes;
    bool differs = false;
    const auto portType = [&](gc_node_ref_t port, type::Type *declared) {
        type::Type *t = typeOf(draft, port);
        differs |= t && t != declared;
        return t ? t : declared;
    };
    if (draft.withPorts().size() != funcType->withTypesCount() ||
        draft.normPorts().size() != funcType->normTypesCount()) {
        return changed;
    }
    for (size_t i = 0; i < draft.withPorts().size(); ++i) {
        withTypes.emplace_back(
            portType(draft.withPorts()[i], funcType->withTypeAt(i)),
            funcType->withIsVarAt(i));
    }
    for (size_t i = 0; i < draft.normPorts().size(); ++i) {
        normTypes.emplace_back(
            portType(draft.normPorts()[i], funcType->normTypeAt(i)),
            funcType->normIsVarAt(i));
    }
    type::Type *exitType = funcType->hasExitType() ? funcType->exitType() : nullptr;
    const gc_node_ref_t result =
        draft.returnNode() != kInvalidNodeRef ? draft.returnNode() : draft.outputNode();
    if (result != kInvalidNodeRef) {
        const gc_node_ref_t value = draft.resolveForwardedValueRef(result);
        if (type::Type *t = typeOf(draft, value != kInvalidNodeRef ? value : result);
            t && t != exitType && exitType) {
            exitType = t;
            differs  = true;
        }
    }
    if (differs) {
        draft.setFuncType(
            type::FunctionType::create(withTypes, normTypes, exitType, funcType->modifiers()));
    }
    return changed;
}

} // namespace camel::runtime
