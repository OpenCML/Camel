// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/** GCT enums and basic types: the kinds of nodes and what distinguishes nodes of a kind. */

#include <cstdint>
#include <string>
#include <vector>

namespace camel::core::type {
class Type;
}

namespace camel::compile::gct {

class Node;
class Tree;

/// A node of the tree. Nodes belong to the Tree that made them and live as long as it; a
/// null pointer is "no node".
using node_ptr_t = Node *;

/// The name the type layer holds for a `typeof e` before the front end resolves it: this prefix
/// and the number `Tree::makeTypeOfPlaceholder` gave the expression. `#` is not a character an
/// identifier can hold, so a type variable a program writes can never collide with one. The type
/// layer knows nothing else about it: it is an ordinary type variable there.
inline constexpr const char *kTypeOfPlaceholderPrefix = "typeof#";

/// Whether `type` is such a placeholder (and not a type variable a program wrote).
bool isTypeOfPlaceholder(const camel::core::type::Type *type);

/// Whether a placeholder occurs anywhere in `type`, in any position: the ones the front end
/// resolves (an argument of a type function, the subject of a `match type`, the arguments of a
/// named type) and the ones it refuses with `TypeOfNotWholeType` (inside an array, a tuple, a
/// function type) alike. A type function that is not applied yet is not searched: its body is not
/// part of the type.
bool hasTypeOfPlaceholder(const camel::core::type::Type *type);

/// The type variables of `type`, as `collectTypeVars` names them, without the placeholders: a
/// placeholder is not a variable a signature binds -- the front end resolves it where the type is
/// used -- so a caller asking which variables a signature declares must not see one. Every number
/// it does not answer for is the same before and after the placeholders were moved out of the type
/// layer, since the type layer never held them as variables.
void collectTypeVarsWithoutPlaceholders(
    camel::core::type::Type *type, std::vector<std::string> &names);

/// The kind of a node: what it stands for and which accessors of Node apply to it.
enum class NodeKind : uint8_t {
    DECL, // a function or type declared ahead of its use; child: TYPE
    FUNC, // a function; children: TYPE, EXEC (the body), ANNO...
    DATA, // a constant
    TYPE, // a type (of a declaration, of a function)
    NREF, // a name bound to the value of its child
    DREF, // a name read
    VARI, // a variable; child: what it is initialised with
    LINK, // a call; children: the callee, the arguments
    WITH, // with-arguments bound to a callee; children: the callee, the arguments
    TAPP, // explicit type application; children: the callee, TYPE arguments; no runtime value
    ACCS, // an element or a field of its child
    BRCH, // a branch; children: what is branched on, CASE...
    CASE, // an arm of a branch
    CAST, // a conversion of its child to a type
    ANNO, // an annotation of a function; child: the annotation expression
    EXIT, // `return`; child: the value
    EXEC, // a block
    EXPT, // the names a module exports
    LOOP, // a loop (design principles 3.10); children: see Node::loopKind
    TMTCH, // `match type` (design principles 5.5); children: TYPE, TCASE...
    TCASE, // a case of `match type`; one child, the body
    SFOR,  // `static for`: children: TYPE, EXEC; payload: the value and type names
    SACC,  // `t.[key]`: children: the target, the key expression
    SCOMP, // a comprehension: child: the SFOR that generates it; tag: 0 struct, 1 tuple
    SLOOP, // `static loop`: children: TYPE, the first values, EXEC body; payload: the
           // names of the state, then the field name and its type
    SPRD,  // `..e`, a rest argument; child: the compile-time tuple it expands
    ASSIGN, // a write of its target; children: the target, the value; tag: AssignmentForm;
            // payload: the group of the compound operator a compound write follows
};

std::string to_string(NodeKind kind);

/// What a write is. The kind says the node writes its target; the form says which write it is,
/// since a write is not a call and the forms differ in what the write means, not in how the node
/// is read.
///  - Plain: `x = e`, and `v.f = e` / `v[i] = e` (the target is the path it writes). A write the
///    program wrote.
///  - Compound: `x op= e`, on a name or on a path. The operator it follows is the node's payload
///    (`__assn_add__` for `+=`): that operator's set of operand types is what decides whether the
///    compound assignment is defined, so the operator is asked, not a spelling.
///  - Rebuilt: the whole variable's `x = ...` the graph builder rebuilds a path write into
///    (`v.f = e` is `v = <v with the field written>`). The compiler's own write, not the
///    program's: a value nobody takes is not reported for it.
///  - Owned: `x = x->op<w...>()`, lowered to `__assn_owned__(x, op<w...>(x))`: the rebinding to
///    the result of an in-place operator.
enum class AssignmentForm : uint8_t { Plain, Compound, Rebuilt, Owned };

std::string to_string(AssignmentForm form);

/// What selects the arm of a BRCH node (design principles 1; all are the same structure on
/// the graph).
///  - Cond: `if` (a boolean) and `match` (a value compared with the cases, or the tag of a
///    `T?` / `T!` with the fixed patterns). Children: condition, CASE...
///  - NullThen: `a ?? b`. Children: a, CASE(True)[EXEC b].
///  - ErrorThen: `a !! b`. Children: a, CASE(True)[EXEC b].
///  - LogicalAnd: `a && b`, which is `if a then b else false` (design principles 4.2).
///    Children: a, CASE(True)[EXEC b], CASE(Else)[EXEC false].
///  - LogicalOr: `a || b`, which is `if a then true else b`.
///    Children: a, CASE(True)[EXEC true], CASE(Else)[EXEC b].
///    Both have exactly the children of that `if`; the kind only records what was written,
///    so that the operands are required to be `bool` and reported as operands.
enum class BranchKind : uint8_t { Cond, NullThen, ErrorThen, LogicalAnd, LogicalOr };

/// The pattern of a CASE node.
///  - Value: a value the scrutinee is compared with. Children: the value, the body.
///  - True / Else: the arms of an `if`. One child, the body.
///  - Bind: `case v` over a tagged scrutinee (design principles 3.8): the value taken out of
///    a `T?` / `T!` under the name binding(). One child, the body.
///  - Error: `case error e`: the error of a failed `T!` under that name. One child, the body.
///  - Enum: `case Shape::Rect { w, h }`: a variant of an enum. One child, the body; the
///    variant and the fields the pattern binds are in the node's EnumCase (design principles
///    5.3). Which variant that is, and what its fields are, the graph builder decides from
///    the type of the value matched.
enum class CaseType : uint8_t { Value, True, Else, Bind, Error, Enum };

/// What a `match type` case selects, in the same order as the AST (design principles 5.5, R3).
/// Struct, Tuple, Func, Leaf and Wildcard are shapes and a TCASE holds a mask of them:
/// `case struct | tuple` selects either. Identity (`case int`, `case T`) is not a shape: the
/// TCASE holds the type to compare the subject with, exactly, in a separate list.
enum class TypeCaseKind : uint8_t {
    Struct   = 0,
    Tuple    = 1,
    Leaf     = 2,
    Wildcard = 3,
    Func     = 4,
    Identity = 5,
    Array    = 6,
    Nullable = 7,
    Fallible = 8,
    Map      = 9,
    Set      = 10
};
inline constexpr uint16_t typeCaseBit(TypeCaseKind kind) {
    return static_cast<uint16_t>(1u << static_cast<uint8_t>(kind));
}

/// The reserved name under which the arm of a pipe `x -> f` finds the value taken out of `x`. It
/// is not an identifier, so no program can write it.
inline constexpr const char *kPassedValueName = "$passed";
/// The name of `let _ = e`: the value of `e` is dropped on purpose and no name is bound
/// (design principles 3.9). An NREF with this name declares nothing.
inline constexpr const char *kDiscardName = "_";

} // namespace camel::compile::gct
