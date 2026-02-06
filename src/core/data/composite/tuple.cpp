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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tuple.h"
#include "core/data/base.h"
#include "core/type/composite/tuple.h"
#include "utils/scope.h"
#include "utils/str.h"
#include "utils/type.h"

#include "../special/null.h"
#include "../special/ref.h"

using namespace std;

struct TupleDataFactory::Impl {
    TupleTypeFactory typeFactory;
    std::vector<data_ptr_t> data;
    std::vector<size_t> refIndices;
};

TupleDataFactory::TupleDataFactory() : impl_(std::make_unique<Impl>()) {}
TupleDataFactory::~TupleDataFactory() = default;

TupleDataFactory &TupleDataFactory::add(const data_ptr_t &e) {
    impl_->typeFactory.add(e->type());
    impl_->data.push_back(e);
    if (e->type()->code() == TypeCode::Ref) {
        impl_->refIndices.push_back(impl_->data.size() - 1);
    }
    return *this;
}

std::shared_ptr<TupleData> TupleDataFactory::build() {
    Type *type = impl_->typeFactory.build();
    return std::shared_ptr<TupleData>(
        new TupleData(type, std::move(impl_->data), std::move(impl_->refIndices)));
}

TupleData::TupleData(Type *type, data_vec_t &&data, std::vector<size_t> &&refIndices)
    : CompositeData(type), refIndices_(std::move(refIndices)), data_(std::move(data)) {}

TupleData::TupleData(data_list_t data) : CompositeData(nullptr) {
    TupleDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    auto p      = f.build();
    type_       = p->type_;
    data_       = std::move(p->data_);
    refIndices_ = std::move(p->refIndices_);
}

TupleData::TupleData(Type *type, data_vec_t &&data) : CompositeData(type), data_(std::move(data)) {
    ASSERT(type->code() == TypeCode::Tuple, "Type is not TupleType");
    refIndices_.clear();
    for (size_t i = 0; i < data_.size(); i++) {
        if (data_[i] && data_[i]->type()->code() == TypeCode::Ref) {
            refIndices_.push_back(i);
        }
    }
}

std::shared_ptr<TupleData> TupleData::create(data_list_t data) {
    TupleDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    return f.build();
}

std::shared_ptr<TupleData> TupleData::create(Type *type, data_vec_t &&data) {
    return std::make_shared<TupleData>(type, std::move(data));
}

data_ptr_t TupleData::get(size_t index) const {
    ASSERT(resolved(), "Cannot get data from unresolved TupleData");
    if (index >= data_.size()) {
        return nullptr;
    }
    return data_[index];
}

bool TupleData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for TupleData
    return true;
}

vector<string> TupleData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &idx : refIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void TupleData::resolve(const data_vec_t &dataList) {
    if (refIndices_.empty()) {
        return;
    }
    ASSERT(refIndices_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refIndices_.size(); i++) {
        size_t idx = refIndices_[i];
        data_[idx] = dataList[i];
    }
    refIndices_.clear();
}

data_ptr_t TupleData::clone(bool deep) const {
    TupleDataFactory f;
    for (const auto &e : data_) {
        f.add(deep ? e->clone(deep) : e);
    }
    return f.build();
}

const string TupleData::toString() const {
    string str = "(";
    str += strutil::join(data_, ", ", [](const data_ptr_t &e) { return e->toString(); });
    str += ")";
    return str;
}

data_ptr_t TupleData::convertTo(Type *type) {
    if (type->equals(type_)) {
        return tt::as_shared<TupleData>(shared_from_this());
    }
    if (type->code() == TypeCode::Tuple) {
        auto tupleType = tt::as_ptr<TupleType>(type);
        ASSERT(tupleType != nullptr, "Failed to cast to TupleType");
        data_vec_t data;
        data.reserve(data_.size());
        for (size_t i = 0; i < data_.size(); i++) {
            const auto &e  = data_[i];
            Type *elemType = tupleType->typeAt(i);
            if (elemType) {
                data.push_back(e->convertTo(elemType));
            } else {
                data.push_back(nullptr);
            }
        }
        return TupleData::create(type, std::move(data));
    }
    return nullptr;
}
