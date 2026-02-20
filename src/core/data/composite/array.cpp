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
 * Created: Oct. 06, 2024
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/data/composite/array.h"
#include "camel/core/data/ref.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

using namespace std;

struct ArrayDataFactory::Impl {
    Type *elemType = nullptr;
    std::vector<size_t> refs;
    std::vector<data_ptr_t> data;
};

ArrayDataFactory::ArrayDataFactory() : impl_(std::make_unique<Impl>()) {}
ArrayDataFactory::~ArrayDataFactory() = default;

ArrayDataFactory &ArrayDataFactory::add(const data_ptr_t &e) {
    if (!impl_->elemType) {
        impl_->elemType = e->type();
    } else if (!e->type()->assignable(impl_->elemType)) {
        throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
            .commit("Array", e->type()->toString(), impl_->elemType->toString());
    }
    if (e->type()->code() == TypeCode::Ref) {
        impl_->refs.push_back(impl_->data.size());
    }
    impl_->data.push_back(e);
    return *this;
}

std::shared_ptr<ArrayData> ArrayDataFactory::build() {
    ArrayTypeFactory typeFactory;
    typeFactory.setElemType(impl_->elemType ? impl_->elemType : Type::Void());
    for (size_t r : impl_->refs) {
        typeFactory.addRef(r);
    }
    Type *type = typeFactory.build();
    return std::shared_ptr<ArrayData>(new ArrayData(type, std::move(impl_->data)));
}

ArrayData::ArrayData(Type *arrayType, data_vec_t &&data)
    : CompositeData(arrayType), data_(std::move(data)) {
    const auto *arr = tt::as_ptr<ArrayType>(arrayType);
    if (arr && arr->refCount() > 0) {
        refs_.assign(arr->refs(), arr->refs() + arr->refCount());
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
    auto p = f.build();
    type_  = p->type_;
    data_  = std::move(p->data_);
    refs_  = std::move(p->refs_);
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
    auto p = f.build();
    type_  = p->type_;
    data_  = std::move(p->data_);
    refs_  = std::move(p->refs_);
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
    res.reserve(refs_.size());
    for (const auto &idx : refs_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void ArrayData::resolve(const data_vec_t &dataList) {
    if (refs_.empty()) {
        return;
    }
    ASSERT(refs_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refs_.size(); i++) {
        size_t idx = refs_[i];
        data_[idx] = dataList[i];
    }
    refs_.clear();
}

data_ptr_t ArrayData::clone(bool deep) const {
    if (data_.empty()) {
        return std::shared_ptr<ArrayData>(new ArrayData(type_, data_vec_t{}));
    }
    ArrayDataFactory f;
    for (const auto &e : data_) {
        f.add(deep ? e->clone(deep) : e);
    }
    return f.build();
}

const string ArrayData::toString() const {
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

data_ptr_t ArrayData::convertTo(Type *type) {
    if (type->equals(type_)) {
        return tt::as_shared<ArrayData>(shared_from_this());
    }
    if (type->code() == TypeCode::Array) {
        const auto &arrType = tt::as_ptr<ArrayType>(type);
        return ArrayData::from(arrType->elemType(), data_);
    }
    return nullptr;
}
