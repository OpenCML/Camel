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
 * Created: Oct. 14, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The type invariant of an encoded graph, checked at every commit: every type on a graph is
 * determined (design principles 5.10: "no value has a type that is still undetermined at run
 * time"; docs/design/any-typing/README.cn.md, 4.6).
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

#include <format>
#include <stdexcept>
#include <string>

namespace camel::runtime {

using namespace camel::core::type;

namespace {

/// The first undetermined type in `type`, or nullptr when every part of it is determined.
const Type *firstUndetermined(const Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const TypeCode code = type->code();
    if (isAuxiliary(code)) {
        return type;
    }
    switch (code) {
    case TypeCode::Array:
        return firstUndetermined(static_cast<const ArrayType *>(type)->elemType());
    case TypeCode::Tuple:
        for (Type *member : static_cast<const TupleType *>(type)->types()) {
            if (const Type *found = firstUndetermined(member)) {
                return found;
            }
        }
        return nullptr;
    case TypeCode::Struct:
        for (Type *member : static_cast<const StructType *>(type)->types()) {
            if (const Type *found = firstUndetermined(member)) {
                return found;
            }
        }
        return nullptr;
    case TypeCode::Function: {
        const auto *func = static_cast<const FunctionType *>(type);
        for (Type *param : func->withTypesSpan()) {
            if (const Type *found = firstUndetermined(param)) {
                return found;
            }
        }
        for (Type *param : func->normTypesSpan()) {
            if (const Type *found = firstUndetermined(param)) {
                return found;
            }
        }
        return func->hasExitType() ? firstUndetermined(func->exitType()) : nullptr;
    }
    default:
        break;
    }
    if (type->isOtherType()) {
        // The arguments of a module type (`Map<K, V>`, `Tensor<f32, ...>`); a missing one (an
        // unknown dimension) is null and says nothing about the type of a value.
        for (Type *param : static_cast<const OtherType *>(type)->paramsSpan()) {
            if (const Type *found = firstUndetermined(param)) {
                return found;
            }
        }
    }
    return nullptr;
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
