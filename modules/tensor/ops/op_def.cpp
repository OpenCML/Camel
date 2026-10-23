// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * InferContext queries and signature rendering for OpDef.
 */

#include "op_def.h"

#include "../dtype.h"

namespace camel::tensor::ops {

using namespace camel::core::type;

TensorFacts InferContext::facts(size_t index) const { return tensorFactsOf(types_[index]); }

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
    // An array written in place whose elements are all constants: `[dim(x, 1), 3]` when the
    // type of x fixes the dimension.
    if (index < partials_.size() && partials_[index]) {
        std::vector<int64_t> values;
        for (const auto &element : *partials_[index]) {
            if (!element) {
                return std::nullopt;
            }
            values.push_back(*element);
        }
        return values;
    }
    return std::nullopt;
}

std::optional<PartialInts> InferContext::partialInts(size_t index) const {
    if (auto values = constInts(index)) {
        return PartialInts(values->begin(), values->end());
    }
    if (index < partials_.size() && partials_[index]) {
        return partials_[index];
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
    case ParamKind::Bytes:
        return "Bytes";
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
