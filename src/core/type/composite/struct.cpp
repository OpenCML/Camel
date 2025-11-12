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
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "utils/assert.h"

#include <algorithm>
#include <sstream>

using namespace std;

StructType::StructType() : CompositeType(TypeCode::Struct) {}

shared_ptr<StructType> StructType::create() const { return make_shared<StructType>(); }

size_t StructType::size() const { return fields_.size(); }

optional<type_ptr_t> StructType::typeAt(size_t idx) const {
    if (idx >= fields_.size()) {
        return nullopt;
    }
    return fields_[idx];
}

optional<type_ptr_t> StructType::typeOf(const string &name) const {
    auto it = fieldIndexMap_.find(name);
    if (it == fieldIndexMap_.end()) {
        return nullopt;
    }
    return fields_[it->second];
}

bool StructType::add(const string &name, const type_ptr_t &type) {
    if (has(name)) {
        return false;
    }
    fieldIndexMap_[name] = fields_.size();
    fields_.push_back(type);
    return true;
}

bool StructType::has(const string &name) const {
    return fieldIndexMap_.find(name) != fieldIndexMap_.end();
}

type_ptr_t StructType::get(const string &name) const {
    auto it = fieldIndexMap_.find(name);
    ASSERT(it != fieldIndexMap_.end(), "Field not found: " + name);
    return fields_[it->second];
}

type_ptr_t StructType::operator|(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "Cannot union with unresolved StructType");
    auto result = make_shared<StructType>();

    // Copy all fields from this struct
    for (const auto &kv : fieldIndexMap_) {
        result->add(kv.first, fields_[kv.second]);
    }

    // Add/overwrite fields from other
    for (const auto &kv : other.fieldIndexMap_) {
        const string &name     = kv.first;
        const type_ptr_t &type = other.fields_[kv.second];
        if (!result->has(name)) {
            result->add(name, type);
        } else {
            result->fields_[result->fieldIndexMap_[name]] = type;
        }
    }
    return result;
}

type_ptr_t StructType::operator&(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "Cannot intersect with unresolved StructType");
    auto result = make_shared<StructType>();
    for (const auto &kv : fieldIndexMap_) {
        const string &name = kv.first;
        if (other.has(name)) {
            result->add(name, other.get(name));
        }
    }
    return result;
}

type_ptr_t StructType::resolve(const type_vec_t &typeList) const {
    ASSERT(!resolved(), "StructType is already resolved");

    auto newStruct  = make_shared<StructType>();
    size_t refCount = 0;
    size_t j        = 0;

    for (const auto &kv : fieldIndexMap_) {
        const string &name     = kv.first;
        const type_ptr_t &type = fields_[kv.second];
        if (type->code() == TypeCode::Ref) {
            ASSERT(j < typeList.size(), "Not enough types in typeList");
            newStruct->add(name, typeList[j++]);
            refCount++;
        } else {
            newStruct->add(name, type);
        }
    }

    ASSERT(j == typeList.size(), "Not all types in typeList are used");
    ASSERT(newStruct->resolved(), "StructType is not fully resolved");
    return newStruct;
}

bool StructType::resolved() const {
    return none_of(fields_.begin(), fields_.end(), [](const type_ptr_t &t) {
        return t->code() == TypeCode::Ref;
    });
}

string StructType::toString() const {
    if (fields_.empty()) {
        return "{}";
    }
    ostringstream oss;
    oss << "{ ";
    for (const auto &kv : fieldIndexMap_) {
        oss << kv.first << ": " << (fields_[kv.second] ? fields_[kv.second]->toString() : "null")
            << ", ";
    }
    string result = oss.str();
    result.pop_back();
    result.pop_back();
    result += " }";
    return result;
}

string StructType::mangle() const {
    string result = "D";
    result += to_string(fields_.size());
    for (const auto &kv : fieldIndexMap_) {
        const string &name = kv.first;
        result += "K" + to_string(name.size()) + name;
        result += "V" + fields_[kv.second]->mangle();
    }
    return result;
}

type_ptr_t StructType::clone() const {
    auto newStruct = make_shared<StructType>();
    for (const auto &kv : fieldIndexMap_) {
        newStruct->add(kv.first, fields_[kv.second]);
    }
    return newStruct;
}

bool StructType::equals(const type_ptr_t &other) const {
    if (this == other.get()) {
        return true;
    }
    if (other->code() != TypeCode::Struct) {
        return false;
    }
    const auto &otherStruct = static_cast<const StructType &>(*other);

    if (fields_.size() != otherStruct.fields_.size()) {
        return false;
    }
    for (const auto &kv : fieldIndexMap_) {
        const string &name = kv.first;
        if (!otherStruct.has(name)) {
            return false;
        }
        if (!fields_[kv.second]->equals(otherStruct.get(name))) {
            return false;
        }
    }
    return true;
}

CastSafety StructType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool StructType::assignable(const type_ptr_t &type) const { return this->equals(type); }