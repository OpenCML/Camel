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
 * ONNX graph construction and value helpers for the exporter (see emitter.h).
 */

#include "emitter.h"

#include "capability.h"

#include "../tensor/dtype.h"
#include "../tensor/interop.h"
#include "../tensor/ops/support.h"
#include "../tensor/tensor.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/array.h"

#include <cstring>
#include <format>
#include <unordered_set>

namespace camel::onnx {

using tensor::TensorObject;
using tensor::ops::ConstArg;
using tensor::ops::TensorFacts;
using type::TypeCode;

namespace {

bool isScalarCode(TypeCode code) {
    return code == TypeCode::Int32 || code == TypeCode::Int64 || code == TypeCode::Float32 ||
           code == TypeCode::Float64 || code == TypeCode::Bool;
}

const TensorObject *constantTensor(const Value &value) {
    if (!value.isConstant() || !tensor::asTensorType(value.ty)) {
        return nullptr;
    }
    return rtdata::fromSlot<TensorObject *>(value.slot);
}

/// Encodes `count` elements read through `read(i)` into raw little-endian bytes of `dtype`.
template <typename Read>
std::vector<std::byte> encodeElements(TypeCode dtype, uint64_t count, Read &&read) {
    std::vector<std::byte> raw(count * tensor::elementSize(dtype));
    for (uint64_t i = 0; i < count; ++i) {
        const double v = read(i);
        switch (dtype) {
        case TypeCode::Float32: {
            const float f = static_cast<float>(v);
            std::memcpy(raw.data() + i * sizeof(float), &f, sizeof(float));
        } break;
        case TypeCode::Int64: {
            const int64_t n = static_cast<int64_t>(v);
            std::memcpy(raw.data() + i * sizeof(int64_t), &n, sizeof(int64_t));
        } break;
        default:
            raw[i] = std::byte{v != 0.0 ? uint8_t{1} : uint8_t{0}};
            break;
        }
    }
    return raw;
}

std::string sanitize(std::string_view hint) {
    std::string out;
    for (char c : hint) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_');
    }
    return out.empty() ? std::string("v") : out;
}

/// Identity of a node for structural CSE, or nullopt for nodes that are never merged (those
/// with tensor or subgraph attributes, whose comparison is not worth it).
std::optional<std::string> structuralKey(
    const std::string &opType, const std::vector<std::string> &inputs,
    const std::vector<Attribute> &attributes) {
    std::string key = opType + "(";
    for (const std::string &in : inputs) {
        key += in + ",";
    }
    key += ")";
    for (const Attribute &attr : attributes) {
        key += "|" + attr.name + "=";
        bool mergeable = true;
        std::visit(
            [&](const auto &v) {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, float> || std::is_same_v<V, int64_t>) {
                    key += std::format("{}", v);
                } else if constexpr (std::is_same_v<V, std::string>) {
                    key += v;
                } else if constexpr (
                    std::is_same_v<V, std::vector<float>> ||
                    std::is_same_v<V, std::vector<int64_t>> ||
                    std::is_same_v<V, std::vector<std::string>>) {
                    for (const auto &e : v) {
                        key += std::format("{};", e);
                    }
                } else {
                    mergeable = false;
                }
            },
            attr.value);
        if (!mergeable) {
            return std::nullopt;
        }
    }
    return key;
}

} // namespace

ElemType elemTypeOf(TypeCode dtype) {
    switch (tensor::normalizeTensorDType(dtype)) {
    case TypeCode::Float32:
        return ElemType::Float;
    case TypeCode::Int64:
        return ElemType::Int64;
    case TypeCode::Bool:
        return ElemType::Bool;
    default:
        throw ExportError("unsupported tensor dtype");
    }
}

Value Value::symbolicScalar(std::string name, type::Type *camelType) {
    Value v;
    v.kind      = Kind::Symbolic;
    v.form      = Form::Scalar;
    v.name      = std::move(name);
    v.camelType = camelType;
    v.dtype     = tensor::normalizeTensorDType(camelType->code());
    v.shape     = tensor::StaticShape{};
    return v;
}

TensorFacts factsOf(const Value &value) {
    if (value.isAggregate()) {
        return {};
    }
    if (value.isSymbolic()) {
        return {value.dtype, value.shape};
    }
    if (const TensorObject *t = constantTensor(value)) {
        return {t->dtype(), tensor::StaticShape(t->shapeSpan().begin(), t->shapeSpan().end())};
    }
    if (value.ty && isScalarCode(value.ty->code())) {
        return {tensor::normalizeTensorDType(value.ty->code()), tensor::StaticShape{}};
    }
    return {};
}

