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
 * Updated: Oct. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "func.h"

#include "core/data/data.h"
#include "utils/str.h"

using namespace std;

FunctionType::FunctionType() : SpecialType(TypeCode::Func), exitType_(Type::Void()) {
    // 默认空构造函数，需要通过addWithArg和addNormArg添加参数
    // 通过此方式构造的FunctionType，是有编译信息的
    hasCompileInfo_ = true;
}

FunctionType::FunctionType(
    const param_init_list_t &withTypes, const param_init_list_t &normTypes,
    const type_ptr_t &returnType, const ModifierSet &modifiers)
    : SpecialType(TypeCode::Func), modifiers_(modifiers), withTypes_(withTypes),
      normTypes_(normTypes), exitType_(returnType) {}

FunctionType::FunctionType(
    const param_vec_t &&withTypes, const param_vec_t &&normTypes, const type_ptr_t &returnType,
    const ModifierSet &modifiers)
    : SpecialType(TypeCode::Func), modifiers_(modifiers), withTypes_(std::move(withTypes)),
      normTypes_(std::move(normTypes)), exitType_(returnType) {
    // 通过此方式构造的FunctionType，没有编译期信息
    hasCompileInfo_ = false;
}

const std::string &FunctionType::argNameAt(size_t idx) const {
    ASSERT(!argNames_.empty(), "No argument names available");
    ASSERT(idx < argNames_.size(), "Index out of range");
    return argNames_[idx];
}

bool FunctionType::addWithArg(const string &ident, const type_ptr_t type, bool isVar) {
    ASSERT(
        hasCompileInfo_,
        "Cannot add argument to FunctionType that is not constructed using default constructor");
    withTypes_.push_back({type, isVar});
    // 确保argsNames没有重复
    if (std::find(argNames_.begin(), argNames_.end(), ident) == argNames_.end()) {
        argNames_.push_back(ident);
    } else {
        return false;
    }
    return true;
}

bool FunctionType::addNormArg(const string &ident, const type_ptr_t type, bool isVar) {
    ASSERT(
        hasCompileInfo_,
        "Cannot add argument to FunctionType that is not constructed using default constructor");
    normTypes_.push_back({type, isVar});
    // 确保argsNames没有重复
    if (std::find(argNames_.begin(), argNames_.end(), ident) == argNames_.end()) {
        argNames_.push_back(ident);
    } else {
        return false;
    }
    return true;
}

bool FunctionType::hasSideEffect() const { return hasSideEffect_; }

type_ptr_t FunctionType::exitType() const { return dynamic_pointer_cast<Type>(exitType_); }

bool FunctionType::checkModifiers() const { return true; }

std::vector<std::tuple<std::string, type_ptr_t, bool>> FunctionType::withArgsInfo() const {
    ASSERT(hasCompileInfo_, "No compile info available");
    ASSERT(
        withTypes_.size() + normTypes_.size() == argNames_.size(),
        "Argument names size mismatch");
    std::vector<std::tuple<std::string, type_ptr_t, bool>> result;
    for (size_t i = 0; i < withTypes_.size(); i++) {
        const auto &type = withTypes_[i];
        const auto &name = argNames_.at(i);
        result.emplace_back(name, type.first, type.second);
    }
    return result;
}

std::vector<std::tuple<std::string, type_ptr_t, bool>> FunctionType::normArgsInfo() const {
    ASSERT(hasCompileInfo_, "No compile info available");
    ASSERT(
        withTypes_.size() + normTypes_.size() == argNames_.size(),
        "Argument names size mismatch");
    std::vector<std::tuple<std::string, type_ptr_t, bool>> result;
    for (size_t i = 0; i < normTypes_.size(); i++) {
        size_t idx = i + withTypes_.size();
        const auto &type = normTypes_[i];
        const auto &name = argNames_.at(idx);
        result.emplace_back(name, type.first, type.second);
    }
    return result;
}

string FunctionType::toString() const {
    string result;
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += string(modifiers_) + " ";
    }
    if (withTypes_.size() > 0) {
        result += "<";
        for (size_t i = 0; i < withTypes_.size(); i++) {
            const auto &type = withTypes_[i];
            if (hasCompileInfo_) {
                const auto &name = argNames_.at(i);
                result += name + ": ";
            }
            if (type.second) {
                result += "var ";
            }
            result += type.first->toString() + ", ";
        }
        result += "> ";
    }
    result += "(";
    for (size_t i = 0; i < normTypes_.size(); i++) {
        const auto &type = normTypes_[i];
        if (hasCompileInfo_) {
            size_t idx = i + withTypes_.size();
            ASSERT(idx < argNames_.size(), "Argument name index out of range");
            const auto &name = argNames_.at(idx);
            result += name + ": ";
        }
        if (type.second) {
            result += "var ";
        }
        result += type.first->toString();
    }
    result += ") => ";
    if (exitType_) {
        result += exitType_->toString();
    } else {
        result += "NULL";
    }
    return result;
}

bool FunctionType::operator==(const Type &other) const {
    if (other.code() != TypeCode::Func) {
        return false;
    }
    const FunctionType &otherFunctor = dynamic_cast<const FunctionType &>(other);
    for (size_t i = 0; i < withTypes_.size(); i++) {
        if (i >= otherFunctor.withTypes_.size()) {
            return false;
        }
        const auto &[type, isVar] = withTypes_[i];
        const auto &[otherType, otherIsVar] = otherFunctor.withTypes_[i];
        if (isVar != otherIsVar || !type->equals(otherType)) {
            return false;
        }
    }
    for (size_t i = 0; i < normTypes_.size(); i++) {
        if (i >= otherFunctor.normTypes_.size()) {
            return false;
        }
        const auto &[type, isVar] = normTypes_[i];
        const auto &[otherType, otherIsVar] = otherFunctor.normTypes_[i];
        if (isVar != otherIsVar || !type->equals(otherType)) {
            return false;
        }
    }
    if (exitType_ != nullptr && !exitType_->equals(otherFunctor.exitType_)) {
        return false;
    }
    return true;
}

bool FunctionType::operator!=(const Type &other) const { return !(*this == other); }
