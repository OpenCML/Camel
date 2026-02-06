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

#include "struct.h"

#include "../special/ref.h"
#include "core/type/composite/struct.h"
#include "utils/str.h"
#include "utils/type.h"

using namespace std;

struct StructDataFactory::Impl {
    StructTypeFactory typeFactory;
    std::map<std::string, data_ptr_t> data;
    std::vector<std::string> refIndices;
};

StructDataFactory::StructDataFactory() : impl_(std::make_unique<Impl>()) {}
StructDataFactory::~StructDataFactory() = default;

StructDataFactory &StructDataFactory::add(const std::string &key, const data_ptr_t &val) {
    if (!impl_->typeFactory.add(key, val->type())) {
        return *this;
    }
    impl_->data[key] = val;
    if (val->type()->code() == TypeCode::Ref) {
        impl_->refIndices.push_back(key);
    }
    return *this;
}

std::shared_ptr<StructData> StructDataFactory::build() {
    Type *type = impl_->typeFactory.build();
    return std::shared_ptr<StructData>(
        new StructData(type, std::move(impl_->data), std::move(impl_->refIndices)));
}

StructData::StructData(
    Type *type, std::map<std::string, data_ptr_t> &&data, std::vector<std::string> &&refIndices)
    : CompositeData(type), refIndices_(std::move(refIndices)), data_(std::move(data)) {}

StructData::StructData() : CompositeData(StructType::create()) {}

StructData::StructData(initializer_list<pair<string, data_ptr_t>> data) : CompositeData(nullptr) {
    StructDataFactory f;
    for (const auto &e : data) {
        f.add(e.first, e.second);
    }
    auto p      = f.build();
    type_       = p->type_;
    data_       = std::move(p->data_);
    refIndices_ = std::move(p->refIndices_);
}

std::shared_ptr<StructData>
StructData::create(std::initializer_list<std::pair<std::string, data_ptr_t>> data) {
    StructDataFactory f;
    for (const auto &e : data) {
        f.add(e.first, e.second);
    }
    return f.build();
}

StructData::StructData(std::map<std::string, data_ptr_t> &&data) : CompositeData(nullptr) {
    StructDataFactory f;
    for (auto &e : data) {
        f.add(e.first, e.second);
    }
    auto p      = f.build();
    type_       = p->type_;
    data_       = std::move(p->data_);
    refIndices_ = std::move(p->refIndices_);
}

std::shared_ptr<StructData> StructData::create(std::map<std::string, data_ptr_t> &&data) {
    StructDataFactory f;
    for (auto &e : data) {
        f.add(e.first, e.second);
    }
    return f.build();
}

bool StructData::has(const string &key) const {
    ASSERT(resolved(), "Cannot check data from unresolved StructData");
    return data_.find(key) != data_.end();
}

data_ptr_t StructData::get(const string &key) const {
    ASSERT(resolved(), "Cannot get data from unresolved StructData");
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return nullptr;
}

bool StructData::equals(const data_ptr_t &other) const {
    if (other == nullptr || other->type()->code() != TypeCode::Struct) {
        return false;
    }
    auto o = tt::as_shared<StructData>(other);
    if (data_.size() != o->data_.size()) {
        return false;
    }
    for (const auto &[k, v] : data_) {
        auto it = o->data_.find(k);
        if (it == o->data_.end() || !v->equals(it->second)) {
            return false;
        }
    }
    return true;
}

vector<string> StructData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &e : refIndices_) {
        const auto &refData = tt::as_shared<RefData>(data_.at(e));
        res.push_back(refData->ref());
    }
    return res;
}

void StructData::resolve(const data_vec_t &dataList) {
    if (refIndices_.empty()) {
        return;
    }
    ASSERT(refIndices_.size() == dataList.size(), "DataList size mismatch");
    for (size_t i = 0; i < refIndices_.size(); i++) {
        const string &key = refIndices_[i];
        data_ptr_t data   = dataList[i];
        data_[key]        = data;
    }
    refIndices_.clear();
}

data_ptr_t StructData::clone(bool deep) const {
    StructDataFactory f;
    for (const auto &[key, val] : data_) {
        f.add(key, deep ? val->clone(true) : val);
    }
    return f.build();
}

const string StructData::toString() const {
    if (data_.size() == 0) {
        return "{}";
    }
    string str = "{ ";
    str += strutil::join(data_, ", ", [](const pair<const string, data_ptr_t> &e) {
        return e.first + ": " + e.second->toString();
    });
    str += " }";
    return str;
}

data_ptr_t StructData::convertTo(Type *type) {
    if (type->equals(type_)) {
        return tt::as_shared<StructData>(shared_from_this());
    }
    return nullptr;
}
