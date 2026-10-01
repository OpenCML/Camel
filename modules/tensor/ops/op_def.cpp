/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * InferContext queries and signature rendering for OpDef.
 */

#include "op_def.h"

#include "../dtype.h"
#include "camel/core/type/composite/array.h"

namespace camel::tensor::ops {

using namespace camel::core::type;

TensorFacts InferContext::facts(size_t index) const {
    Type *argType = types_[index];
    if (const TensorType *tensor = asTensorType(argType)) {
        return {tensor->dtype(), tensor->shape()};
    }
    if (argType->code() == TypeCode::Array) {
        // Nested arrays: the innermost element type decides the dtype.
        Type *elem  = static_cast<ArrayType *>(argType)->elemType();
        size_t rank = 1;
        while (elem->code() == TypeCode::Array) {
            elem = static_cast<ArrayType *>(elem)->elemType();
            ++rank;
        }
        TensorFacts facts;
        if (isSupportedTensorScalar(elem->code())) {
            facts.dtype = normalizeTensorDType(elem->code());
        }
        facts.shape = StaticShape(rank, kUnknownDim);
        return facts;
    }
    if (isSupportedTensorScalar(argType->code())) {
        return {normalizeTensorDType(argType->code()), StaticShape{}};
    }
    return {};
}

const ConstArg *InferContext::constant(size_t index) const {
    if (index >= constants_.size() || !constants_[index]) {
        return nullptr;
    }
    return &*constants_[index];
}

std::optional<int64_t> InferContext::constInt(size_t index) const {
    if (const ConstArg *value = constant(index)) {
        if (auto *v = std::get_if<int64_t>(value)) {
            return *v;
        }
    }
    return std::nullopt;
}

std::optional<double> InferContext::constNumber(size_t index) const {
    if (const ConstArg *value = constant(index)) {
        if (auto *v = std::get_if<double>(value)) {
            return *v;
        }
        if (auto *v = std::get_if<int64_t>(value)) {
            return static_cast<double>(*v);
        }
    }
    return std::nullopt;
}

std::optional<bool> InferContext::constBool(size_t index) const {
    if (const ConstArg *value = constant(index)) {
        if (auto *v = std::get_if<bool>(value)) {
            return *v;
        }
    }
    return std::nullopt;
}

std::optional<std::string> InferContext::constString(size_t index) const {
    if (const ConstArg *value = constant(index)) {
        if (auto *v = std::get_if<std::string>(value)) {
            return *v;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<int64_t>> InferContext::constInts(size_t index) const {
    if (const ConstArg *value = constant(index)) {
        if (auto *v = std::get_if<std::vector<int64_t>>(value)) {
            return *v;
        }
    }
    return std::nullopt;
}

namespace {

std::string_view kindText(ParamKind kind) {
    switch (kind) {
    case ParamKind::Tensor:
        return "Tensor";
    case ParamKind::TensorLike:
        return "Tensor | number[]";
    case ParamKind::TensorOrScalar:
        return "Tensor | number";
    case ParamKind::TensorLikeOrScalar:
        return "Tensor | number[] | number";
    case ParamKind::Int:
        return "int";
    case ParamKind::Number:
        return "number";
    case ParamKind::Bool:
        return "bool";
    case ParamKind::String:
        return "string";
    case ParamKind::IntArray:
        return "int[]";
    }
    return "?";
}

} // namespace

std::string OpDef::signature() const {
    std::string text = "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += params[i].name;
        text += params[i].optional ? "?: " : ": ";
        text += kindText(params[i].kind);
    }
    text += ") => ";
    text += resultDoc;
    return text;
}

} // namespace camel::tensor::ops
