// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TensorType implementation: interning, printing, and the refinement lattice.
 */

#include "type.h"

#include "camel/core/data/composite/array.h"
#include "camel/core/data/primary.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"
#include "dtype.h"
#include "tensor.h"

#include <mutex>
#include <unordered_map>

namespace mm = camel::core::mm;

namespace camel::tensor {

using namespace camel::core::type;

namespace {

std::string dimText(int64_t extent) { return extent == kUnknownDim ? "?" : std::to_string(extent); }

std::string
internKey(const std::optional<TypeCode> &dtype, const std::optional<StaticShape> &shape) {
    std::string key = dtype ? std::string(dtypeName(*dtype)) : "?";
    if (shape) {
        key += "[";
        for (int64_t extent : *shape) {
            key += dimText(extent) + ",";
        }
        key += "]";
    }
    return key;
}

/// The intern key of the named extents: a type that states `[n, 3]` is not the type that states
/// `[?, 3]` (it keeps the name for its signature), even though the two are equal for every check
/// that is not about names.
std::string formsKey(const std::vector<ExtentForm> &forms) {
    std::string key = "|";
    for (const ExtentForm &form : forms) {
        key += (form.hasNames() ? form.toString() : std::string("-")) + ",";
    }
    return key;
}

/// Whether any extent names a dimension. The forms of a type that names none are not kept: the
/// shape says everything, and the intern key stays the one it always had.
bool hasNamedExtent(const std::vector<ExtentForm> &forms) {
    for (const ExtentForm &form : forms) {
        if (form.hasNames()) {
            return true;
        }
    }
    return false;
}

} // namespace

TensorType::TensorType(
    std::optional<TypeCode> dtype, std::optional<StaticShape> shape, std::vector<ExtentForm> forms)
    : OtherType(typeCode()), dtype_(dtype), shape_(std::move(shape)), forms_(std::move(forms)) {}

TypeCode TensorType::typeCode() {
    static TypeCode code = registerOtherType("Tensor", TypeFlag::GC_Traced);
    return code;
}

TensorType *TensorType::get(std::optional<TypeCode> dtype, std::optional<StaticShape> shape) {
    return get(std::move(dtype), std::move(shape), {});
}

TensorType *TensorType::get(
    std::optional<TypeCode> dtype, std::optional<StaticShape> shape,
    std::vector<ExtentForm> forms) {
    if (dtype && !isSupportedTensorScalar(*dtype)) {
        throw std::invalid_argument("Unsupported Tensor element type");
    }
    if (shape) {
        for (int64_t &extent : *shape) {
            if (extent < 0) {
                extent = kUnknownDim;
            }
        }
    }
    if (forms.size() != (shape ? shape->size() : 0) || !hasNamedExtent(forms)) {
        forms.clear();
    }
    static std::mutex mutex;
    static std::unordered_map<std::string, TensorType *> interned;
    const std::string key = internKey(dtype, shape) + formsKey(forms);
    std::lock_guard guard(mutex);
    if (auto it = interned.find(key); it != interned.end()) {
        return it->second;
    }
    void *mem = mm::permSpace().alloc(sizeof(TensorType), alignof(TensorType));
    ASSERT(mem != nullptr, "Failed to allocate TensorType from permSpace");
    auto *created = new (mem) TensorType(dtype, std::move(shape), std::move(forms));
    interned.emplace(key, created);
    return created;
}

TensorType *TensorType::Default() {
    static TensorType *instance = get(std::nullopt, std::nullopt);
    return instance;
}

bool TensorType::isStaticShape() const {
    if (!shape_) {
        return false;
    }
    for (int64_t extent : *shape_) {
        if (extent == kUnknownDim) {
            return false;
        }
    }
    return true;
}

std::string TensorType::toString() const {
    if (!dtype_ && !shape_) {
        return "Tensor";
    }
    std::string result = "Tensor<" + (dtype_ ? std::string(dtypeName(*dtype_)) : std::string("?"));
    if (shape_) {
        result += ", [";
        for (size_t i = 0; i < shape_->size(); ++i) {
            result += (i ? ", " : "");
            result += i < forms_.size() && forms_[i].hasNames() ? forms_[i].toString()
                                                                : dimText((*shape_)[i]);
        }
        result += "]";
    }
    return result + ">";
}

std::string TensorType::spelling() const {
    if (!dtype_ && !shape_) {
        return "Tensor";
    }
    std::string arguments;
    if (dtype_) {
        arguments = elementTypeSpelling(*dtype_);
    }
    if (shape_) {
        arguments += arguments.empty() ? "const [" : ", const [";
        for (size_t i = 0; i < shape_->size(); ++i) {
            arguments += (i ? ", " : "");
            if (i < forms_.size() && forms_[i].hasNames()) {
                arguments += forms_[i].toString();
            } else {
                arguments +=
                    (*shape_)[i] == kUnknownDim ? std::string("-1") : std::to_string((*shape_)[i]);
            }
        }
        arguments += "]";
    }
    return "Tensor<" + arguments + ">";
}

std::string TensorType::mangle() const { return "T" + internKey(dtype_, shape_) + ";"; }

Type *TensorType::clone(bool) const { return const_cast<TensorType *>(this); }

bool TensorType::equals(Type *other) const {
    // Named extents are compile-time metadata of the signature that writes them, like parameter
    // names: `Tensor<[n, 3]>` and `Tensor<[?, 3]>` are equal as tensor types. Two interned types
    // may therefore be equal without being the same object.
    if (other == nullptr || other->code() != typeCode()) {
        return false;
    }
    const auto *rhs = static_cast<const TensorType *>(other);
    return dtype_ == rhs->dtype_ && shape_ == rhs->shape_;
}

Type *TensorType::unify(Type *other) const {
    const TensorType *rhs = asTensorType(other);
    if (!rhs) {
        return nullptr;
    }
    std::optional<TypeCode> dtype = dtype_ == rhs->dtype_ ? dtype_ : std::nullopt;
    std::optional<StaticShape> shape;
    if (shape_ && rhs->shape_ && shape_->size() == rhs->shape_->size()) {
        shape = StaticShape(shape_->size());
        for (size_t i = 0; i < shape->size(); ++i) {
            (*shape)[i] = (*shape_)[i] == (*rhs->shape_)[i] ? (*shape_)[i] : kUnknownDim;
        }
    }
    return get(dtype, std::move(shape));
}

Type *TensorType::widened() const { return Default(); }

CastSafety TensorType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr) {
        return CastSafety::Forbidden;
    }
    if (assignableFrom(sourceType)) {
        return CastSafety::Safe;
    }
    // `t as Tensor<...>` is the checked way from a less precise tensor type to a more precise
    // one. What the two static types already contradict is refused at compile time; what the
    // source leaves open is checked when the conversion runs (castSlotFrom): Unsafe.
    const TensorType *rhs = asTensorType(sourceType);
    if (!rhs) {
        return CastSafety::Forbidden;
    }
    if (dtype_ && rhs->dtype_ && rhs->dtype_ != dtype_) {
        return CastSafety::Forbidden;
    }
    if (shape_ && rhs->shape_) {
        if (shape_->size() != rhs->shape_->size()) {
            return CastSafety::Forbidden;
        }
        for (size_t i = 0; i < shape_->size(); ++i) {
            const int64_t want = (*shape_)[i], have = (*rhs->shape_)[i];
            if (want != kUnknownDim && have != kUnknownDim && want != have) {
                return CastSafety::Forbidden;
            }
        }
    }
    return CastSafety::Unsafe;
}

