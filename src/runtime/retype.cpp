// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Rebuilding a graph under a type substitution (see retype.h).
 *
 * The rebuild rides on the decode/encode pair the runtime rewrites already use: `decode` copies
 * every column of a frozen graph into a draft, `encode` (through the session's commit) writes a
 * draft back as a new version, checking it with the same validators every commit runs. What this
 * file adds is the one thing neither knows: the types. They live in three places at once -- the
 * node records, the slot layouts (a node without a type of its own takes the slot's), and the
 * graph's own signature (function type, closure, and the form ports of its regions) -- and a
 * substitution that changed one of the three would leave the graph disagreeing with itself.
 */

#include "camel/runtime/retype.h"

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

FunctionType *rebuildFunctionType(FunctionType *func, const GraphTypeSubstitution &types) {
    std::vector<Type *> with(func->withTypesSpan().begin(), func->withTypesSpan().end());
    std::vector<Type *> norm(func->normTypesSpan().begin(), func->normTypesSpan().end());
    std::vector<bool> changedWith(with.size(), false);
    std::vector<bool> changedNorm(norm.size(), false);
    for (size_t i = 0; i < with.size(); ++i) {
        Type *mapped = types.apply(with[i]);
        changedWith[i] = mapped != with[i];
        with[i]        = mapped;
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        Type *mapped = types.apply(norm[i]);
        changedNorm[i] = mapped != norm[i];
        norm[i]        = mapped;
    }
    Type *exit       = func->hasExitType() ? func->exitType() : nullptr;
    Type *mappedExit = types.apply(exit);
    bool changed     = mappedExit != exit;
    for (bool one : changedWith) {
        changed = changed || one;
    }
    for (bool one : changedNorm) {
        changed = changed || one;
    }
    if (!changed) {
        return func;
    }

    const bool named = func->hasArgNames();
    FunctionTypeFactory factory;
    factory.setHasCompileInfo(named);
    factory.setResources(func->resources());
    for (size_t i = 0; i < with.size(); ++i) {
        const std::string name = named ? std::string(func->argNameAt(i)) : std::string();
        if (func->hasWithRest() && i + 1 == with.size()) {
            factory.addWithRest(name);
        } else if (func->withIsConstasAt(i)) {
            factory.addWithConstasArg(name, with[i]);
        } else {
            factory.addWithArg(name, with[i], func->withIsVarAt(i), func->withNeedsValueAt(i));
        }
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        const std::string name = named ? std::string(func->argNameAt(with.size() + i))
                                       : std::string();
        if (func->hasRest() && i + 1 == norm.size()) {
            Type *pack = norm[i] != nullptr && norm[i]->code() == TypeCode::TypeVar ? norm[i]
                                                                                   : nullptr;
            factory.addRestArg(name, pack);
        } else if (func->normIsConstasAt(i)) {
            factory.addConstasArg(name, norm[i]);
        } else {
            factory.addNormArg(name, norm[i], func->normIsVarAt(i));
        }
    }
    factory.setExitType(mappedExit != nullptr ? mappedExit : Type::Void());
    return factory.build();
}

/// The type the table names for `type`, one level at a time: a composite whose components are all
/// unchanged is the object it was, so a substitution that names nothing leaves every type as it
/// found it (which is what makes a rebuild of nothing the same graph).
Type *applyTo(Type *type, const GraphTypeSubstitution &types) {
    switch (type->code()) {
    case TypeCode::Tuple: {
        auto *tuple = static_cast<TupleType *>(type);
        std::vector<Type *> members(tuple->types().begin(), tuple->types().end());
        bool changed = false;
        for (Type *&member : members) {
            Type *mapped = types.apply(member);
            changed      = changed || mapped != member;
            member       = mapped;
        }
        if (!changed) {
            return type;
        }
        if (tuple->isCell()) {
            return TupleType::cell(members.front());
        }
        if (tuple->isNullable()) {
            return TupleType::nullable(members.front());
        }
        if (tuple->isFallible()) {
            return TupleType::fallible(members.front());
        }
        return TupleType::create(members);
    }
    case TypeCode::Array: {
        auto *array  = static_cast<ArrayType *>(type);
        Type *mapped = types.apply(array->elemType());
        if (mapped == array->elemType()) {
            return type;
        }
        if (array->openRank()) {
            return ArrayType::openRank(mapped, array->extents());
        }
        return ArrayType::create(mapped, array->extents(), array->packName());
    }
    case TypeCode::Stream: {
        auto *stream = static_cast<StreamType *>(type);
        Type *mapped = types.apply(stream->elemType());
        return mapped == stream->elemType() ? type : StreamType::create(mapped);
    }
    case TypeCode::Function:
        return rebuildFunctionType(static_cast<FunctionType *>(type), types);
    default:
        // A primitive has no components; a named type (a declared struct, an enum instance, a
        // module type) has its identity in its declaration, so its components are not reachable
        // from here and the table has to name the type itself (retype.h).
        return type;
    }
}

size_t inputCountOf(const GraphTransaction &transaction, gc_node_ref_t ref) {
    const auto &draft = transaction.graph();
    return draft.dataPortsOf(ref).size() + draft.ctrlInputsOf(ref).size() +
           draft.orderInputsOf(ref).size() + draft.syncInputsOf(ref).size();
}

} // namespace

GraphTypeSubstitution::GraphTypeSubstitution(std::initializer_list<Pair> pairs) : pairs_(pairs) {}

GraphTypeSubstitution::GraphTypeSubstitution(std::span<const Pair> pairs)
    : pairs_(pairs.begin(), pairs.end()) {}

void GraphTypeSubstitution::replace(TypeId from, TypeId to) {
    for (Pair &pair : pairs_) {
        if (pair.first == from) {
            pair.second = to;
            return;
        }
    }
    pairs_.emplace_back(from, to);
}

void GraphTypeSubstitution::replace(Type *from, Type *to) {
    ASSERT(from != nullptr && to != nullptr, "A type substitution needs two types.");
    replace(from->typeId(), to->typeId());
}

Type *GraphTypeSubstitution::apply(Type *type) const {
    if (type == nullptr || pairs_.empty()) {
        return type;
    }
    for (const Pair &pair : pairs_) {
        if (pair.first == type->typeId()) {
            Type *target = TypeTable::instance().get(pair.second);
            ASSERT(target != nullptr, "A type substitution names a type the table does not hold.");
            return target;
        }
    }
    return applyTo(type, *this);
}

GraphRetypeReport retypeDraft(
    GraphTransaction &transaction, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options) {
    auto &draft = transaction.edit();
    GraphRetypeReport report;
    if (types.empty() && !options.nodeType) {
        return report;
    }

    // 1. The nodes. A node's own type is what the record stores; a node without one takes the
    // type of the slot it reads, which the layout below carries.
    for (gc_node_ref_t ref = 0; ref < draft.nodeSlotCount(); ++ref) {
        if (!draft.alive(ref)) {
            continue;
        }
        const DraftNodeHeader *header = draft.header(ref);
        ASSERT(header != nullptr, "A live draft node has no header.");
        ++report.nodes;
        report.edges += inputCountOf(transaction, ref);
        Type *oldType = header->dataType;
        if (oldType == nullptr) {
            continue;
        }
        Type *mapped = types.apply(oldType);
        if (options.nodeType) {
            if (Type *own =
                    options.nodeType(transaction, ref, header->kind, oldType, mapped)) {
                mapped = own;
            }
        }
        if (mapped != oldType) {
            draft.setNodeDataType(ref, mapped);
            ++report.retypedNodes;
        }
    }

    // 2. The frame layout: every slot the graph has, including the reserved slot 0.
    if (draft.hasFrameLayout() && !draft.runtimeSlotTypes().empty()) {
        std::vector<Type *> slots(
            draft.runtimeSlotTypes().begin(), draft.runtimeSlotTypes().end());
        bool changed = false;
        for (Type *&slot : slots) {
            Type *mapped = types.apply(slot);
            if (mapped != slot) {
                slot     = mapped;
                changed  = true;
                ++report.retypedSlots;
            }
        }
        if (changed) {
            draft.setRuntimeSlotTypes(slots);
        }
    }

    // 3. The static area's layout. Its values are bit patterns for a type that is not an object,
    // so the layout can follow them; a value that *is* an object keeps its own type, and changing
    // the slot's would hand the collector and the encoder a value of a type it does not have.
    if (!draft.staticSlotTypes().empty()) {
        std::vector<Type *> slots(draft.staticSlotTypes().begin(), draft.staticSlotTypes().end());
        bool changed = false;
        for (size_t i = 0; i < slots.size(); ++i) {
            Type *mapped = types.apply(slots[i]);
            if (mapped == slots[i]) {
                continue;
            }
            if (slots[i]->isGCTraced()) {
                if (options.leaveStaticObjects) {
                    // The value keeps its class, so its declared type stays with it; the node that
                    // reads it is retyped all the same, and the rewrite that replaces that node is
                    // what reconciles the two.
                    ++report.leftStaticSlots;
                    continue;
                }
                const std::string graph = draft.baseGraph() != nullptr ? draft.baseGraph()->name()
                                                                      : std::string("<draft>");
                throw std::runtime_error(
                    std::format(
                        "Graph '{}' holds a static value of type '{}', which the rebuild would "
                        "have to retype: the value keeps its own object type. A caller that is "
                        "about to rewrite the node reading it may set "
                        "GraphRetypeOptions::leaveStaticObjects.",
                        graph,
                        slots[i]->toString()));
            }
            slots[i] = mapped;
            changed  = true;
            ++report.retypedStaticSlots;
        }
        if (changed) {
            draft.setStaticSlotTypes(slots);
        }
    }

    // 4. The graph's own types: the signature its values have, the closure it was captured with,
    // and the form ports of its regions (the arguments of a loop part).
    if (FunctionType *func = draft.funcType()) {
        // A graph is a function: its signature stays a function type. A substitution that names it
        // retypes what the signature mentions (one of its parameters, say), not the signature
        // itself, which is what a caller whose type is the substituted one needs.
        Type *mapped = options.leaveSignatures ? rebuildFunctionType(func, types)
                                               : types.apply(func);
        if (mapped != func) {
            draft.setFuncType(static_cast<FunctionType *>(mapped));
            report.signature = true;
        }
    }
    if (TupleType *closure = draft.closureType()) {
        Type *mapped = types.apply(closure);
        if (mapped != closure) {
            draft.setClosureType(static_cast<TupleType *>(mapped));
            report.closure = true;
        }
    }
    for (uint32_t region = 0; region < draft.regionCount(); ++region) {
        const FunctionType *params = draft.region(region).params;
        if (params == nullptr) {
            continue;
        }
        Type *mapped = types.apply(const_cast<FunctionType *>(params));
        if (mapped != params) {
            draft.setRegionParams(region, static_cast<const FunctionType *>(mapped));
            ++report.retypedRegions;
        }
    }
    return report;
}

GCGraph *retypeGraph(
    RuntimeGraphDraftSession &session, GCGraph *source, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options, GraphRetypeReport *report) {
    ASSERT(source != nullptr, "A graph rebuild needs a graph.");
    const GraphRetypeReport one = retypeDraft(session.edit(source), types, options);
    if (report != nullptr) {
        *report = one;
    }
    const Program *next = session.commit();
    return next->graph(source->id());
}

const Program *retypeProgram(
    RuntimeGraphDraftSession &session, const GraphTypeSubstitution &types,
    const GraphRetypeOptions &options, ProgramRetypeReport *report) {
    ProgramRetypeReport mine;
    const std::vector<GCGraph *> graphs = session.collectReachableRuntimeGraphs();
    mine.graphs                         = graphs.size();
    for (GCGraph *graph : graphs) {
        const GraphRetypeReport one = retypeDraft(session.edit(graph), types, options);
        mine.retypedNodes += one.retypedNodes;
        mine.retypedStatic += one.retypedStaticSlots;
        mine.leftStatic += one.leftStaticSlots;
        if (one.changed()) {
            ++mine.rebuilt;
        }
    }
    mine.program = session.commit();
    if (report != nullptr) {
        *report = mine;
    }
    return mine.program;
}

} // namespace camel::runtime