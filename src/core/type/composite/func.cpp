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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "func.h"

#include "core/data/data.h"
#include "utils/str.h"

#include <algorithm>

using namespace std;

FunctionType::FunctionType()
    : CompositeType(TypeCode::Function), exitType_(nullptr), hasCompileInfo_(true) {
    // 默认空构造函数，需要通过addWithArg和addNormArg添加参数
    // 通过此方式构造的FunctionType，是有编译信息的
}

FunctionType::FunctionType(
    const param_init_list_t &withTypes, const param_init_list_t &normTypes,
    const type_ptr_t &returnType, const ModifierSet &modifiers)
    : CompositeType(TypeCode::Function), implMark_(ImplMark::Graph), modifiers_(modifiers),
      withTypes_(withTypes), normTypes_(normTypes), exitType_(returnType), hasCompileInfo_(false) {}

FunctionType::FunctionType(
    const param_vec_t &withTypes, const param_vec_t &normTypes, const type_ptr_t &returnType,
    const ModifierSet &modifiers)
    : CompositeType(TypeCode::Function), implMark_(ImplMark::Graph), modifiers_(modifiers),
      withTypes_(withTypes), normTypes_(normTypes), exitType_(returnType), hasCompileInfo_(false) {}

FunctionType::FunctionType(
    const param_vec_t &&withTypes, const param_vec_t &&normTypes, const type_ptr_t &returnType,
    const ModifierSet &modifiers)
    : CompositeType(TypeCode::Function), implMark_(ImplMark::Graph), modifiers_(modifiers),
      withTypes_(std::move(withTypes)), normTypes_(std::move(normTypes)), exitType_(returnType),
      hasCompileInfo_(false) {}

const string &FunctionType::argNameAt(size_t idx) const {
    ASSERT(!argNames_.empty(), "No argument names available");
    ASSERT(idx < argNames_.size(), "Index out of range");
    return argNames_[idx];
}

bool FunctionType::addWithArg(const string &ident, const type_ptr_t type, bool isVar) {
    ASSERT(hasCompileInfo_, "Cannot add argument to non-compile-info FunctionType");
    if (find(argNames_.begin(), argNames_.end(), ident) != argNames_.end()) {
        return false;
    }
    withTypes_.push_back({type, isVar});
    argNames_.push_back(ident);
    return true;
}

bool FunctionType::addNormArg(const string &ident, const type_ptr_t type, bool isVar) {
    ASSERT(hasCompileInfo_, "Cannot add argument to non-compile-info FunctionType");
    if (find(argNames_.begin(), argNames_.end(), ident) != argNames_.end()) {
        return false;
    }
    normTypes_.push_back({type, isVar});
    argNames_.push_back(ident);
    return true;
}

bool FunctionType::addClosureRef(const string &ident) {
    ASSERT(hasCompileInfo_, "Cannot add closure ref to non-compile-info FunctionType");
    closureRefs_.push_back(ident);
    return true;
}

type_ptr_t FunctionType::exitType() const {
    // 如果没有返回值类型，默认为void
    // 但此时返回值仍然是未设置状态，以便编译器进行类型推导
    return exitType_ ? exitType_ : Type::Void();
}

bool FunctionType::checkModifiers() const { return true; }

vector<tuple<string, type_ptr_t, bool>> FunctionType::withArgsInfo() const {
    ASSERT(hasCompileInfo_, "No compile info available");
    ASSERT(
        withTypes_.size() + normTypes_.size() == argNames_.size(),
        "Argument names size mismatch");
    vector<tuple<string, type_ptr_t, bool>> result;
    for (size_t i = 0; i < withTypes_.size(); i++) {
        result.emplace_back(argNames_[i], withTypes_[i].first, withTypes_[i].second);
    }
    return result;
}

vector<tuple<string, type_ptr_t, bool>> FunctionType::normArgsInfo() const {
    ASSERT(hasCompileInfo_, "No compile info available");
    ASSERT(
        withTypes_.size() + normTypes_.size() == argNames_.size(),
        "Argument names size mismatch");
    vector<tuple<string, type_ptr_t, bool>> result;
    for (size_t i = 0; i < normTypes_.size(); i++) {
        size_t idx = i + withTypes_.size();
        result.emplace_back(argNames_[idx], normTypes_[i].first, normTypes_[i].second);
    }
    return result;
}