slot_t TensorType::castSlotFrom(slot_t value, Type *sourceType) const {
    (void)sourceType;
    auto *tensor      = camel::core::rtdata::fromSlot<Array *>(value);
    const auto refuse = [&](const std::string &what) {
        camel::core::error::throwRuntimeFault(
            camel::core::error::RuntimeDiag::RuntimeError,
            "conversion to '" + toString() + "' failed: " + what);
    };
    if (tensor == nullptr) {
        refuse("the value is null");
    }
    if (dtype_ && tensor->dtype() != *dtype_) {
        refuse("the tensor has element type " + std::string(dtypeName(tensor->dtype())));
    }
    if (shape_) {
        std::string actual = "[";
        for (size_t i = 0; i < tensor->rank(); ++i) {
            actual += (i ? ", " : "") + std::to_string(tensor->dim(i));
        }
        actual += "]";
        if (tensor->rank() != shape_->size()) {
            refuse("the tensor has shape " + actual);
        }
        for (size_t i = 0; i < shape_->size(); ++i) {
            if ((*shape_)[i] != kUnknownDim && (*shape_)[i] != tensor->dim(i)) {
                refuse("the tensor has shape " + actual);
            }
        }
    }
    return value;
}

bool TensorType::assignableFrom(Type *sourceType) const {
    const TensorType *rhs = asTensorType(sourceType);
    if (!rhs) {
        return false;
    }
    if (dtype_ && rhs->dtype_ != dtype_) {
        return false;
    }
    if (!shape_) {
        return true;
    }
    if (!rhs->shape_ || rhs->shape_->size() != shape_->size()) {
        return false;
    }
    for (size_t i = 0; i < shape_->size(); ++i) {
        if ((*shape_)[i] != kUnknownDim && (*shape_)[i] != (*rhs->shape_)[i]) {
            return false;
        }
    }
    return true;
}

