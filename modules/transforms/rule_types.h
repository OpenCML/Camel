// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph_txn.h"
#include "camel/utils/type.h"

namespace camel::transforms::rules {

/// The type a rule is generic in: the type the primitive works on. A primitive over scalars is its
/// own type (`+` on two `f32`), and one over arrays works on the element of the array -- `matmul`
/// multiplies elements, `sum_axis` adds them -- so the rule is filed once per element type and says
/// which axis the batch is on, never what shape a row has. Asking for the array type instead would
/// file one instance per shape and would name the rows in type of the rule's `T`.
///
/// This is the one place the question is answered: `vmap` instantiates its rules through it, and so
/// does `jvp` -- a rule instantiated at a type of its own would be a second answer to the same
/// question, which is the shape that let `index_into<T>` be filed at `float` while its operand was
/// a `float[]` (the review's P1-9).
inline camel::core::type::Type *ruleElementTypeOf(camel::core::type::Type *type) {
    while (type != nullptr && type->code() == camel::core::type::TypeCode::Array) {
        type = ::TypeTraits::as_ptr<camel::core::type::ArrayType>(type)->elemType();
    }
    return type;
}

/// The type of the first *value* operand of the node at `ref`, or nullptr when it has none.
///
/// A rule is generic in the element its values are made of, and that is not always the first port: a
/// condition is not a value arm. `where(cond, lhs, rhs)` takes a `bool` first and answers the element
/// of the two arms, so a rule instantiated on the first port would be a `bool` rule applied to a
/// float batch -- and the arms carry the element the answer has. The node's own `dataType()` is no
/// better: `argmax_axis` takes a tensor of numbers and answers `int64` positions, so asking the node
/// would instantiate its rule at `int64` and apply it to floats. The first port whose element is not
/// a condition is the one that names the rule's `T`; an operator that is all conditions (none today)
/// falls back to its first port.
template <class Graph>
inline camel::core::type::Type *firstOperandTypeOf(
    const Graph &function, camel::runtime::gc_node_ref_t ref) {
    using camel::core::type::TypeCode;
    camel::core::type::Type *first = nullptr;
    for (const camel::runtime::gc_node_ref_t input : function.normInputsOf(ref)) {
        if (input >= function.nodeCount()) {
            continue;
        }
        const auto *operand = function.node(input);
        if (operand == nullptr) {
            continue;
        }
        camel::core::type::Type *element = ruleElementTypeOf(operand->dataType());
        const bool isCondition = element != nullptr && element->code() == TypeCode::Bool;
        if (first == nullptr) {
            first = operand->dataType();
        }
        if (!isCondition) {
            return operand->dataType();
        }
    }
    return first;
}

} // namespace camel::transforms::rules