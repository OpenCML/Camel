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
 * Updated: Oct. 21, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "params.h"
#include "core/data/data.h"

using namespace std;

CastSafety ParamsType::convertibilityToParams(const ParamsType &other) const {
    const auto &indexEls = indexElements();
    const auto &namedEls = namedElements();
    CastSafety result = CastSafety::Safe;
    size_t idx = 0;
    for (size_t i = 0; i < other.elements_.size(); i++) {
        const auto &[name, type, value] = other.elements_[i];
        type_ptr_t elType = nullptr;
        if (name.empty() || namedEls.find(name) == namedEls.end()) {
            if (idx >= indexEls.size()) {
                if (!value) {
                    return CastSafety::Forbidden;
                }
                continue;
            }
            elType = indexEls[idx];
            idx++;
        } else {
            elType = namedEls.at(name);
        }
        CastSafety elConv = elType->castSafetyTo(*type);
        if (elConv == CastSafety::Forbidden) {
            return CastSafety::Forbidden;
        } else if (elConv == CastSafety::Unsafe) {
            result = CastSafety::Unsafe;
        }
    }
    return result;
}

ParamsType::ParamsType() : StructType(TypeCode::Params) {}

ParamsType::ParamsType(
    const std::initializer_list<std::tuple<string, type_ptr_t, data_ptr_t>> &&elements)
    : StructType(TypeCode::Params), elements_(std::move(elements)) {}

string ParamsType::toString() const {
    string result = "Params<";
    for (const auto &tuple : elements_) {
        auto &[name, type, value] = tuple;
        result += (name.empty() ? "" : name + ": ") + (type ? type->toString() : "NULL");
        if (value) {
            result += " = " + value->toString();
        }
        result += ", ";
    }
    if (elements_.size() > 0) {
        result.pop_back();
        result.pop_back();
    }
    result += ">";
    return result;
}

bool ParamsType::operator==(const Type &other) const {
    if (other.code() != TypeCode::Params) {
        return false;
    }
    const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);

    if (elements_.size() != otherParam.elements_.size()) {
        return false;
    }
    for (size_t i = 0; i < elements_.size(); i++) {
        auto &[name, type, value] = elements_[i];
        auto &[otherName, otherType, otherValue] = otherParam.elements_[i];
        if (name != otherName) {
            return false;
        }
        if (!type->equals(otherType)) {
            return false;
        }
        if (value && !value->equals(otherValue)) {
            return false;
        }
    }
    return true;
}

bool ParamsType::operator!=(const Type &other) const { return !(*this == other); }

bool ParamsType::add(const string &key, const type_ptr_t &type, const data_ptr_t &value) {
    // here we allow duplicate keys, for the sake of simplicity
    // we use "" to represent the unnamed parameter
    elements_.push_back({key, type, value});
    return true;
}

size_t ParamsType::size() const { return elements_.size(); }

const std::tuple<string, type_ptr_t, data_ptr_t> &ParamsType::elementAt(size_t idx) const {
    if (idx >= elements_.size()) {
        throw out_of_range("Index out of range");
    }
    return elements_[idx];
}

const vector<tuple<string, type_ptr_t, data_ptr_t>> &ParamsType::elements() const {
    return elements_;
}

vector<type_ptr_t> ParamsType::indexElements() const {
    auto result = vector<type_ptr_t>();
    for (const auto &tuple : elements_) {
        const auto &[name, type, value] = tuple;
        if (name.empty()) {
            result.push_back(type);
        }
    }
    return result;
}

std::unordered_map<string, type_ptr_t> ParamsType::namedElements() const {
    auto result = std::unordered_map<string, type_ptr_t>();
    for (const auto &tuple : elements_) {
        const auto &[name, type, value] = tuple;
        if (!name.empty()) {
            result[name] = type;
        }
    }
    return result;
}

void ParamsType::clear() { elements_.clear(); }

CastSafety ParamsType::castSafetyTo(const Type &other) const {
    // TODO: not fully implemented
    // TODO: others' convertibility should be checked
    if (other.structured()) {
        switch (other.code()) {
        case TypeCode::Params: {
            const ParamsType &otherParam = dynamic_cast<const ParamsType &>(other);
            if (elements_.size() != otherParam.elements_.size()) {
                return CastSafety::Forbidden;
            }
            CastSafety result = CastSafety::Safe;
            for (size_t i = 0; i < elements_.size(); i++) {
                const auto &[name, type, value] = elements_[i];
                if (name != name) {
                    return CastSafety::Forbidden;
                }
                CastSafety paramConv = type->castSafetyTo(*type);
                if (paramConv == CastSafety::Forbidden) {
                    return CastSafety::Forbidden;
                } else if (paramConv == CastSafety::Unsafe) {
                    result = CastSafety::Unsafe;
                }
            }
            return result;
        }
        case TypeCode::Union:
            return CastSafety::Safe;
        case TypeCode::List:
            return CastSafety::Safe;
        case TypeCode::Set:
            return CastSafety::Safe;
        case TypeCode::Array:
            return CastSafety::Safe;
        case TypeCode::Vector:
            return CastSafety::Safe;
        case TypeCode::Tensor:
            return CastSafety::Safe;
        case TypeCode::Map:
            return CastSafety::Safe;
        case TypeCode::Dict:
            return CastSafety::Safe;

        default:
            return CastSafety::Forbidden;
        }
    }
    if (other.code() == TypeCode::Any) {
        return CastSafety::Safe;
    }
    // primary types and special types are forbidden
    return CastSafety::Forbidden;
}