type::Type *inferenceTypeOf(const Value &value) {
    if (value.isSymbolic() &&
        (value.form == Value::Form::Scalar || value.form == Value::Form::Aggregate)) {
        return value.camelType;
    }
    if (value.isSymbolic() && value.form == Value::Form::IntArray) {
        static type::ArrayType *intArray = type::ArrayType::create(type::Type::Int64());
        return intArray;
    }
    if (value.isSymbolic() || constantTensor(value)) {
        const TensorFacts facts = factsOf(value);
        return tensor::TensorType::get(facts.dtype, facts.shape);
    }
    return value.ty;
}

std::optional<ConstArg> constArgOf(const Value &value) {
    if (!value.isConstant()) {
        return std::nullopt;
    }
    return tensor::ops::constArgOf(value.slot, value.ty);
}

Emitter::Emitter(int64_t opset, const CapabilityTable *capabilities)
    : capabilities_(capabilities), opset_(opset) {
    scopes_.emplace_back();
}

void Emitter::addInput(ValueInfo info) {
    elemTypes_[info.name] = info.type;
    graph().inputs.push_back(std::move(info));
}

std::optional<ElemType> Emitter::valueElemType(const std::string &name) const {
    auto it = elemTypes_.find(name);
    return it == elemTypes_.end() ? std::nullopt : std::optional(it->second);
}

std::optional<ElemType> Emitter::outputElemType(
    const std::string &opType, const std::vector<std::string> &inputs,
    const std::vector<Attribute> &attributes) const {
    static const std::unordered_set<std::string> kBoolResults = {
        "Equal", "Greater", "GreaterOrEqual", "Less", "LessOrEqual", "And", "Or", "Not"};
    if (kBoolResults.contains(opType)) {
        return ElemType::Bool;
    }
    if (opType == "Shape" || opType == "ArgMax") {
        return ElemType::Int64;
    }
    if (opType == "Cast") {
        for (const Attribute &a : attributes) {
            if (a.name == "to") {
                return static_cast<ElemType>(std::get<int64_t>(a.value));
            }
        }
        return std::nullopt;
    }
    if (opType == "ConstantOfShape") {
        for (const Attribute &a : attributes) {
            if (a.name == "value") {
                return std::get<TensorValue>(a.value).type;
            }
        }
        return ElemType::Float;
    }
    if (opType == "If") {
        return std::nullopt;
    }
    // Every other emitted operator keeps the element type of its data input.
    const size_t data = opType == "Where" ? 1 : 0;
    return data < inputs.size() ? valueElemType(inputs[data]) : std::nullopt;
}

void Emitter::checkCapability(
    const std::string &opType, const std::vector<std::string> &inputs) const {
    if (!capabilities_) {
        return;
    }
    const size_t data = opType == "Where" ? 1 : 0;
    const auto elem   = data < inputs.size() ? valueElemType(inputs[data]) : std::nullopt;
    if (!elem) {
        return;
    }
    if (auto reason = capabilities_->rejection(opType, *elem, opset_)) {
        throw ExportError(*reason);
    }
}

std::string Emitter::fresh(std::string_view hint) {
    const std::string base = sanitize(hint);
    std::string name       = base;
    while (usedNames_.contains(name)) {
        name = std::format("{}_{}", base, nameCounts_[base]++);
    }
    usedNames_.insert(name);
    return name;
}

std::string Emitter::node(
    std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes,
    std::string_view hint) {
    const std::optional<std::string> key = structuralKey(opType, inputs, attributes);
    if (key) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            if (auto found = it->nodes.find(*key); found != it->nodes.end()) {
                return found->second;
            }
        }
    }
    checkCapability(opType, inputs);
    std::string output = fresh(hint.empty() ? std::string_view(opType) : hint);
    if (key) {
        scopes_.back().nodes.emplace(*key, output);
    }
    if (auto elem = outputElemType(opType, inputs, attributes)) {
        elemTypes_[output] = *elem;
    }
    Node n;
    n.opType     = std::move(opType);
    n.name       = "n_" + output;
    n.inputs     = std::move(inputs);
    n.outputs    = {output};
    n.attributes = std::move(attributes);
    scopes_.back().graph.nodes.push_back(std::move(n));
    return output;
}

