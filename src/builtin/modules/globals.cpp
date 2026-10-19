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
 * Created: Jul. 29, 2025
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "builtin/operators/derivative.h"
#include "builtin/operators/ops.h"
#include "builtin/operators/collection.h"
#include "builtin/operators/tagged.h"
#include "camel/core/context/context.h"
#include "camel/core/type/resolver.h"

#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>

using namespace std;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {

/// Whether a value of type `type` exists at run time, so that an operator taking a value of
/// every type (`string(x)`) may take it: not `void`, not a type the compiler uses on its own
/// (a type variable, a union, a reference). This is what a parameter typed `any` used to accept.
bool isConcreteValueType(Type *type) {
    return type != nullptr && type->code() != TypeCode::Void && !isAuxiliary(type->code());
}

} // namespace

module_ptr_t GlobalsBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<GlobalsBuiltinModule>(ctx);
}

const std::vector<oper_group_ptr_t> &getGlobalOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        // ======= Type conversions =======
        OperatorGroup::create(
            "i32",
            {
                {
                    ":op/itoi",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/ltoi",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int32()),
                },
                {
                    ":op/ftoi",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int32()),
                },
                {
                    ":op/dtoi",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int32()),
                },
                {
                    ":op/stoi",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int32()),
                },
            }),
        OperatorGroup::create(
            "i64",
            {
                {
                    ":op/itol",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int64()),
                },
                {
                    ":op/ltol",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/ftol",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int64()),
                },
                {
                    ":op/dtol",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int64()),
                },
                {
                    ":op/stol",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "int",
            {
                {
                    ":op/itol",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int64()),
                },
                {
                    ":op/ltol",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/ftol",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int64()),
                },
                {
                    ":op/dtol",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int64()),
                },
                {
                    ":op/stol",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "f32",
            {
                {
                    ":op/itof",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float32()),
                },
                {
                    ":op/ltof",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float32()),
                },
                {
                    ":op/ftof",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":op/dtof",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float32()),
                },
                {
                    ":op/stof",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float32()),
                },
            }),
        OperatorGroup::create(
            "f64",
            {
                {
                    ":op/itod",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float64()),
                },
                {
                    ":op/ltod",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float64()),
                },
                {
                    ":op/ftod",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float64()),
                },
                {
                    ":op/dtod",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
                {
                    ":op/stod",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "float",
            {
                {
                    ":op/itod",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float64()),
                },
                {
                    ":op/ltod",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float64()),
                },
                {
                    ":op/ftod",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float64()),
                },
                {
                    ":op/dtod",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
                {
                    ":op/stod",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "string",
            {
                {
                    ":op/itos",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
                {
                    ":op/ltos",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String()),
                },
                {
                    ":op/ftos",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::String()),
                },
                {
                    ":op/dtos",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::String()),
                },
                {
                    ":op/stos",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String()),
                },
                {
                    // Every other type: the text println writes for the value. A type rule,
                    // not a parameter of type `any` (design principles 5.10): the operator
                    // reads the argument with the static type it has at the call.
                    ":op/atos",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(x: T) => string",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!isConcreteValueType(norm[0])) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
            }),

        // ======= Variables =======
        // A `var` variable is a cell (TupleType::cell). The compiler emits these two for a
        // declaration and for a read; assignment is the family below.
        OperatorGroup::create(
            "__var_new__",
            {
                {
                    ":var/new",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(value: T) => var T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> { return TupleType::cell(norm[0]->widened()); }),
                },
            }),
        OperatorGroup::create(
            "__var_get__",
            {
                {
                    ":var/get",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(variable: var T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (valueTypeOf(norm[0]) == norm[0]) {
                                return nullopt; // not a variable
                            }
                            return valueTypeOf(norm[0]);
                        }),
                },
            }),

        // A read of a variable that leaves its sole-reference mark set; the compiler writes it
        // in place of a `:var/get` whose value cannot be kept (Builder::markNonRetainingReads).
        OperatorGroup::create(
            "__var_peek__",
            {
                {
                    ":var/peek",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(variable: var T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (valueTypeOf(norm[0]) == norm[0]) {
                                return nullopt; // not a variable
                            }
                            return valueTypeOf(norm[0]);
                        }),
                },
            }),

        // `xs = xs->sort` as a statement: the compiler writes these two groups around the
        // in-place form of the operator (Builder::inPlaceRebindingOf, __builtin__var_take__);
        // no program can name them. Arrays only: the mark they rely on is one of arrays.
        OperatorGroup::create(
            "__var_take__",
            {
                {
                    ":var/take",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "(var variable: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_owned__",
            {
                {
                    ":op/assn_owned",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: T[], value: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array || !norm[0]->equals(norm[1]))
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),

        // ======= Assignment operators =======
        // `=` takes any type. A compound assignment takes exactly the types listed for it: a
        // combination without an implementation is rejected when the program is compiled.
        OperatorGroup::create(
            "__assn__",
            {
                {
                    ":op/assn_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/assn_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        Type::String()),
                },
                {
                    ":op/assn_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/assn",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: typeas T, other: T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // The target keeps its declared type; the value only has to fit it.
                            if (!norm[0]->assignableFrom(norm[1])) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
            }),
        // `acc = acc->append<v>` written as a statement: the compiler writes this group for it
        // (Builder::rebindingAppendOf), no program can name it. The variable is bound to the
        // new array, which is built in place when only the variable refers to the old one
        // (__builtin__assn_append__). Its types are those of `append`.
        OperatorGroup::create(
            "__assn_append__",
            {
                {
                    ":op/assn_append",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: T[], value: T) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            if (!arrType->elemType()->equals(norm[1]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_add__",
            {
                {
                    ":op/assn_add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/assn_add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        Type::String()),
                },
            }),
        OperatorGroup::create(
            "__assn_sub__",
            {
                {
                    ":op/assn_sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__assn_mul__",
            {
                {
                    ":op/assn_mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__assn_div__",
            {
                {
                    ":op/assn_div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__assn_mod__",
            {
                {
                    ":op/assn_mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
            }),
        // No built-in type has `@`; a module that has it (tensor) provides `@=` or it is rejected.
        OperatorGroup::create("__assn_mat__", {}),
        OperatorGroup::create(
            "__assn_pow__",
            {
                {
                    ":op/assn_pow_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/assn_pow_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/assn_pow_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/assn_pow_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__assn_and__",
            {
                {
                    ":op/assn_and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__assn_or__",
            {
                {
                    ":op/assn_or",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        // ======= Logical operators =======
        OperatorGroup::create(
            "__and__",
            {
                {
                    ":op/and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__or__",
            {
                {
                    ":op/or",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        // ======= Comparison operators =======
        OperatorGroup::create(
            "__eq__",
            {
                {
                    ":op/eq_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: T, b: T) => bool, T an array, tuple or struct of comparable parts",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // By content (design principles 4.2): both sides of one type,
                            // an array, a tuple or a struct, every part of which is itself
                            // comparable (no function value, no tensor). Anything else has no
                            // overload, which is a located compile-time error.
                            const TypeCode code = norm[0]->code();
                            const bool composite =
                                code == TypeCode::Array || code == TypeCode::Struct ||
                                (code == TypeCode::Tuple &&
                                 !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                 !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            if (!composite || !norm[0]->equals(norm[1]) ||
                                !isContentComparable(norm[0])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
            }),
        OperatorGroup::create(
            "__neq__",
            {
                {
                    ":op/ne_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: T, b: T) => bool, T an array, tuple or struct of comparable parts",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // By content (design principles 4.2): both sides of one type,
                            // an array, a tuple or a struct, every part of which is itself
                            // comparable (no function value, no tensor). Anything else has no
                            // overload, which is a located compile-time error.
                            const TypeCode code = norm[0]->code();
                            const bool composite =
                                code == TypeCode::Array || code == TypeCode::Struct ||
                                (code == TypeCode::Tuple &&
                                 !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                 !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            if (!composite || !norm[0]->equals(norm[1]) ||
                                !isContentComparable(norm[0])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
            }),
        // `===` and `!==` are not supported for now (design principles 4.2): the front end
        // refuses them, so there is no operator behind them.
        // <、<=、>、>=
        OperatorGroup::create(
            "__lt__",
            {
                {
                    ":op/lt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__le__",
            {
                {
                    ":op/le_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__gt__",
            {
                {
                    ":op/gt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__ge__",
            {
                {
                    ":op/ge_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
            }),
        // ======= Arithmetic operators =======
        OperatorGroup::create(
            "__add__",
            {
                {
                    ":op/add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::String()),
                },
            }),
        OperatorGroup::create(
            "__sub__",
            {
                {
                    ":op/sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__mul__",
            {
                {
                    ":op/mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__div__",
            {
                {
                    ":op/div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__mod__",
            {
                {
                    ":op/mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "__pow__",
            {
                {
                    ":op/pow_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/pow_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/pow_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/pow_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create("__mat__", {}),
        OperatorGroup::create(
            "__idx__",
            {
                {
                    ":op/idx_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(arr: T[], index: int) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Int64)
                                return nullopt;
                            return tt::as_ptr<ArrayType>(norm[0])->elemType();
                        }),
                },
                {
                    ":op/idx_str",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(str: string, index: int) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::String ||
                                norm[1]->code() != TypeCode::Int64)
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        // ======= Unary operators =======
        OperatorGroup::create(
            "__not__",
            {
                {
                    ":op/not",
                    StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__neg__",
            {
                {
                    ":op/neg_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/neg_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/neg_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":op/neg_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "__inv__",
            {
                {
                    ":op/inv_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/inv_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
            }),
        // ======= IO (built-in input/print/println) ======
        OperatorGroup::create(
            "input",
            {
                {
                    ":io/input",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(prompt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "print",
            {
                {
                    ":io/print",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "<fmt?: string> (x: T, ...rest: U...) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() > 0 && with[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            if (norm.size() < 1) {
                                return Type::Void();
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "println",
            {
                {
                    ":io/println",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "<fmt?: string> (x: T, ...rest: U...) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() > 0 && with[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            if (norm.size() < 1) {
                                return Type::Void();
                            }
                            return norm[0];
                        }),
                },
            }),
        // ======= OS (implemented in os.cmo) ======
        OperatorGroup::create(
            "sleep",
            {
                {
                    "os:sleep",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "exit",
            {
                {
                    "os:exit",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        // ======= string =======
        OperatorGroup::create(
            "format",
            {
                {
                    ":str/format",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {1, {false}}},
                        "<...args: T...> (fmt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "join",
            {
                {
                    ":str/join",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<sep: string> (vec: string[]) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            const auto &type = norm[0];
                            if (type->code() != TypeCode::Array) {
                                return nullopt;
                            }
                            const auto &arrType = tt::as_ptr<ArrayType>(type);
                            if (arrType->elemType()->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
            }),
        // ======= struct =======
        OperatorGroup::create(
            "len",
            {
                {
                    ":struct/len_str",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64()),
                },
                {
                    ":struct/len_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (arr: T[]) => int",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return Type::Int64();
                        }),
                },
            }),
        OperatorGroup::create(
            "zip",
            {
                {
                    ":struct/zip",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(...args: (typeas T)[]) => (T, T, ...)[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm.size() < 2)
                                return nullopt;
                            for (const auto &t : norm) {
                                if (t->code() != TypeCode::Array)
                                    return nullopt;
                            }
                            type_vec_t tupleTypes;
                            for (size_t i = 0; i < norm.size(); i++) {
                                const auto &elemType = tt::as_ptr<ArrayType>(norm[i])->elemType();
                                tupleTypes.push_back(elemType);
                            }
                            return ArrayType::create(TupleType::create(std::move(tupleTypes)));
                        }),
                },
            }),
        OperatorGroup::create(
            "head",
            {
                {
                    ":struct/head_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (arr: T[]) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return arrType->elemType();
                        }),
                },
            }),
        OperatorGroup::create(
            "tail",
            {
                {
                    ":struct/tail_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return ArrayType::create(arrType->elemType());
                        }),
                },
            }),
        OperatorGroup::create(
            "range",
            {
                {
                    ":struct/range",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(start: int, end: int) => int[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Int64)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Int64)
                                return nullopt;
                            return ArrayType::create(Type::Int64());
                        }),
                },
            }),
        OperatorGroup::create(
            "slice",
            {
                {
                    ":struct/slice_arr",
                    DynamicFuncTypeResolver::create(
                        {{2, {false, false}}, {1, {false}}},
                        "<start: int, end: int> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Int64)
                                return nullopt;
                            if (with[1]->code() != TypeCode::Int64)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "concat",
            {
                {
                    ":struct/concat_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (arr: T[], other: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType      = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_ptr<ArrayType>(norm[1]);
                            if (!arrType->elemType()->equals(otherArrType->elemType()))
                                return nullopt;
                            return ArrayType::create(arrType->elemType());
                        }),
                },
            }),
        OperatorGroup::create(
            "append",
            {
                {
                    ":struct/append_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &eleType = arrType->elemType();
                            if (eleType != Type::Void() && !eleType->equals(with[0]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "extend",
            {
                {
                    ":struct/extend_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<other: T[]> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType      = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_ptr<ArrayType>(with[0]);
                            if (!arrType->equals(otherArrType))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "contains",
            {
                {
                    ":struct/contains_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (arr: T[]) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            if (!arrType->elemType()->equals(with[0]))
                                return nullopt;
                            return Type::Bool();
                        }),
                },
                {
                    ":struct/contains_str",
                    StaticFuncTypeResolver::create(
                        {{Type::String(), false}},
                        {{Type::String(), false}},
                        Type::Bool()),
                },
            }),
        // ======= Marked Operators =======
        OperatorGroup::create(
            "map",
            {
                {
                    ":mark/map_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => U> (collect: T[]) => U[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType  = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &elemType = arrType->elemType();
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(elemType))
                                return nullopt;
                            ASSERT(funcType->hasExitType(), "Function must have exit type");
                            return ArrayType::create(funcType->exitType());
                        }),
                },
            }),
        OperatorGroup::create(
            "apply",
            {
                {
                    ":mark/apply_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => T> (collect: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType  = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &elemType = arrType->elemType();
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(elemType))
                                return nullopt;
                            if (!funcType->exitType()->equals(elemType))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "filter",
            {
                {
                    ":mark/filter_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => bool> (collect: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            // The predicate is the one with-argument (it was read at
                            // index 1, past the end: every use of filter crashed).
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(arrType->elemType()))
                                return nullopt;
                            if (!funcType->hasExitType() ||
                                !funcType->exitType()->equals(Type::Bool()))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "reduce",
            {
                {
                    ":mark/reduce_arr",
                    DynamicFuncTypeResolver::create(
                        {{2, {false, false}}, {1, {false}}},
                        "<f: (acc: U, item: T) => U, initial: U> (collect: T[]) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() < 2)
                                return nullopt;
                            if (!funcType->normTypeAt(1)->equals(arrType->elemType()))
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(with[1]))
                                return nullopt;
                            if (!funcType->exitType()->equals(with[1]))
                                return nullopt;
                            return with[1];
                        }),
                },
            }),
        OperatorGroup::create(
            "foreach",
            {
                {
                    ":mark/foreach_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => void> (collect: T[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(arrType->elemType()))
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        // ======= Macros =======
        OperatorGroup::create(
            "__cmp__",
            {
                {
                    ":macro/cmp",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(lhs: (arg1: T1, arg2: T2, ...) => R, rhs: (arg: R) => U) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Function)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &lhsFuncType   = tt::as_ptr<FunctionType>(norm[0]);
                            const auto &rhsFuncType   = tt::as_ptr<FunctionType>(norm[1]);
                            const auto &lhsReturnType = lhsFuncType->exitType();
                            if (rhsFuncType->normTypesCount() != 1)
                                return nullopt;
                            if (!rhsFuncType->normTypeAt(0)->equals(lhsReturnType))
                                return nullopt;
                            return rhsFuncType->exitType();
                        }),
                },
            }),
    };
    return groups;
}

GlobalsBuiltinModule::GlobalsBuiltinModule(context_ptr_t ctx) : BuiltinModule("", ctx) {
    registerBuiltinOperatorTraits();
    registerBuiltinDerivatives();
    // The operators over Map and Set (design principles 11.3). Those named like a group of
    // this module (`len`, `contains`, `__idx__`, `__eq__`, `__neq__`) are further overloads
    // of it: one group is exported per name, the existing overloads first.
    std::vector<oper_group_ptr_t> collectionGroups = getCollectionOperatorGroups();
    for (const auto &group : getGlobalOperatorGroups()) {
        const auto extra = std::find_if(
            collectionGroups.begin(),
            collectionGroups.end(),
            [&](const oper_group_ptr_t &other) { return other->name() == group->name(); });
        if (extra == collectionGroups.end()) {
            exportEntity(group->name(), group);
            continue;
        }
        auto resolvers = group->resolvers();
        resolvers.insert(resolvers.end(), (*extra)->resolvers().begin(), (*extra)->resolvers().end());
        exportEntity(group->name(), OperatorGroup::create(group->name(), resolvers));
        collectionGroups.erase(extra);
    }
    for (const auto &group : collectionGroups) {
        exportEntity(group->name(), group);
    }
    // The operators over T? and T! (design principles 3.8).
    for (const auto &group : getTaggedOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool GlobalsBuiltinModule::load() {
    CAMEL_LOG_INFO_S("GlobalsBuiltinModule", "Loading basic built-in module.");
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("", [ctx = context_]() {
        return BasicBuiltinExecutor::create(ctx);
    });
    loaded_ = true;
    return true;
}
