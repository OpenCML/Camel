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
 * Author: Zhenjie Wei
 * Created: May. 05, 2024
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

/** GCT enums and basic types: the kinds of nodes and what distinguishes nodes of a kind. */

#include <cstdint>
#include <string>

namespace camel::compile::gct {

class Node;
class Tree;

/// A node of the tree. Nodes belong to the Tree that made them and live as long as it; a
/// null pointer is "no node".
using node_ptr_t = Node *;

/// The kind of a node: what it stands for and which accessors of Node apply to it.
enum class NodeKind : uint8_t {
    DECL, // a function or type declared ahead of its use; child: TYPE
    FUNC, // a function; children: TYPE, EXEC (the body), ANNO...
    DATA, // a constant
    TYPE, // a type (of a declaration, of a function)
    NREF, // a name bound to the value of its child
    DREF, // a name read
    VARI, // a variable; child: what it is initialised with
    WAIT, // `wait`; child: the expression waited for
    LINK, // a call; children: the callee, the arguments
    WITH, // with-arguments bound to a callee; children: the callee, the arguments
    ACCS, // an element or a field of its child
    BRCH, // a branch; children: what is branched on, CASE...
    CASE, // an arm of a branch
    CAST, // a conversion of its child to a type
    ANNO, // an annotation of a function; child: the annotation expression
    EXIT, // `return` / `throw`; child: the value
    EXEC, // a block
    EXPT, // the names a module exports
};

std::string to_string(NodeKind kind);

/// What selects the arm of a BRCH node (design principles 2.9; all are the same structure on
/// the graph).
///  - Cond: `if` (a boolean) and `match` (a value compared with the cases, or the tag of a
///    `T?` / `T!` with the fixed patterns). Children: condition, CASE...
///  - NullThen: `a ?? b`. Children: a, CASE(True)[EXEC b].
///  - ErrorThen: `a !! b`. Children: a, CASE(True)[EXEC b].
///  - ErrorPass: `x ?-> f`. Children: x, CASE(Else)[EXEC call], where the call takes
///    DREF kPassedValueName as its argument.
///  - LogicalAnd: `a && b`, which is `if a then b else false` (design principles 2.11).
///    Children: a, CASE(True)[EXEC b], CASE(Else)[EXEC false].
///  - LogicalOr: `a || b`, which is `if a then true else b`.
///    Children: a, CASE(True)[EXEC true], CASE(Else)[EXEC b].
///    Both have exactly the children of that `if`; the kind only records what was written,
///    so that the operands are required to be `bool` and reported as operands.
enum class BranchKind : uint8_t { Cond, NullThen, ErrorThen, ErrorPass, LogicalAnd, LogicalOr };

/// The pattern of a CASE node.
///  - Value: a value the scrutinee is compared with. Children: the value, the body.
///  - True / Else: the arms of an `if`. One child, the body.
///  - Bind: `case v` over a tagged scrutinee (design principles 2.9): the value taken out of
///    a `T?` / `T!` under the name binding(). One child, the body.
///  - Error: `case error e`: the error of a failed `T!` under that name. One child, the body.
enum class CaseType : uint8_t { Value, True, Else, Bind, Error };

/// The reserved name under which the arm of `x ?-> f` finds the value taken out of `x`. It
/// is not an identifier, so no program can write it.
inline constexpr const char *kPassedValueName = "$passed";
/// The name of `let _ = e`: the value of `e` is dropped on purpose and no name is bound
/// (design principles 2.11). An NREF with this name declares nothing.
inline constexpr const char *kDiscardName = "_";

} // namespace camel::compile::gct
