// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/data/base.h"
#include "camel/core/data/null.h"
#include "camel/core/data/ref.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"

using namespace std;
using namespace camel::core::data;
using namespace camel::core::type;

struct TupleDataFactory::Impl {
    TupleTypeFactory typeFactory;
    std::vector<data_ptr_t> data;
    std::vector<size_t> holeIndices;
};

TupleDataFactory::TupleDataFactory() : impl_(std::make_unique<Impl>()) {}
TupleDataFactory::~TupleDataFactory() = default;

TupleDataFactory &TupleDataFactory::add(const data_ptr_t &e) {
    impl_->typeFactory.add(e->type());
    impl_->data.push_back(e);
    if (e->type()->code() == TypeCode::Ref) {
        impl_->holeIndices.push_back(impl_->data.size() - 1);
    }
    return *this;
}

std::shared_ptr<TupleData> TupleDataFactory::build() {
    Type *type = impl_->typeFactory.build();
    return std::shared_ptr<TupleData>(
        new TupleData(type, std::move(impl_->data), std::move(impl_->holeIndices)));
}

TupleData::TupleData(Type *type, data_vec_t &&data, std::vector<size_t> &&holeIndices)
    : CompositeData(type), holeIndices_(std::move(holeIndices)), data_(std::move(data)) {}

TupleData::TupleData(data_list_t data) : CompositeData(nullptr) {
    TupleDataFactory f;
    for (const auto &e : data) {
        f.add(e);
    }
    auto p       = f.build();
    type_        = p->type_;
    data_        = std::move(p->data_);
    holeIndices_ = std::move(p->holeIndices_);
}

TupleData::TupleData(Type *type, data_vec_t &&data) : CompositeData(type), data_(std::move(data)) {
    ASSERT(type->code() == TypeCode::Tuple, "Type is not TupleType");
    holeIndices_.clear();
    for (size_t i = 0; i < data_.size(); i++) {
        if (data_[i] && data_[i]->type()->code() == TypeCode::Ref) {
            holeIndices_.push_back(i);
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
    res.reserve(holeIndices_.size());
    for (const auto &idx : holeIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void TupleData::resolve(const data_vec_t &dataList) {
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
        if (data_.size() != tupleType->size()) {
            return nullptr;
        }
        data_vec_t data;
        data.reserve(data_.size());
        for (size_t i = 0; i < data_.size(); i++) {
            const auto &e  = data_[i];
            Type *elemType = tupleType->typeAt(i);
            if (e == nullptr || elemType == nullptr) {
                return nullptr;
            }
            data_ptr_t converted = convertDataTo(e, elemType);
            if (converted == nullptr) {
                // A tuple with a hole is not a value: the conversion of the whole literal fails,
                // and the caller reports the element that could not be converted.
                return nullptr;
            }
            data.push_back(std::move(converted));
        }
        return TupleData::create(type, std::move(data));
    }
    return nullptr;
}
