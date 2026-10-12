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
 * Operators over the tagged forms `T?` and `T!`; see tagged.h.
 *
 * None of them faults. The three that take a part out of a value are only ever placed in
 * the branch arm their tag selects; applied to the other case they return the zero slot, so
 * a pass that evaluates one early changes nothing as long as its result is used in that arm.
 */

#include "camel/utils/number_text.h"
#include "tagged.h"
#include "camel/core/context/context.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"

#include "camel/core/error/runtime.h"
#include "camel/execute/fault_isolation.h"

#include <charconv>
#include <optional>
#include <sstream>
#include <string>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

slot_t __is_null__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(isNullValue(norm.slot(0)));
}

slot_t __null_value__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return nullablePayload(norm.slot(0));
}

slot_t __is_error__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(isErrorValue(norm.slot(0)));
}

slot_t __error_value__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return falliblePayload(norm.slot(0));
}

slot_t __error_info__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return fallibleError(norm.slot(0));
}

slot_t __throw__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return makeFailed(norm.slot(0));
}

slot_t __error_new__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const auto text = [&](size_t index) {
        String *str = index < norm.size() ? norm.get<String *>(index) : nullptr;
        return str ? str->toString() : std::string();
    };
    return makeErrorInfo(text(0), text(1), text(2));
}

namespace {

/// `parse_int` and `parse_float` read a number under the rule of the conversion functions
/// (design principles 2.10): the whole string must be one number, without blanks around it.
/// What is a fault there is an error value here.
template <typename T>
slot_t parseNumber(ArgsView &norm, const TupleType *resultType, const char *what, const char *op) {
    String *str           = norm.get<String *>(0);
    const std::string raw = str ? str->toString() : std::string();
    T value{};
    camel::utils::NumberTextStatus status;
    if constexpr (std::is_floating_point_v<T>) {
        status = camel::utils::parseWholeFloat<T>(raw, value);
    } else {
        status = camel::utils::parseWholeInteger<T>(raw, value);
    }
    switch (status) {
    case camel::utils::NumberTextStatus::Ok:
        return makeOk(toSlot<T>(value), resultType);
    case camel::utils::NumberTextStatus::OutOfRange:
        return makeError("ParseError", "'" + raw + "' is out of the range of " + what, op);
    default:
        return makeError("ParseError", "'" + raw + "' is not " + what, op);
    }
}

} // namespace

slot_t __parse_int__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::Int64());
    return parseNumber<Int64>(norm, resultType, "an integer", "<parse_int>");
}

slot_t __parse_float__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::Float64());
    return parseNumber<Float64>(norm, resultType, "a number", "<parse_float>");
}

const std::vector<oper_group_ptr_t> &getTaggedOperatorGroups() {
    using std::nullopt;
    using std::optional;
    // One argument of a tagged form; `result` gives the type taken out of it.
    const auto opening = [](const char *signature, TupleType::Tag tag, auto result) {
        return DynamicFuncTypeResolver::create(
            {{0, {}}, {1, {false}}},
            signature,
            [tag, result](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                -> optional<Type *> {
                TupleType *tagged = asTagged(norm[0]);
                if (tagged == nullptr || tagged->tag() != tag) {
                    return nullopt;
                }
                return result(tagged);
            });
    };
    static const std::vector<oper_group_ptr_t> groups = {
        // ======= Lowering targets of ??, !!, ?->, throw and the match patterns =======
        OperatorGroup::create(
            "__is_null__",
            {{":op/tag_is_null",
              opening("(value: T?) => bool", TupleType::Tag::Nullable, [](TupleType *) {
                  return Type::Bool();
              })}}),
        OperatorGroup::create(
            "__null_value__",
            {{":op/tag_null_value",
              opening("(value: T?) => T", TupleType::Tag::Nullable, [](TupleType *type) {
                  return type->payloadType();
              })}}),
        OperatorGroup::create(
            "__is_error__",
            {{":op/tag_is_error",
              opening("(value: T!) => bool", TupleType::Tag::Fallible, [](TupleType *) {
                  return Type::Bool();
              })}}),
        OperatorGroup::create(
            "__error_value__",
            {{":op/tag_error_value",
              opening("(value: T!) => T", TupleType::Tag::Fallible, [](TupleType *type) {
                  return type->payloadType();
              })}}),
        OperatorGroup::create(
            "__error_info__",
            {{":op/tag_error_info",
              opening("(value: T!) => Error", TupleType::Tag::Fallible, [](TupleType *) -> Type * {
                  return errorType();
              })}}),
        OperatorGroup::create(
            "__throw__",
            {{":op/tag_throw",
              StaticFuncTypeResolver::create(
                  {},
                  {{errorType(), false}},
                  TupleType::fallible(Type::Void()))}}),
        // ======= Library =======
        // error(kind, message): the compiler appends the position of the call as a third
        // argument (GIR builder, visitLinkNode).
        OperatorGroup::create(
            "error",
            {{":op/error_new",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {-1, {}}},
                  "(kind: string, message: string) => Error",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> optional<Type *> {
                      if (norm.size() != 2 && norm.size() != 3) {
                          return nullopt;
                      }
                      for (Type *type : norm) {
                          if (type->code() != TypeCode::String) {
                              return nullopt;
                          }
                      }
                      return errorType();
                  })}}),
        // Fault isolation: a marked call of the function value given first. The schedulers
        // run it themselves (":mark/..."); see camel/execute/fault_isolation.h.
        OperatorGroup::create(
            camel::execute::kFaultIsolationName,
            {{std::string(camel::execute::kFaultIsolationUri),
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {-1, {}}},
                  "(f: (...args: A) => T, ...args: A) => T!",
                  [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                      -> optional<Type *> {
                      return camel::execute::resolveFaultIsolationType(with, norm);
                  })}}),
        OperatorGroup::create(
            "parse_int",
            {{":op/parse_int",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}},
                  TupleType::fallible(Type::Int64()))}}),
        OperatorGroup::create(
            "parse_float",
            {{":op/parse_float",
              StaticFuncTypeResolver::create(
                  {},
                  {{Type::String(), false}},
                  TupleType::fallible(Type::Float64()))}}),
    };
    return groups;
}