std::string Emitter::operand(const Value &value, std::optional<TypeCode> dtype) {
    if (value.isConstant()) {
        return constantOperand(value, dtype);
    }
    if (value.isAggregate()) {
        throw ExportError(std::format(
            "a '{}' that depends on the model input is used where a tensor is expected",
            value.camelType->toString()));
    }
    if (!dtype || !value.dtype || tensor::normalizeTensorDType(*dtype) == *value.dtype) {
        return value.name;
    }
    const TypeCode target = tensor::normalizeTensorDType(*dtype);
    // A cast is valid in the scope that emitted it and in scopes nested inside it.
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (auto found = it->casts.find({value.name, target}); found != it->casts.end()) {
            return found->second;
        }
    }
    std::string cast = node(
        "Cast",
        {value.name},
        {Attribute::makeInt("to", static_cast<int64_t>(elemTypeOf(target)))},
        value.name + "_cast");
    scopes_.back().casts[{value.name, target}] = cast;
    return cast;
}

std::string Emitter::constantOperand(const Value &value, std::optional<TypeCode> dtype) {
    const TensorFacts facts = factsOf(value);
    if (!facts.dtype || !facts.shape) {
        throw ExportError(std::format(
            "constant of type '{}' cannot be used as a tensor operand",
            value.ty ? value.ty->toString() : "?"));
    }
    const TypeCode target  = dtype ? tensor::normalizeTensorDType(*dtype) : *facts.dtype;
    const std::string hint = value.label.empty() ? "const" : value.label;

    if (const TensorObject *t = constantTensor(value)) {
        const std::string key =
            std::format("t:{}:{}", static_cast<const void *>(t), static_cast<uint32_t>(target));
        if (auto it = initializerByKey_.find(key); it != initializerByKey_.end()) {
            return it->second;
        }
        TensorValue tv;
        tv.type = elemTypeOf(target);
        tv.dims = *facts.shape;
        if (t->dtype() == target) {
            tv.raw.assign(t->rawData(), t->rawData() + t->byteSize());
        } else {
            tv.raw =
                encodeElements(target, t->numel(), [&](uint64_t i) { return t->getAsDouble(i); });
        }
        tv.name = fresh(hint);
        return addInitializer(std::move(tv), key);
    }

    if (value.ty->code() == TypeCode::Array) {
        // Numeric (nested) arrays become tensors of their inferred dtype and shape.
        TensorObject *t =
            tensor::tensorFromArray(value.slot, value.ty, tensor::ops::resultAllocator());
        retain(t, tensor::TensorType::Default());
        Value asTensor = Value::constant(rtdata::toSlot(t), tensor::TensorType::Default());
        asTensor.label = value.label;
        return constantOperand(asTensor, dtype);
    }

    // Numeric scalar: a rank-0 tensor.
    const double scalar   = tensor::scalarToDouble(value.ty->code(), value.slot);
    const std::string key = std::format("s:{}:{}", static_cast<uint32_t>(target), scalar);
    if (auto it = initializerByKey_.find(key); it != initializerByKey_.end()) {
        return it->second;
    }
    TensorValue tv;
    tv.type = elemTypeOf(target);
    tv.raw  = encodeElements(target, 1, [&](uint64_t) { return scalar; });
    tv.name = fresh(hint);
    return addInitializer(std::move(tv), key);
}

std::string Emitter::int64s(std::span<const int64_t> values) {
    std::string key = "i:";
    for (int64_t v : values) {
        key += std::to_string(v) + ",";
    }
    if (auto it = initializerByKey_.find(key); it != initializerByKey_.end()) {
        return it->second;
    }
    return addInitializer(
        makeTensor(fresh("ints"), {static_cast<int64_t>(values.size())}, values),
        key);
}

std::string Emitter::addInitializer(TensorValue tensor, const std::string &key) {
    std::string name  = tensor.name;
    elemTypes_[name] = tensor.type;
    graph().initializers.push_back(std::move(tensor));
    initializerByKey_.emplace(key, name);
    return name;
}

void Emitter::retain(rtdata::Object *object, const type::Type *type) {
    if (object) {
        roots_.emplace_back(core::mm::autoSpace(), object, type, "onnx.export_model");
    }
}

void Emitter::pushScope(std::string name) {
    scopes_.emplace_back();
    scopes_.back().graph.name = std::move(name);
}

Graph Emitter::popScope() {
    Graph g = std::move(scopes_.back().graph);
    scopes_.pop_back();
    return g;
}

} // namespace camel::onnx