OtherType *TensorType::cloneWithParams(std::span<Type *const> params) const {
    // `Tensor<dtype>`: the first parameter names the element type.
    if (params.empty()) {
        return Default();
    }
    return get(params[0]->code(), std::nullopt);
}

OtherType *TensorType::cloneWithArguments(
    std::span<const type::TypeArgument> arguments, std::string &error) const {
    std::optional<TypeCode> dtype;
    std::optional<StaticShape> shape;
    std::vector<ExtentForm> forms;
    for (const type::TypeArgument &argument : arguments) {
        if (argument.type != nullptr) {
            if (dtype || shape) {
                error = "Tensor takes at most one element type, written before the shape: "
                        "Tensor<dtype, const [d0, d1, ...]>";
                return nullptr;
            }
            // The element type is a storage dtype of this module (dtype.h): exactly the type written,
            // f32, f64, bf16, f16, i32, i64 or bool. Anything else would reach the kernels,
            // which dispatch on these seven element types.
            const TypeCode code = argument.type->code();
            if (!isSupportedTensorScalar(code)) {
                error = "the element type is f32, f64, bf16, f16, i32, i64 or bool, not '" +
                        type::typeCodeToString(code) + "'";
                return nullptr;
            }
            // As cloneWithParams: the type's code names the element type, and the tensor stores
            // exactly that type (no normalization: dtype.h).
            dtype = code;
            continue;
        }
        if (shape) {
            error = "Tensor takes at most one shape: Tensor<dtype, const [d0, d1, ...]>";
            return nullptr;
        }
        if (argument.extents) {
            // A shape written over dimension names: each extent is a linear form, and the
            // non-constant ones are unknown in the shape (the name is the metadata).
            StaticShape extents;
            extents.reserve(argument.extents->size());
            forms.reserve(argument.extents->size());
            for (const ExtentForm &form : *argument.extents) {
                if (form.isConstant()) {
                    extents.push_back(form.constant() < 0 ? kUnknownDim : form.constant());
                } else {
                    extents.push_back(kUnknownDim);
                }
                forms.push_back(form);
            }
            shape = std::move(extents);
            continue;
        }
        const auto array = std::dynamic_pointer_cast<camel::core::data::ArrayData>(argument.data);
        if (!array) {
            error = "the constant argument of a Tensor type is its shape, an array of integers "
                    "such as const [3, 4] (a negative extent means unknown)";
            return nullptr;
        }
        StaticShape extents;
        for (const auto &element : array->raw()) {
            if (auto i = std::dynamic_pointer_cast<camel::core::data::IntData>(element)) {
                extents.push_back(i->data() < 0 ? kUnknownDim : static_cast<int64_t>(i->data()));
            } else if (auto l = std::dynamic_pointer_cast<camel::core::data::LongData>(element)) {
                extents.push_back(l->data() < 0 ? kUnknownDim : l->data());
            } else {
                error = "every extent of a Tensor shape must be an integer constant (a negative "
                        "extent means unknown)";
                return nullptr;
            }
        }
        shape = std::move(extents);
    }
    return get(dtype, std::move(shape), std::move(forms));
}

