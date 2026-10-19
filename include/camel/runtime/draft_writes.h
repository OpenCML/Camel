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
 * Created: Oct. 04, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * In-place writes on a graph draft.
 *
 * A node is not always a single value. An argument passed to a `var` parameter is written by
 * the callee. For a function and for the assignment operators (`:op/assn*`) the argument is a
 * variable: the cell that `:var/new` created (TupleType::cell), which the callee stores into.
 * For any other operator it is the object the operator changes in place (an in-place sort, an
 * append). The graph expresses this only through the callee's signature and the control edges
 * that order later readers after the writer. A rewrite that treats such a node as one value
 * (forwarding what was filled into it, merging two reads of it, replacing it by a constant)
 * changes the program, so rewrites ask here first.
 */

#pragma once

#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"

#include <string_view>
#include <vector>

namespace camel::runtime {

/**
 * The function type the overload of operator `uri` has for arguments of these types, when the
 * values of constant arguments are left out of the question (they only refine the result type);
 * nullptr when the operator is unknown or rejects the types.
 *
 * Asking a resolver builds a new function type in the permanent space every time, and that
 * space never releases. Rewrites ask the same question over and over (is this argument a var
 * parameter, what does this operator return), so the answer is kept per (operator, argument
 * types): types are permanent objects, their addresses identify them for good, and a repeated
 * question allocates nothing.
 */
camel::core::type::FunctionType *operatorOverloadType(
    std::string_view uri, const camel::core::type::type_vec_t &with,
    const camel::core::type::type_vec_t &norm);

/**
 * True when the argument at position `index` of `user`'s norm (or with) inputs is written by
 * `user`: for an OPER, what the operator's resource declaration says (design principles 2.17;
 * an operator without one is read conservatively, OperatorTable::writesOperand); for a FUNC,
 * a `var` parameter of the graph it calls; for a CALL, a `var` parameter of its callee's type.
 */
bool isWrittenArgument(const GraphDraft &draft, gc_node_ref_t user, bool isWith, size_t index);

/**
 * True when the value `id` holds may be written in place: it is passed to a `var` parameter,
 * directly or through GATEs. A GATE has no slot of its own, so writing it writes the value it
 * forwards, and a GATE over a written value is written too.
 */
bool isWrittenInPlace(const GraphDraft &draft, gc_node_ref_t id);

/**
 * isWrittenInPlace for every node of a draft at once: the written slots are computed once, so a
 * pass that asks about many nodes does not walk the users of each. A snapshot: valid while the
 * pass adds no writer and no alias, as a rewrite of unwritten pure nodes does not.
 */
class WrittenSlots {
  public:
    explicit WrittenSlots(const GraphDraft &draft);
    bool operator()(gc_node_ref_t id) const { return id < written_.size() && written_[id]; }

  private:
    std::vector<bool> written_;
};

} // namespace camel::runtime