type_ptr_t FunctionType::resolve(const type_vec_t &typeList) const {
    ASSERT(false, "Not implemented");
    return nullptr;
}

bool FunctionType::resolved() const { return true; }

std::optional<type_ptr_t> FunctionType::typeAt(size_t idx) const {
    ASSERT(false, "FunctionType does not support typeAt()");
    return std::nullopt;
}

string FunctionType::toString() const {
    string result;
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += string(modifiers_) + " ";
    }
    if (!withTypes_.empty()) {
        result += "<";
        for (size_t i = 0; i < withTypes_.size(); i++) {
            if (i > 0)
                result += ", ";
            if (withTypes_[i].second)
                result += "var ";
            if (hasCompileInfo_)
                result += argNames_[i] + ": ";
            result += withTypes_[i].first->toString();
        }
        result += "> ";
    }
    result += "(";
    for (size_t i = 0; i < normTypes_.size(); i++) {
        if (i > 0)
            result += ", ";
        if (normTypes_[i].second)
            result += "var ";
        if (hasCompileInfo_) {
            size_t idx = i + withTypes_.size();
            result += argNames_[idx] + ": ";
        }
        result += normTypes_[i].first->toString();
    }
    result += ") => ";
    result += exitType_ ? exitType_->toString() : "<null>";
    return result;
}

string FunctionType::mangle() const {
    string result = "F";
    if (!withTypes_.empty()) {
        result += "W" + to_string(withTypes_.size());
        for (const auto &[type, isVar] : withTypes_) {
            if (isVar)
                result += "V";
            result += type->mangle();
        }
    }
    if (!normTypes_.empty()) {
        result += "N" + to_string(normTypes_.size());
        for (const auto &[type, isVar] : normTypes_) {
            if (isVar)
                result += "V";
            result += type->mangle();
        }
    }
    result += "R";
    result += exitType_ ? exitType_->mangle() : "v";
    return result;
}

type_ptr_t FunctionType::clone(bool deep /* = false */) const {
    auto res             = std::make_shared<FunctionType>();
    res->implMark_       = implMark_;
    res->modifiers_      = modifiers_;
    res->withTypes_      = withTypes_;
    res->normTypes_      = normTypes_;
    res->exitType_       = exitType_;
    res->argNames_       = argNames_;
    res->closureRefs_    = closureRefs_;
    res->hasCompileInfo_ = hasCompileInfo_;
    return res;
}

bool FunctionType::equals(const type_ptr_t &other) const {
    if (this == other.get())
        return true;
    if (other->code() != TypeCode::Function)
        return false;

    const auto &otherFunc = static_cast<const FunctionType &>(*other);

    if (withTypes_.size() != otherFunc.withTypes_.size() ||
        normTypes_.size() != otherFunc.normTypes_.size()) {
        return false;
    }
    for (size_t i = 0; i < withTypes_.size(); i++) {
        const auto &[type, isVar]           = withTypes_[i];
        const auto &[otherType, otherIsVar] = otherFunc.withTypes_[i];
        if (isVar != otherIsVar || !type->equals(otherType))
            return false;
    }
    for (size_t i = 0; i < normTypes_.size(); i++) {
        const auto &[type, isVar]           = normTypes_[i];
        const auto &[otherType, otherIsVar] = otherFunc.normTypes_[i];
        if (isVar != otherIsVar || !type->equals(otherType))
            return false;
    }
    if (exitType_ && otherFunc.exitType_) {
        if (!exitType_->equals(otherFunc.exitType_))
            return false;
    } else if (exitType_ || otherFunc.exitType_) {
        return false;
    }
    return true;
}

CastSafety FunctionType::castSafetyTo(const Type &other) const {
    if (this == &other)
        return CastSafety::Safe;
    return CastSafety::Forbidden;
}

bool FunctionType::assignable(const type_ptr_t &type) const { return equals(type); }
