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
 * Created: Oct. 11, 2026
 * Updated: Oct. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Type variables and type functions (design principles 2.9).
 *
 * Both exist only while a module is compiled; neither ever reaches a graph.
 *
 * - TypeVarType is a named hole in a type. A generic function binds one with `typeas T` at the
 *   position of its signature where the type first appears and names it `T` afterwards; a
 *   parameterised type declaration (`type Pair<T> = (T, T)`) names its parameters the same
 *   way. A generic function is a subgraph template: every call binds the variables from the
 *   argument types and builds a subgraph in which each variable is replaced by its binding.
 * - TypeFunctionType is what a parameterised type declaration declares: parameter names and a
 *   body, an ordinary type expression over them. Applying it (`Pair<int>`) substitutes the
 *   arguments into the body, so a use site only ever holds the resulting type.
 *
 * The free functions are the whole algebra over them: test, substitute, bind.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"

#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

class TypeVarType : public Type {
  public:
    /// `binder`: this occurrence is the one written `typeas T`, where the variable takes the
    /// type found at its position. Every other occurrence is a use.
    static TypeVarType *create(const std::string &name, bool binder);

    const std::string &name() const { return name_; }
    bool binder() const { return binder_; }

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    /// Occurrences of one variable are equal whether or not they bind.
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;

  private:
    TypeVarType(const std::string &name, bool binder);

    std::string name_;
    bool binder_;
};

/// The variables bound so far and their types, in binding order.
using type_bindings_t = std::vector<std::pair<std::string, Type *>>;

/// The type bound to `name`, or nullptr.
Type *findTypeBinding(const type_bindings_t &bindings, const std::string &name);

class TypeFunctionType : public Type {
  public:
    static TypeFunctionType *
    create(const std::string &name, std::vector<std::string> params, Type *body);

    const std::string &name() const { return name_; }
    const std::vector<std::string> &params() const { return params_; }
    Type *body() const { return body_; }

    /// The body with each parameter replaced by the argument at its position. The number of
    /// arguments must equal the number of parameters.
    Type *apply(const type_vec_t &arguments) const;

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;

  private:
    TypeFunctionType(const std::string &name, std::vector<std::string> params, Type *body);

    std::string name_;
    std::vector<std::string> params_;
    Type *body_;
};

/// Whether a type variable occurs anywhere in `type`.
bool containsTypeVar(Type *type);

/**
 * `type` with every variable that `bindings` binds replaced by its binding; a variable they do
 * not bind stays. Returns `type` itself (the same object) when nothing in it was replaced, so
 * code that holds no bindings is not affected.
 */
Type *substituteTypeVars(Type *type, const type_bindings_t &bindings);

/**
 * A new function type: `type` with the variables replaced as substituteTypeVars does, and with
 * its parameter names, `var` marks, modifiers and implementation mark kept. Always a new
 * object, also when nothing was replaced: a graph completes its function type in place (return
 * type, captures), so two graphs never share one.
 */
FunctionType *instantiateFunctionType(FunctionType *type, const type_bindings_t &bindings);

/**
 * Binds the variables `pattern` binds (its `typeas` occurrences) from the type `actual` found
 * at the same position, appending to `bindings`. Only binding occurrences are looked at: a
 * variable that is already bound, and every use, is left for the ordinary check of the
 * substituted type against the argument. Returns false when `actual` does not have the shape
 * `pattern` needs to reach a binding occurrence (an array where a tuple is, tuples of
 * different sizes, ...).
 */
bool bindTypeVars(Type *pattern, Type *actual, type_bindings_t &bindings);

/// The names of the variables `pattern` can bind (see bindTypeVars), appended to `names`
/// without duplicates. A `typeas` inside a union or among the arguments of a module type is
/// not among them: there is no position of the argument type to take it from.
void collectBindableTypeVars(Type *pattern, std::vector<std::string> &names);

/// A `T??` or `T!!` anywhere in `type`, or nullptr: the same mark twice, which a type
/// expression rejects where it is written and which substitution can still produce
/// (`(typeas T)?` with `T = int?`).
TupleType *findRepeatedMark(Type *type);

/// `T = int, U = string`, for diagnostics and instance names.
std::string typeBindingsToString(const type_bindings_t &bindings);

} // namespace camel::core::type
