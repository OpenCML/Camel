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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "builtin/operators/collection.h"
#include "builtin/operators/mem.h"
#include "builtin/operators/derivative.h"
#include "builtin/operators/enum.h"
#include "builtin/operators/ops.h"
#include "builtin/operators/tagged.h"
#include "builtin/operators/set_value.h"
#include "camel/core/context/context.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"

#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>

using namespace std;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
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
            "c64",
            {
                {
                    ":op/itoc",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Complex64()),
                },
                {
                    ":op/ltoc",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Complex64()),
                },
                {
                    ":op/ftoc",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Float32(), false}}, Type::Complex64()),
                },
                {
                    ":op/dtoc",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Float64(), false}}, Type::Complex64()),
                },
                {
                    ":op/ctoc",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex64(), false}}, Type::Complex64()),
                },
                {
                    ":op/ztoc",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex128(), false}}, Type::Complex64()),
                },
            }),
        OperatorGroup::create(
            "c128",
            {
                {
                    ":op/itoz",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Complex128()),
                },
                {
                    ":op/ltoz",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Complex128()),
                },
                {
                    ":op/ftoz",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Float32(), false}}, Type::Complex128()),
                },
                {
                    ":op/dtoz",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Float64(), false}}, Type::Complex128()),
                },
                {
                    ":op/ctoz",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex64(), false}}, Type::Complex128()),
                },
                {
                    ":op/ztoz",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex128(), false}}, Type::Complex128()),
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
                    ":op/ctos",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex64(), false}}, Type::String()),
                },
                {
                    ":op/ztos",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex128(), false}}, Type::String()),
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
                {
                    ":op/assn_add_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_add_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/assn_sub_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_sub_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/assn_mul_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_mul_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/assn_div_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_div_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/assn_pow_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_pow_c_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Int32(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_pow_c_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Int64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/assn_pow_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/assn_pow_z_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Int32(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/assn_pow_z_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Int64(), false}},
                        Type::Complex128()),
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
                    ":op/eq_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
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
                            const TypeCode code  = norm[0]->code();
                            const bool composite = code == TypeCode::Array ||
                                                   code == TypeCode::Struct ||
                                                   (code == TypeCode::Tuple &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            // An enum compares by its variant and then its fields.
                            const bool comparable = composite || asEnumType(norm[0]) != nullptr;
                            if (!comparable || !norm[0]->equals(norm[1]) ||
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
                    ":op/ne_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
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
                            const TypeCode code  = norm[0]->code();
                            const bool composite = code == TypeCode::Array ||
                                                   code == TypeCode::Struct ||
                                                   (code == TypeCode::Tuple &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            // An enum compares by its variant and then its fields.
                            const bool comparable = composite || asEnumType(norm[0]) != nullptr;
                            if (!comparable || !norm[0]->equals(norm[1]) ||
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
                {
                    ":op/add_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/add_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/sub_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/sub_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/mul_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/mul_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                {
                    ":op/div_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/div_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
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
                // A complex exponent is a complex number; an integer exponent is the repeated
                // squaring form (an integer literal exponent becomes a complex literal, and
                // the two give the same bits — see the runtime's complexPower).
                {
                    ":op/pow_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_c_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Int32(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_c_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Int64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/pow_z_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Int32(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/pow_z_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Int64(), false}},
                        Type::Complex128()),
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
                    ":op/idx_take",
                    // `t[idx]` with an int array takes the rows the indices name, in that
                    // order (design principles 5.4; `take` in its function form).
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(arr: T[], indices: int[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &indices = tt::as_ptr<ArrayType>(norm[1]);
                            if (indices->elemType()->code() != TypeCode::Int64)
                                return nullopt;
                            return norm[0];
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
                {
                    ":op/neg_c",
                    StaticFuncTypeResolver::create({}, {{Type::Complex64(), false}}, Type::Complex64()),
                },
                {
                    ":op/neg_z",
                    StaticFuncTypeResolver::create(
                        {}, {{Type::Complex128(), false}}, Type::Complex128()),
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
        // ======= IO (built-in input/print/println/eprintln) ======
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
        // `println` on the standard error stream, with the same declaration shape (the author's
        // ruling on H134 question 2: it is in the prelude, so no import).
        OperatorGroup::create(
            "eprintln",
            {
                {
                    ":io/eprintln",
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
        // ======= process (implemented in process.cmo) ======
        OperatorGroup::create(
            "sleep",
            {
                {
                    "process:sleep",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "exit",
            {
                {
                    "process:exit",
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
        // The elements a `for` takes from a string (design principles 11.1): its code points,
        // each a one-character string. The loop builder converts a string to that array once,
        // before the first round; a program does not write `__iter__` itself. A map adds its
        // own overload (collection.cpp), and the tensor module adds one for tensors.
        OperatorGroup::create(
            "__iter__",
            {
                {
                    ":struct/iter_str",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (s: string) => string[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!with.empty() || norm.size() != 1 ||
                                norm[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return ArrayType::create(Type::String());
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
            "last",
            {
                {
                    ":struct/last_arr",
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
            "__slice__",
            {
                {
                    ":op/slice_sub",
                    // `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]`: the slice of the first
                    // dimension a subscript writes (design principles 5.4). The last argument is
                    // a two-bit mask: bit 0 says whether the end was written (`t[a:]` runs to
                    // the end), bit 1 whether the start was (`xs[:: -1]` starts at the last
                    // element).
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {5, {false, false, false, false, false}}},
                        "(arr: T[], start: int, end: int, step: int, written: int) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            for (size_t i = 1; i < 5; ++i) {
                                if (norm[i]->code() != TypeCode::Int64)
                                    return nullopt;
                            }
                            return norm[0];
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
                            // A value of the element type, or an unnamed struct value whose
                            // fields are its fields in the same order (design principles 5.2):
                            // the append writes the slot as it is, and the layout agrees.
                            if (eleType != Type::Void() && !eleType->assignableFrom(with[0]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        // A private copy of an array: one of the states of a loop whose body only appends to it
        // (Builder::markLoopStateInPlace, which decides per state), so the literal the program
        // wrote stays a constant and the loop appends to its own array. Not a name a program can
        // reach for on its own.
        OperatorGroup::create(
            "__copy_arr__",
            {
                {
                    ":struct/copy_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(arr: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        // `xs->append<v>` where the compiler proved the old array invisible: one state of a
        // loop body, with that state bound only to this call and its first value a fresh
        // literal (Builder::markLoopStateInPlace, per state). The append may then write into
        // the state's own Block (Array::canWriteInPlace) instead of copying the array; when
        // the array is not compact (an element that holds references) or the block is shared,
        // it behaves exactly as `append`. Not a name a program can reach for on its own.
        OperatorGroup::create(
            "__append_owned__",
            {
                {
                    ":struct/append_arr_owned",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (arr: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &eleType = arrType->elemType();
                            if (eleType != Type::Void() && !eleType->assignableFrom(with[0]))
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
                            Type *elem          = arrType->elemType();
                            // The value is a value of the element type (design principles 5.2):
                            // an unnamed struct value whose fields are the element type's
                            // fields, in the same order, is read as that type without moving
                            // anything. A value of another named type is not (an `as` says it);
                            // nor is one whose fields are written in another order, which the
                            // call would have to copy and this signature cannot ask for.
                            if (!elem->assignableFrom(with[0]))
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
                            // The accumulator's parameter type is the result type, and the
                            // elements, the initial value and the callback's result must all be
                            // values of it. An unnamed struct value whose fields are that
                            // type's fields in the same order is one (design principles 5.2);
                            // the call converts it, as `enterStructParameters` does for the
                            // initial value.
                            Type *acc = funcType->normTypeAt(0);
                            if (!funcType->normTypeAt(1)->assignableFrom(arrType->elemType()))
                                return nullopt;
                            if (!acc->assignableFrom(with[1]))
                                return nullopt;
                            if (!acc->assignableFrom(funcType->exitType()))
                                return nullopt;
                            return acc;
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
        // ======= Diagnostics =======
        // `std::mem::blocks()`: the block counters as a struct, readable between two
        // operations (docs/design-principles 10.5; the allocation-count assertions). Declared
        // with effects below: the counters are not a function of the arguments.
        OperatorGroup::create(
            "std::mem::blocks",
            {{":op/mem_blocks", StaticFuncTypeResolver::create({}, {}, memBlocksStructType())}}),
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
        resolvers.insert(
            resolvers.end(),
            (*extra)->resolvers().begin(),
            (*extra)->resolvers().end());
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
    // The lowering targets of the enum (design principles 5.3).
    for (const auto &group : getEnumOperatorGroups()) {
        exportEntity(group->name(), group);
    }
    // The `set` operators a path assignment lowers to (design principles 3.5).
    for (const auto &group : getSetValueOperatorGroups()) {
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
