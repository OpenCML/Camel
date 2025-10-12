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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"
#include "utils/scope.h"

#include "../special/any.h"
#include "../special/null.h"
#include "../special/ref.h"

#include "error/diagnostics/diagnostics.h"

using namespace std;

ArrayData::ArrayData(type_ptr_t arrType, data_list_t data) : ComposedData(arrType), data_(data) {
    for (const auto &e : data) {
        emplace(e);
    }
}

ArrayData::ArrayData(type_ptr_t arrType, data_vec_t &&data)
    : ComposedData(arrType), data_(std::move(data)) {
    ASSERT(arrType->code() == TypeCode::Array, "Type is not ArrayType");
}

void ArrayData::emplace(const data_ptr_t &e) {
    if (e->type()->code() == TypeCode::Ref) {
        refIndices_.push_back(data_.size());
    } else {
        const auto &arrType = tt::as_shared<ArrayType>(type_);
        ASSERT(arrType, "ArrayData type is not ArrayType");
        const auto &elemType = arrType->elementType();
        if (elemType == Type::Void()) {
            type_ = std::make_shared<ArrayType>(e->type());
        } else if (!e->type()->assignable(elemType)) {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", e->type()->toString(), elemType->toString());
        }
    }
    data_.push_back(e);
}

bool ArrayData::equals(const data_ptr_t &other) const {
    // TODO: implement equals for ArrayData
    return true;
}

data_ptr_t ArrayData::convert(type_ptr_t target, bool inplace) {
    if (target == type_ || type_->equals(target)) {
        // same type, no need to convert
        return shared_from_this();
    }
    try {
        if (target->composed()) {
            switch (target->code()) {
                // TODO: implement conversion to other composed types
            default:
                throw UnsupportedConvError();
            }
        } else if (target->special()) {
            switch (target->code()) {
            case TypeCode::Any:
                return make_shared<AnyData>(shared_from_this());
                break;
            case TypeCode::Void:
                return Data::null();
                break;
            default:
                throw UnsupportedConvError();
            }
        }
        throw UnsupportedConvError();
    } catch (const UnsupportedConvError &e) {
        throw DataConvError(
            "Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    } catch (const std::exception &e) {
        throw DataConvError(e.what());
    }
    throw DataConvError(
        "Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
}

vector<string> ArrayData::refs() const {
    vector<string> res;
    res.reserve(refIndices_.size());
    for (const auto &idx : refIndices_) {
        data_ptr_t ref = data_[idx];
        res.push_back(tt::as_shared<RefData>(ref)->ref());
    }
    return res;
}

void ArrayData::resolve(const data_vec_t &dataList) {
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

data_ptr_t ArrayData::clone(bool deep) const {
    auto vec = make_shared<ArrayData>(type_);
    for (const auto &e : data_) {
        vec->emplace(deep ? e->clone(deep) : e);
    }
    return vec;
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

void ArrayData::print(std::ostream &os) const { os << toString(); }
