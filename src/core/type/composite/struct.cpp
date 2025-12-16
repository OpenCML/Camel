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
 * Updated: Dec. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "utils/assert.h"
#include "utils/type.h"

#include <algorithm>
#include <sstream>

using namespace std;

void StructType::computeLayout() const {
    std::vector<std::pair<std::string, TypeCode>> fieldList;
    fieldList.reserve(fields_.size());

    for (const auto &kv : fields_) {
        fieldList.emplace_back(kv.first, kv.second->code());
    }

    layout_ = std::make_shared<StructTypeLayout>(fieldList, refs_);
}

StructType::StructType() : CompositeType(TypeCode::Struct) {}

shared_ptr<StructType> StructType::create() { return make_shared<StructType>(); }

size_t StructType::size() const { return fields_.size(); }

optional<type_ptr_t> StructType::typeOf(const string &idx) const {
    auto it = fields_.find(idx);
    if (it == fields_.end()) {
        return nullopt;
    }
    return it->second;
}

bool StructType::add(const string &name, const type_ptr_t &type) {
    if (has(name)) {
        return false;
    }
    fields_.emplace(name, type);
    if (type->code() == TypeCode::Ref) {
        refs_.push_back(name);
    }
    return true;
}

bool StructType::has(const string &name) const { return fields_.find(name) != fields_.end(); }

type_ptr_t StructType::get(const string &name) const {
    auto it = fields_.find(name);
    if (it == fields_.end()) {
        throw out_of_range("StructType::get: field not found - " + name);
    }
    return it->second;
}

const StructTypeLayout &StructType::layout() const {
    if (!layout_) {
        computeLayout();
    }
    return *layout_;
}

type_ptr_t StructType::operator|(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator| requires resolved operands");

    auto result = make_shared<StructType>();

    // 复制 lhs 的字段
    for (const auto &kv : fields_) {
        const auto &name = kv.first;
        const auto &type = kv.second;
        result->fields_.emplace(name, type ? type->clone() : nullptr);
    }

    // 补齐 rhs 独有的字段
    for (const auto &kv : other.fields_) {
        const auto &name = kv.first;
        if (result->fields_.find(name) == result->fields_.end()) {
            const auto &type = kv.second;
            result->fields_.emplace(name, type->clone());
        }
    }

    return result;
}

type_ptr_t StructType::operator&(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator& requires resolved operands");

    auto result = make_shared<StructType>();

    for (const auto &kv : fields_) {
        const auto &name = kv.first;
        if (other.fields_.find(name) != other.fields_.end()) {
            const auto &type = kv.second;
            result->fields_.emplace(name, type->clone());
        }
    }

    return result;
}

type_ptr_t StructType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() == refs_.size(), "StructType::resolve: typeList size mismatch");

    auto resolvedStruct = tt::as_shared<StructType>(this->clone(false));

    for (size_t i = 0; i < refs_.size(); ++i) {
        const auto &fieldName              = refs_[i];
        const auto &resolvedType           = typeList[i];
        resolvedStruct->fields_[fieldName] = resolvedType;
    }

    resolvedStruct->refs_.clear();
    return resolvedStruct;
}

bool StructType::resolved() const { return refs_.empty(); }

std::optional<type_ptr_t> StructType::typeAt(size_t idx) const {
    if (idx >= fields_.size()) {
        return std::nullopt;
    }
    auto it = fields_.begin();
    std::advance(it, idx);
    return it->second;
}

string StructType::toString() const {
    ostringstream oss;
    oss << "{ ";
    bool first = true;
    for (const auto &kv : fields_) {
        if (!first) {
            oss << ", ";
        }
        first = false;
        oss << kv.first << ": " << (kv.second ? kv.second->toString() : "<null>");
    }
    oss << " }";
    return oss.str();
}

string StructType::mangle() const {
    ostringstream oss;
    oss << "S" << fields_.size();
    for (const auto &kv : fields_) {
        oss << kv.first.length() << kv.first;
        oss << (kv.second ? kv.second->mangle() : "N");
    }
    return oss.str();
}

type_ptr_t StructType::clone(bool deep /* = false */) const {
    auto result = make_shared<StructType>();

    result->refs_   = refs_;
    result->layout_ = layout_;

    for (const auto &kv : fields_) {
        const auto &name      = kv.first;
        const auto &fieldType = kv.second;
        result->fields_.emplace(name, deep ? fieldType->clone(true) : fieldType);
    }

    return result;
}

bool StructType::equals(const type_ptr_t &type) const {
    if (!type || type->code() != TypeCode::Struct) {
        return false;
    }
    auto other = tt::as_shared<const StructType>(type);
    if (!other) {
        return false;
    }
    if (fields_.size() != other->fields_.size()) {
        return false;
    }
    for (const auto &kv : fields_) {
        const auto &name    = kv.first;
        const auto &lhsType = kv.second;

        auto it = other->fields_.find(name);
        if (it == other->fields_.end()) {
            return false;
        }
        const auto &rhsType = it->second;
        if (!lhsType || !rhsType || !lhsType->equals(rhsType)) {
            return false;
        }
    }
    return true;
}

CastSafety StructType::castSafetyTo(const Type &) const { return CastSafety::Unsafe; }

bool StructType::assignable(const type_ptr_t &type) const {
    if (!type || type->code() != TypeCode::Struct) {
        return false;
    }
    auto other = tt::as_shared<const StructType>(type);
    if (!other) {
        return false;
    }
    for (const auto &kv : fields_) {
        const auto &name    = kv.first;
        const auto &lhsType = kv.second;

        auto it = other->fields_.find(name);
        if (it == other->fields_.end()) {
            return false;
        }
        if (!lhsType || !it->second || !lhsType->assignable(it->second)) {
            return false;
        }
    }
    return true;
}