bool TensorType::extentForms(std::vector<ExtentForm> &out) const {
    if (!shape_) {
        return false;
    }
    out.clear();
    out.reserve(shape_->size());
    for (size_t i = 0; i < shape_->size(); ++i) {
        if (i < forms_.size() && forms_[i].hasNames()) {
            out.push_back(forms_[i]);
        } else if ((*shape_)[i] == kUnknownDim) {
            out.push_back(ExtentForm::unknown());
        } else {
            out.push_back(ExtentForm::constant((*shape_)[i]));
        }
    }
    return true;
}

const Type *TensorType::withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const {
    (void)packs; // a tensor shape has no trailing pack; only the array type binds one
    if (!shape_ || forms_.empty()) {
        return const_cast<TensorType *>(this);
    }
    StaticShape shape = *shape_;
    bool refined      = false;
    for (size_t i = 0; i < shape.size() && i < forms_.size(); ++i) {
        if (!forms_[i].hasNames()) {
            continue;
        }
        if (auto value = forms_[i].evaluate(values)) {
            shape[i] = *value;
            refined  = true;
        }
    }
    if (!refined) {
        return const_cast<TensorType *>(this);
    }
    // The refined type is concrete: the names have served their purpose at this call.
    return get(dtype_, std::move(shape));
}

const TensorType *asTensorType(const Type *type) {
    if (!type || type->code() != TensorType::typeCode()) {
        return nullptr;
    }
    return static_cast<const TensorType *>(type);
}

bool isTensorType(const Type *type) {
    if (asTensorType(type) != nullptr) {
        return true;
    }
    // While the alias is being introduced, the only array a tensor can be is the numeric-unknown
    // array it hands out (`Tensor`, `Tensor<f32>`, `Tensor<f32, [3]>`): an array a program writes
    // (`f32[]`, `f32[2, 3]`) is still an array. The flip replaces this line with
    // `isNumericArrayType(type)`.
    const ArrayType *array = asArrayType(type);
    return array != nullptr && array->isNumericUnknown();
}

TensorFacts tensorFactsOf(const Type *type) {
    if (type == nullptr) {
        return {};
    }
    if (const TensorType *tensor = asTensorType(type)) {
        return {tensor->dtype(), tensor->shape()};
    }
    const ArrayType *array = asArrayType(type);
    if (array == nullptr) {
        if (isSupportedTensorScalar(type->code())) {
            // A scalar argument: rank 0, the element type it has.
            return {type->code(), StaticShape{}};
        }
        return {};
    }
    if (array->isNumericUnknown()) {
        // `Tensor`: no element type is fixed. A shape argument fixes the rank and its extents;
        // a bare `Tensor` fixes neither.
        TensorFacts facts;
        if (!array->anyRank()) {
            StaticShape shape;
            shape.reserve(array->rank());
            for (const ExtentForm &form : array->extents()) {
                shape.push_back(form.isConstant() ? form.constant() : kUnknownDim);
            }
            facts.shape = std::move(shape);
        }
        return facts;
    }
    // A nested array (`f32[][]`): the innermost element type decides the dtype, the nesting depth
    // is the rank, and the extents of a written-out literal are not known.
    Type *elem = array->elemType();
    size_t depth = 1;
    while (elem->code() == TypeCode::Array) {
        elem = static_cast<ArrayType *>(elem)->elemType();
        ++depth;
    }
    TensorFacts facts;
    if (isSupportedTensorScalar(elem->code())) {
        facts.dtype = elem->code();
    }
    facts.shape = StaticShape(depth, kUnknownDim);
    return facts;
}

} // namespace camel::tensor
