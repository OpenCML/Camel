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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * What the builders know about the names of a program, told to whoever asks.
 *
 * A compilation resolves every name and types every expression, and then keeps only the graph.
 * A tool that answers questions about the source (the language server: what is this name, what
 * is its type, where is it declared) needs those facts. The builders report them to a
 * SemanticRecorder when the context has one; without one (every ordinary compilation) a
 * report site costs a test of a null pointer.
 *
 * A fact is keyed by the origin of the syntax it is about (camel/core/source/manager.h), so a
 * recorder turns it into a source range without knowing the trees. Reporting never changes
 * what is built. The recorder is called while the builders run: it must only store.
 */

#pragma once

#include "camel/core/source/ids.h"

#include <string>
#include <vector>

namespace camel::core::type {
class Type;
class FunctionType;
} // namespace camel::core::type

namespace camel::compile {

class SemanticRecorder {
  public:
    using origin_id_t  = camel::source::origin_id_t;
    using Type         = camel::core::type::Type;
    using FunctionType = camel::core::type::FunctionType;

    virtual ~SemanticRecorder() = default;

    /// A type expression at `expr` (an AST origin) stands for `type`.
    virtual void typeExpression(origin_id_t expr, Type *type) = 0;

    /// The declaration at `decl` binds `name` to a value of `type`; `variable` for `var`.
    /// For a parameter `decl` is the function it belongs to.
    virtual void
    valueDeclared(origin_id_t decl, const std::string &name, Type *type, bool variable) = 0;

    /// Where the value of a name declared with valueDeclared lives when the program runs: in
    /// the graph `graph` (stable id) the name stands for the node whose origin is `node`.
    /// `node` is invalid for a parameter or a captured name: those are ports of the graph,
    /// found by position (the names are in the graph's function type). A debugger reads the
    /// value of a name from the slot of that node (docs/technical/34_debugger.md, 6.1).
    /// Optional: the default ignores the report.
    virtual void valueBound(
        origin_id_t decl, const std::string &name, const std::string &graph, origin_id_t node,
        Type *type, bool variable) {
        (void)decl;
        (void)name;
        (void)graph;
        (void)node;
        (void)type;
        (void)variable;
    }

    /// The expression at `expr` has a value of `type`.
    virtual void expressionTyped(origin_id_t expr, Type *type) = 0;

    /// The name at `ref` stands for the value that `decl` bound to `declName` (see
    /// valueDeclared); `decl` is invalid when the builder does not know the declaration.
    virtual void valueReferenced(
        origin_id_t ref, const std::string &name, origin_id_t decl, Type *type) = 0;

    /// The name at `ref` stands for the functions whose graphs have the stable ids `graphs`
    /// (several: overloads). The origin of a graph is in the debug map of the source context
    /// once its body has been built, which may be after this report.
    virtual void functionReferenced(
        origin_id_t ref, const std::string &name, std::vector<std::string> graphs) = 0;

    /// The name at `ref` stands for the generic function declared at `decl`.
    virtual void
    genericReferenced(origin_id_t ref, const std::string &name, origin_id_t decl) = 0;

    /// The name at `ref` stands for an operator group (a built-in or module function
    /// implemented outside the language); `signatures` are those of its overloads.
    virtual void operatorReferenced(
        origin_id_t ref, const std::string &name, std::vector<std::string> signatures) = 0;

    /// The call at `call`, whose callee expression is at `callee`, was resolved to a function
    /// of type `type`: the graph `graph` (empty for an operator or a function value), or the
    /// operator `operatorUri`.
    virtual void callResolved(
        origin_id_t call, origin_id_t callee, const std::string &graph,
        const std::string &operatorUri, FunctionType *type) = 0;

    /// The body of the function at `func` was built into the graph `graph`; `type` is its
    /// type, complete once the compilation is (an inferred return type is filled in place).
    /// A generic function is reported once per instance.
    virtual void functionBuilt(
        origin_id_t func, const std::string &name, const std::string &graph,
        FunctionType *type) = 0;
};

} // namespace camel::compile
