// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "model.h"

#include "camel/core/type/intern.h"
#include "camel/utils/assert.h"

#include <cstring>
#include <ostream>

namespace camel::onnx {

using namespace camel::core::type;
using namespace camel::core::rtdata;

// ---- the type ----

ModelType::ModelType(TypeCode code, Type *source)
    : OtherType(
          code, source == nullptr ? 0 : 1,
          source == nullptr
              ? nullptr
              : OtherType::copyParams(std::span<Type *const>(&source, std::size_t(1)))) {}

TypeCode ModelType::typeCode() {
    static TypeCode code = registerOtherType("Model", TypeFlag::GC_Traced);
    return code;
}

ModelType *ModelType::get() {
    static ModelType *bare = [] {
        const TypeCode code = typeCode();
        return static_cast<ModelType *>(
            TypeTable::instance().internOther(code, std::span<const uintptr_t>(), [&]() -> Type * {
                void *mem = mm::permSpace().alloc(sizeof(ModelType), alignof(ModelType));
                ASSERT(mem != nullptr, "Failed to allocate ModelType from permSpace");
                return new (mem) ModelType(code, nullptr);
            }));
    }();
    return bare;
}

ModelType *ModelType::of(Type *source) {
    ModelType *bare = get();
    if (source == nullptr) {
        return bare;
    }
    const uintptr_t parts[1] = {typeKeyWord(TypeTable::instance().canonical(source))};
    return static_cast<ModelType *>(TypeTable::instance().internOther(
        bare->code(),
        std::span<const uintptr_t>(parts, 1),
        [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(ModelType), alignof(ModelType));
            ASSERT(mem != nullptr, "Failed to allocate ModelType from permSpace");
            return new (mem) ModelType(bare->code(), source);
        }));
}

std::string ModelType::toString() const {
    Type *source = sourceType();
    return source == nullptr ? std::string("Model") : "Model<" + source->toString() + ">";
}

std::string ModelType::mangle() const {
    Type *source = sourceType();
    return source == nullptr ? std::string("onnx.Model") : "onnx.Model_" + source->mangle();
}

Type *ModelType::clone(bool deep) const {
    (void)deep;
    return paramCount_ == 1 ? of(params_[0]) : of(nullptr);
}

bool ModelType::equals(Type *type) const {
    const ModelType *other = isModelType(type) ? static_cast<const ModelType *>(type) : nullptr;
    if (other == nullptr || other->paramCount() != paramCount()) {
        return false;
    }
    Type *mine   = sourceType();
    Type *theirs = other->sourceType();
    return (mine == nullptr) == (theirs == nullptr) && (mine == nullptr || mine->equals(theirs));
}

CastSafety ModelType::castSafetyFrom(Type *sourceType) const {
    // Nothing converts into a Model: `onnx::model` makes one.
    return isModelType(sourceType) && equals(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool ModelType::assignableFrom(Type *sourceType) const {
    return isModelType(sourceType) && equals(sourceType);
}

OtherType *ModelType::cloneWithParams(std::span<Type *const> params) const {
    return of(params.empty() ? nullptr : params[0]);
}

bool isModelType(const Type *type) {
    return type != nullptr && type->code() == ModelType::typeCode();
}

// ---- the object ----

ModelValue *ModelValue::create(std::span<const std::byte> bytes, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(ModelValue), alignof(ModelValue));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    // The bytes are copied into the object's own buffer: nothing the export held is borrowed.
    return new (mem) ModelValue(bytes);
}

bool ModelValue::equals(const Object *other, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    const auto *that = dynamic_cast<const ModelValue *>(other);
    return that != nullptr && that->bytes_.size() == bytes_.size() &&
           std::memcmp(that->bytes_.data(), bytes_.data(), bytes_.size()) == 0;
}

Object *ModelValue::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    return create(bytes(), allocator);
}

void ModelValue::print(std::ostream &os, const Type *type) const {
    (void)type;
    os << "<model " << bytes_.size() << " bytes>";
}

void ModelValue::updateRefs(const Object::RefRelocator &relocate, const Type *type) {
    // A model holds no reference: its bytes are its own. Nothing to relocate.
    (void)relocate;
    (void)type;
}

} // namespace camel::onnx