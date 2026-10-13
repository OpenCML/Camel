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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The string library (design principles 2.9: a string is stored as bytes, its length and
 * indices count bytes, and separate operators read and write it by code point).
 *
 * Every operator is a primitive written in C++ and is pure. The string an operator works on is
 * its one ordinary argument and everything else is a `with` argument, as for the built-in
 * `join<sep>(xs)` and `slice<start, end>(xs)`, so calls chain: `s->trim->split<','>`.
 * DESIGN-QUESTION(param-style): the alternative is ordinary arguments, `split(s, ',')`.
 */

#include "module.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace {

using param_list_t = std::initializer_list<std::pair<Type *, bool>>;

oper_group_ptr_t
group(const char *name, const param_list_t &with, const param_list_t &norm, Type *result) {
    return OperatorGroup::create(
        name,
        {{std::string("str:") + name, StaticFuncTypeResolver::create(with, norm, result)}});
}

const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    Type *const S         = Type::String();
    Type *const I         = Type::Int64();
    Type *const B         = Type::Bool();
    Type *const strings   = ArrayType::create(S);
    Type *const ints      = ArrayType::create(I);
    const param_list_t s  = {{S, false}};
    const param_list_t i  = {{I, false}};
    const param_list_t ii = {{I, false}, {I, false}};
    const param_list_t ss = {{S, false}, {S, false}};
    const param_list_t xs = {{ints, false}};
    static const std::vector<oper_group_ptr_t> groups = {
        // ---- by byte ----
        group("split", s, s, strings),
        group("trim", {}, s, S),
        group("trim_start", {}, s, S),
        group("trim_end", {}, s, S),
        group("to_upper", {}, s, S),
        group("to_lower", {}, s, S),
        group("replace", ss, s, S),
        group("find", s, s, TupleType::nullable(I)),
        group("substr", ii, s, S),
        group("starts_with", s, s, B),
        group("ends_with", s, s, B),
        group("repeat", i, s, S),
        group("bytes", {}, s, ints),
        group("from_bytes", {}, xs, S),
        // ---- by code point (UTF-8) ----
        group("is_valid_utf8", {}, s, B),
        group("codepoints", {}, s, TupleType::fallible(ints)),
        group("from_codepoints", {}, xs, TupleType::fallible(S)),
        group("char_count", {}, s, TupleType::fallible(I)),
        group("char_at", i, s, TupleType::fallible(S)),
        group("char_substr", ii, s, TupleType::fallible(S)),
    };
    return groups;
}
} // namespace

StrModule::StrModule(context_ptr_t ctx) : BuiltinModule("str", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t StrModule::create(context_ptr_t ctx) { return std::make_shared<StrModule>(ctx); }

bool StrModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("str", [ctx = context_]() { return createStrExecutor(ctx); });
    // Every operator of this module is a function of its arguments.
    for (const auto &[name, op] : getStrOpsMap()) {
        camel::core::OperatorTable::instance().set(
            "str:" + name,
            {.pure = true, .elementwise = false});
    }
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new StrModule(ctx->shared_from_this()); }
}
