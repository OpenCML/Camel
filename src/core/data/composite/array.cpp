// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/composite/array.h"
#include "camel/core/data/ref.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

using namespace std;
using namespace camel::core::data;
using namespace camel::core::type;
using namespace camel::core::error;

struct ArrayDataFactory::Impl {
    Type *elemType = nullptr;
    std::vector<ExtentForm> extents;
    std::vector<size_t> holeIndices;
    std::vector<data_ptr_t> data;
};

ArrayDataFactory::ArrayDataFactory() : impl_(std::make_unique<Impl>()) {}
ArrayDataFactory::~ArrayDataFactory() = default;

ArrayDataFactory &ArrayDataFactory::add(const data_ptr_t &e) {
    data_ptr_t value = e;
    // The element in a position of `T[..]` is the rank-zero value that position promises (design
    // principles 5.4), and it is written at the element type the position states: `[1.5, 2.5]` at
    // `f32[..][2]` holds two rank-zero `f32` values, not two `float`s the element type cannot take.
    // This is the same edge `convertDataTo` makes, applied to each element, because this is where
    // the elements of an array literal are collected.
    if (impl_->elemType != nullptr && impl_->elemType->code() == TypeCode::Array &&
        !impl_->elemType->equals(value->type())) {
        if (auto *slotArrayType = tt::as_ptr<ArrayType>(impl_->elemType);
            slotArrayType != nullptr) {
            if (Type *element = slotArrayType->rankZeroType(); element != nullptr) {
                data_ptr_t asElement =
                    element->equals(value->type()) ? value : convertDataTo(value, element);
                if (asElement != nullptr) {
                    if (data_ptr_t zero = convertDataTo(asElement, impl_->elemType);
                        zero != nullptr) {
                        value = zero;
                    }
                }
            }
        }
    }
    if (!impl_->elemType) {
        impl_->elemType = value->type();
    } else if (impl_->elemType->code() == TypeCode::Ref && value->type()->code() != TypeCode::Ref) {
        // When an array literal mixes dynamic expressions and static values, prefer the concrete
        // element type; holeIndices only records which positions need backfilling later.
        impl_->elemType = value->type();
    } else if (
        impl_->elemType->code() != TypeCode::Ref && value->type()->code() == TypeCode::Ref) {
        // Keep the known concrete element type and only mark this position as a deferred reference.
    } else if (!impl_->elemType->assignableFrom(value->type())) {
        // Widen to the least common element type when one exists (e.g. tensors whose static
        // shapes differ, or an unnamed struct value next to a named struct type whose fields
        // it has: the array is of the named type, design principles 5.2); otherwise the
        // elements are incompatible.
        Type *joined = joinTypes(impl_->elemType, value->type());
        if (!joined) {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", value->type()->toString(), impl_->elemType->toString());
        }
        if (!joined->equals(impl_->elemType)) {
            // The elements already collected are values of the joined type too: a literal of
            // an unnamed struct type becomes the named type it enters.
            for (data_ptr_t &existing : impl_->data) {
                if (existing == nullptr || existing->type()->code() == TypeCode::Ref ||
                    joined->equals(existing->type())) {
                    continue;
                }
                data_ptr_t converted = existing->convertTo(joined);
                if (converted == nullptr) {
                    throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                        .commit("Array", existing->type()->toString(), joined->toString());
                }
                existing = converted;
            }
        }
        // The element being added is a value of the joined type too.
        if (!joined->equals(value->type())) {
            data_ptr_t converted = value->convertTo(joined);
            if (converted == nullptr) {
                throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                    .commit("Array", value->type()->toString(), joined->toString());
            }
            value = converted;
        }
        impl_->elemType = joined;
    }
    if (value->type()->code() == TypeCode::Ref) {
        impl_->holeIndices.push_back(impl_->data.size());
    }
    impl_->data.push_back(value);
    return *this;
}

ArrayDataFactory &ArrayDataFactory::setElemType(Type *elemType) {
    impl_->elemType = elemType;
    return *this;
}

ArrayDataFactory &ArrayDataFactory::setExtents(std::span<const int64_t> extents) {
    impl_->extents.clear();
    impl_->extents.reserve(extents.size());
    for (const int64_t extent : extents) {
        impl_->extents.push_back(ExtentForm::constant(extent));
    }
    return *this;
}

std::shared_ptr<ArrayData> ArrayDataFactory::build() {
    Type *type = nullptr;
    if (impl_->extents.empty()) {
        ArrayTypeFactory typeFactory;
        typeFactory.setElemType(impl_->elemType ? impl_->elemType : Type::Void());
        type = typeFactory.build();
    } else {
        // The shape the literal writes is the value's own shape, so the elements are one array
        // of that many dimensions rather than the array of the elements they were written as
        // (design principles 5.1, 5.4: a nested literal at an `f32[2, 2]` position).
        type = ArrayType::create(
            impl_->elemType ? impl_->elemType : Type::Void(), impl_->extents);
    }
    return std::shared_ptr<ArrayData>(new ArrayData(type, std::move(impl_->data)));
}

ArrayData::ArrayData(Type *arrayType, data_vec_t &&data)
    : CompositeData(arrayType), data_(std::move(data)) {
    holeIndices_.clear();
    for (size_t i = 0; i < data_.size(); ++i) {
        if (data_[i] && data_[i]->type()->code() == TypeCode::Ref) {
            holeIndices_.push_back(i);
        }
    }
}

ArrayData::ArrayData(Type *elemType, data_list_t data) : CompositeData(nullptr) {
    if (data.size() == 0) {
        type_ = ArrayType::create(elemType);
        return;
    }
    ArrayDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    auto p       = f.build();
    type_        = p->type_;
    data_        = std::move(p->data_);
    holeIndices_ = std::move(p->holeIndices_);
}

