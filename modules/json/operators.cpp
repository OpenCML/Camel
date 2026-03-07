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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */
#include <nlohmann/json.hpp>

#include <optional>

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "operators.h"

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

using json = nlohmann::json;

// 递归：将 Camel 值转为 JSON。失败时返回 nullopt 并报告诊断。
static std::optional<json> camelToJson(Type *t, slot_t s, Context &ctx) {
    if (!t) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<json.encode> null type");
    }
    if (t->code() == TypeCode::Any) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<json.encode> cannot encode value of type Any");
    }
    switch (t->code()) {
    case TypeCode::Int32:
        return json(static_cast<int64_t>(fromSlot<Int32>(s)));
    case TypeCode::Int64:
        return json(fromSlot<Int64>(s));
    case TypeCode::Float32:
        return json(fromSlot<Float32>(s));
    case TypeCode::Float64:
        return json(fromSlot<Float64>(s));
    case TypeCode::Bool:
        return json(fromSlot<Bool>(s));
    case TypeCode::Byte:
        return json(static_cast<int>(static_cast<unsigned>(fromSlot<Byte>(s))));
    case TypeCode::Void:
        return json(nullptr);
    case TypeCode::String: {
        String *str = fromSlot<String *>(s);
        return json(str ? str->toString() : "");
    }
    case TypeCode::Array: {
        Array *arr = fromSlot<Array *>(s);
        if (!arr)
            return json::array();
        const ArrayType *arrayType = static_cast<const ArrayType *>(t);
        Type *elemType             = arrayType->elemType();
        if (!elemType)
            elemType = Type::Any();
        json jarr = json::array();
        for (size_t i = 0; i < arr->size(); ++i) {
            auto elem = camelToJson(elemType, arr->data()[i], ctx);
            if (!elem)
                return std::nullopt;
            jarr.push_back(std::move(*elem));
        }
        return jarr;
    }
    case TypeCode::Tuple: {
        Tuple *tuple = fromSlot<Tuple *>(s);
        if (!tuple)
            return json::array();
        const TupleType *tupleType = static_cast<const TupleType *>(t);
        json jarr                  = json::array();
        for (size_t i = 0; i < tupleType->size(); ++i) {
            Type *elemType = tupleType->typeAt(i);
            if (!elemType)
                elemType = Type::Any();
            auto elem = camelToJson(elemType, tuple->data()[i], ctx);
            if (!elem)
                return std::nullopt;
            jarr.push_back(std::move(*elem));
        }
        return jarr;
    }
    case TypeCode::Struct: {
        Struct *st = fromSlot<Struct *>(s);
        if (!st)
            return json::object();
        const StructType *structType = static_cast<const StructType *>(t);
        json jobj;
        for (size_t i = 0; i < structType->size(); ++i) {
            Type *fieldType = structType->typeAt(i);
            if (!fieldType)
                fieldType = Type::Any();
            auto val = camelToJson(fieldType, st->data()[i], ctx);
            if (!val)
                return std::nullopt;
            jobj[std::string(structType->fieldName(i))] = std::move(*val);
        }
        return jobj;
    }
    default:
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<json.encode> unsupported type: " + t->toString());
    }
}

// 递归：将 JSON 转为 Camel slot（参考 pyToCamel）
static slot_t jsonToCamel(const json &j, Context &ctx) {
    if (j.is_null())
        return toSlot(0); // Void-like
    if (j.is_boolean())
        return toSlot(j.get<bool>());
    if (j.is_number_integer()) {
        try {
            return toSlot(j.get<Int64>());
        } catch (const json::exception &) {
            return toSlot(j.get<Float64>()); // 大整数退化为 float
        }
    }
    if (j.is_number_float())
        return toSlot(j.get<Float64>());
    if (j.is_string()) {
        std::string str = j.get<std::string>();
        return toSlot(String::from(str, mm::autoSpace()));
    }
    if (j.is_array()) {
        size_t n   = j.size();
        Array *arr = Array::create(mm::autoSpace(), n);
        for (size_t i = 0; i < n; ++i)
            arr->set(i, jsonToCamel(j[i], ctx));
        return toSlot(arr);
    }
    if (j.is_object()) {
        StructTypeFactory factory;
        std::vector<slot_t> slots;
        slots.reserve(j.size());
        for (auto &[k, v] : j.items()) {
            factory.add(k, Type::Any());
            slots.push_back(jsonToCamel(v, ctx));
        }
        StructType *structType = StructType::fromFactory(factory);
        size_t n               = structType->size();
        Struct *st             = Struct::create(n, mm::autoSpace());
        for (size_t i = 0; i < n; ++i)
            st->set(i, slots[i]);
        return toSlot(st);
    }
    throwRuntimeFault(RuntimeDiag::RuntimeError, "<json.decode> unexpected JSON type");
}

} // namespace

slot_t __json_encode__(ArgsView &, ArgsView &norm, Context &ctx) {
    Type *t  = norm.type(0);
    slot_t s = norm.slot(0);
    auto opt = camelToJson(t, s, ctx);
    if (!opt)
        return NullSlot;
    int indent = norm.size() >= 2 ? static_cast<int>(fromSlot<Int64>(norm.slot(1))) : -1;
    if (indent < 0)
        indent = -1;
    std::string out = indent >= 0 ? opt->dump(indent, ' ') : opt->dump();
    return toSlot(String::from(out, mm::autoSpace()));
}

slot_t __json_decode__(ArgsView &, ArgsView &norm, Context &ctx) {
    String *str = norm.get<String *>(0);
    if (!str)
        return NullSlot;
    try {
        json j = json::parse(str->c_str());
        return jsonToCamel(j, ctx);
    } catch (const json::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("<json.decode> ") + e.what());
    }
}

std::unordered_map<std::string, operator_t> getJsonOpsMap() {
    return {{"encode", __json_encode__}, {"decode", __json_decode__}};
}
