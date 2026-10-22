// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The type invariant of an encoded graph, checked at every commit: every type on a graph is
 * determined (design principles 5.10: "no value has a type that is still undetermined at run
 * time"; design principles 5.10).
 *
 * The types that describe a graph are its function type (ports and result) and the layouts of
 * its three slot areas: the runtime slots (one per node that produces a value), the static
 * slots (constants) and the closure. Every node's value lives in one of those slots, so
 * checking the layouts checks every node. None of them may hold, at any depth, a type of the
 * compiler's own (TypeFlag::Auxiliary: a type variable, a type function, a union): a back end
 * reads a slot with its type, and the collector traces a slot by its type. A violation is a
 * defect of the compiler or of a pass, reported at the commit that produced the graph.
 */

#include "runtime/graph_build.h"

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

/// The first undetermined type in `type`, or nullptr when every part of it is determined.
/// `path` holds the composite types currently being walked: a named type may mention itself
/// through an array (`type Node = { children: Node[] }`), which is a cycle between type objects,
/// not an infinite type. Seeing one again on the same path means its fields have already been
/// walked, so there is nothing new to find there.
const Type *firstUndetermined(const Type *type, std::vector<const Type *> &path) {
    if (type == nullptr) {
        return nullptr;
    }
    const TypeCode code = type->code();
    if (isAuxiliary(code)) {
        return type;
    }
    if (std::find(path.begin(), path.end(), type) != path.end()) {
        return nullptr;
    }
    path.push_back(type);
    const Type *found = nullptr;
    switch (code) {
    case TypeCode::Array:
        found = firstUndetermined(static_cast<const ArrayType *>(type)->elemType(), path);
        break;
    case TypeCode::Tuple:
        for (Type *member : static_cast<const TupleType *>(type)->types()) {
            if ((found = firstUndetermined(member, path))) {
                break;
            }
        }
        break;
    case TypeCode::Struct:
        for (Type *member : static_cast<const StructType *>(type)->types()) {
            if ((found = firstUndetermined(member, path))) {
                break;
            }
        }
        break;
    case TypeCode::Function: {
        const auto *func = static_cast<const FunctionType *>(type);
        for (Type *param : func->withTypesSpan()) {
            if ((found = firstUndetermined(param, path))) {
                break;
            }
        }
        if (!found) {
            for (Type *param : func->normTypesSpan()) {
                if ((found = firstUndetermined(param, path))) {
                    break;
                }
            }
        }
        if (!found && func->hasExitType()) {
            found = firstUndetermined(func->exitType(), path);
        }
        break;
    }
    default:
        break;
    }
    if (!found && type->isOtherType()) {
        // The arguments of a module type (`Map<K, V>`, `Tensor<f32, ...>`); a missing one (an
        // unknown dimension) is null and says nothing about the type of a value.
        for (Type *param : static_cast<const OtherType *>(type)->paramsSpan()) {
            if ((found = firstUndetermined(param, path))) {
                break;
            }
        }
    }
    path.pop_back();
    return found;
}

const Type *firstUndetermined(const Type *type) {
    std::vector<const Type *> path;
    return firstUndetermined(type, path);
}

void check(const GCGraph *graph, const Type *type, const char *where) {
    if (const Type *found = firstUndetermined(type)) {
        throw std::runtime_error(
            std::format(
                "Runtime graph '{}' has an undetermined type: '{}' in its {} ('{}'). Every type "
                "on a committed graph is determined (design principles 5.1).",
                graph->name(),
                found->toString(),
                where,
                type->toString()));
    }
}

} // namespace

void validateRuntimeGraphTypes(const GCGraph *graph) {
    if (graph == nullptr) {
        return;
    }
    check(graph, graph->funcType(), "function type");
    check(graph, graph->runtimeDataType(), "runtime slots");
    check(graph, graph->staticDataType(), "static slots");
    check(graph, graph->closureType(), "closure");
}

} // namespace camel::runtime