ArrayData::ArrayData(Type *elemType, const data_vec_t &data) : CompositeData(nullptr) {
    if (data.empty()) {
        type_ = ArrayType::create(elemType);
        return;
    }
    ArrayDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    auto p       = f.build();
    type_        = p->type_;
    data_        = std::move(p->data_);
    holeIndices_ = std::move(p->holeIndices_);
}

std::shared_ptr<ArrayData> ArrayData::create(Type *elemType, data_list_t data) {
    if (data.size() == 0) {
        return std::shared_ptr<ArrayData>(new ArrayData(ArrayType::create(elemType), data_vec_t{}));
    }
    ArrayDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    return f.build();
}

std::shared_ptr<ArrayData> ArrayData::rankZero(ArrayType *arrayType, const data_ptr_t &element) {
    ASSERT(
        arrayType != nullptr && arrayType->rankZeroType() != nullptr,
        "A rank-zero value is an element of an open rank.");
    data_vec_t raw;
    raw.push_back(element);
    std::shared_ptr<ArrayData> value(new ArrayData(arrayType, std::move(raw)));
    value->rankZero_ = true;
    return value;
}

data_ptr_t camel::core::data::convertDataTo(const data_ptr_t &value, Type *type) {
    if (value == nullptr || type == nullptr) {
        return nullptr;
    }
    if (type->code() == TypeCode::Array) {
        auto *arrayType = tt::as_ptr<ArrayType>(type);
        Type *element   = arrayType != nullptr ? arrayType->rankZeroType() : nullptr;
        if (element != nullptr && element->assignableFrom(value->type())) {
            // The element in a position of `T[..]` is the rank-zero value the position promises
            // (design principles 5.4): the same value, kept as the value with no dimension it is,
            // so the run has an array to read and the program still sees the element.
            return ArrayData::rankZero(arrayType, value);
        }
    }
    return value->convertTo(type);
}

std::shared_ptr<ArrayData> ArrayData::from(Type *elemType, const data_vec_t &data) {
    if (data.empty()) {
        return std::shared_ptr<ArrayData>(new ArrayData(ArrayType::create(elemType), data_vec_t{}));
    }
    ArrayDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    return f.build();
}

bool ArrayData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ArrayData
    return true;
}

vector<string> ArrayData::refs() const {
    vector<string> res;
    res.reserve(holeIndices_.size());
    for (const auto &idx : holeIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void ArrayData::resolve(const data_vec_t &dataList) {
    if (holeIndices_.empty()) {
        return;
    }
    ASSERT(holeIndices_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < holeIndices_.size(); i++) {
        size_t idx = holeIndices_[i];
        data_[idx] = dataList[i];
    }
    holeIndices_.clear();
}

data_ptr_t ArrayData::clone(bool deep) const {
    if (data_.empty()) {
        return std::shared_ptr<ArrayData>(new ArrayData(type_, data_vec_t{}));
    }
    if (rankZero_) {
        // The value with no dimension is the element itself: a clone of it is a rank-zero value of
        // the same type holding a clone of the element, not an array of one element.
        return rankZero(tt::as_ptr<ArrayType>(type_), deep ? data_[0]->clone(true) : data_[0]);
    }
    ArrayDataFactory f;
    for (const auto &e : data_) {
        f.add(deep ? e->clone(deep) : e);
    }
    return f.build();
}

const string ArrayData::toString() const {
    // A rank-zero value has no dimension to write brackets for: it is written as the element.
    if (rankZero_ && !data_.empty()) {
        return data_[0]->toString();
    }
    string str = "[";
    for (const auto &e : data_) {
        str += e->toString() + ", ";
    }
    if (data_.size() > 0) {
        str.pop_back();
        str.pop_back();
    }
    str += "]";
    return str;
}

namespace {

/// The shape a type states in constants (`f32[3]`, `f32[2, 2]`), which is the shape a literal
/// can be the elements of. A shape left open (`f32[]`), written over a dimension name (`f32[n]`)
/// or fixed only in part states none here: what those state is checked when the conversion runs.
bool statedShapeOf(const ArrayType *type, std::vector<int64_t> &extents) {
    const std::span<const ExtentForm> forms = type->extents();
    if (forms.empty()) {
        return false;
    }
    extents.clear();
    for (const ExtentForm &form : forms) {
        if (!form.isConstant()) {
            return false;
        }
        extents.push_back(form.constant());
    }
    return true;
}

} // namespace

data_ptr_t ArrayData::convertTo(Type *type) {
    if (type->equals(type_)) {
        return tt::as_shared<ArrayData>(shared_from_this());
    }
    if (type->code() == TypeCode::Array) {
        const auto &arrType = tt::as_ptr<ArrayType>(type);
        // Every literal element takes the array's element type (an unnamed struct literal
        // becomes the named type it enters, design principles 5.2).
        ArrayDataFactory f;
        f.setElemType(arrType->elemType());
        // The shape a type states is part of the type: the elements of a literal are one array
        // of that shape, not the array of unknown shape their element type alone gives them
        // (`let x: f32[3] = [1.0, 2.0, 3.0]` is an `f32[3]`, design principles 5.1, 5.4). A
        // shape the elements do not fill is not one this value can take, and the conversion
        // has no answer: the declaration reports the literal it cannot make.
        std::vector<int64_t> extents;
        if (statedShapeOf(arrType, extents)) {
            int64_t count = 1;
            for (const int64_t extent : extents) {
                count *= extent;
            }
            if (count != static_cast<int64_t>(data_.size())) {
                return nullptr;
            }
            f.setExtents(extents);
        }
        for (const auto &e : data_) {
            f.add(e);
        }
        return f.build();
    }
    return nullptr;
}
