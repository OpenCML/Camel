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
 * Updated: Dec. 12, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "func.h"

#include "../../data.h"
#include "../struct/params.h"
#include "utils/str.h"

using namespace std;

FunctionType::FunctionType()
    : SpecialType(TypeCode::FUNC), withParamsType_(make_shared<ParamsType>()),
      normParamsType_(make_shared<ParamsType>()), returnType_(voidTypePtr) {}

FunctionType::FunctionType(
    const shared_ptr<ParamsType> &withParamsType, const shared_ptr<ParamsType> &paramsType,
    const type_ptr_t &returnType)
    : SpecialType(TypeCode::FUNC), withParamsType_(withParamsType), normParamsType_(paramsType),
      returnType_(returnType) {}

FunctionType::FunctionType(
    const param_init_list &withParamsList, const param_init_list &normParamsList,
    const type_ptr_t &returnType)
    : SpecialType(TypeCode::FUNC) {
    withParamsType_ = make_shared<ParamsType>();
    normParamsType_ = make_shared<ParamsType>();
    for (const auto &tuple : withParamsList) {
        const auto &[name, type, value, isVar] = tuple;
        withParamsType_->add(name, type, value);
        variableMap_.insert({name, isVar});
    }
    for (const auto &tuple : normParamsList) {
        const auto &[name, type, value, isVar] = tuple;
        normParamsType_->add(name, type, value);
        variableMap_.insert({name, isVar});
    }
}

const std::string &FunctionType::argNameAt(size_t idx) const {
    if (idx < withParamsType_->size())
        return std::get<0>(withParamsType_->elementAt(idx));
    else
        return std::get<0>(normParamsType_->elementAt(idx - withParamsType_->size()));
}

bool FunctionType::addIdent(const string &ident, bool isVar) {
    if (variableMap_.find(ident) != variableMap_.end()) {
        return false;
    }
    variableMap_.insert({ident, isVar});
    if (isVar) {
        hasSideEffect_ = true;
    }
    return true;
}

bool FunctionType::hasSideEffect() const { return hasSideEffect_; }

type_ptr_t FunctionType::withParamsType() const {
    return dynamic_pointer_cast<Type>(withParamsType_);
}

type_ptr_t FunctionType::normParamsType() const {
    return dynamic_pointer_cast<Type>(normParamsType_);
}

type_ptr_t FunctionType::returnType() const { return dynamic_pointer_cast<Type>(returnType_); }

std::vector<std::tuple<std::string, type_ptr_t, bool>> FunctionType::withParams() const {
    std::vector<std::tuple<std::string, type_ptr_t, bool>> result;
    if (withParamsType_) {
        const auto &elements = withParamsType_->elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, _] = tuple;
            result.emplace_back(name, type, variableMap_.count(name) && variableMap_.at(name));
        }
    }
    return result;
}

std::vector<std::tuple<std::string, type_ptr_t, bool>> FunctionType::normParams() const {
    std::vector<std::tuple<std::string, type_ptr_t, bool>> result;
    if (normParamsType_) {
        const auto &elements = normParamsType_->elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, _] = tuple;
            result.emplace_back(name, type, variableMap_.count(name) && variableMap_.at(name));
        }
    }
    return result;
}

bool FunctionType::checkModifiers() const { return true; }

string FunctionType::toString() const {
    string result;
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += string(modifiers_) + " ";
    }
    if (withParamsType_ && withParamsType_->size() > 0) {
        result += "<";
        const auto &with = dynamic_cast<const ParamsType &>(*withParamsType_);
        const auto &elements = with.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
            if (variableMap_.count(name) && variableMap_.at(name)) {
                result += "var ";
            }
            result += name + ": " + type->toString();
            if (value) {
                result += " = " + value->toString();
            }
            result += ", ";
        }
        if (elements.size() > 0) {
            result.pop_back();
            result.pop_back();
        }
        result += "> ";
    }
    result += "(";
    if (normParamsType_ && normParamsType_->size() > 0) {
        const auto &params = dynamic_cast<const ParamsType &>(*normParamsType_);
        const auto &elements = params.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
            if (variableMap_.count(name) && variableMap_.at(name)) {
                result += "var ";
            }
            result += name + ": " + type->toString();
            if (value) {
                result += " = " + value->toString();
            }
            result += ", ";
        }
        if (elements.size() > 0) {
            result.pop_back();
            result.pop_back();
        }
    }
    result += ") => ";
    if (returnType_) {
        result += returnType_->toString();
    } else {
        result += "NULL";
    }
    return result;
}

bool FunctionType::operator==(const Type &other) const {
    if (other.code() != TypeCode::FUNC) {
        return false;
    }
    const FunctionType &otherFunctor = dynamic_cast<const FunctionType &>(other);
    if (withParamsType_ != nullptr && !withParamsType_->equals(otherFunctor.withParamsType_)) {
        return false;
    }
    if (normParamsType_ != nullptr && !normParamsType_->equals(otherFunctor.normParamsType_)) {
        return false;
    }
    if (returnType_ != nullptr && !returnType_->equals(otherFunctor.returnType_)) {
        return false;
    }
    return true;
}

bool FunctionType::operator!=(const Type &other) const { return !(*this == other); }

TypeConv FunctionType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    if (other.code() == TypeCode::FUNC) {
        TypeConv result = TypeConv::SAFE;
        const FunctionType &otherFunctor = dynamic_cast<const FunctionType &>(other);
        if (withParamsType_ && !otherFunctor.withParamsType_) {
            const TypeConv withTypeConv =
                withParamsType_->convertibility(*otherFunctor.withParamsType_);
            if (withTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (withTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (normParamsType_ && !otherFunctor.normParamsType_) {
            const TypeConv paramsTypeConv =
                normParamsType_->convertibility(*otherFunctor.normParamsType_);
            if (paramsTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (paramsTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (returnType_ && !otherFunctor.returnType_) {
            const TypeConv returnTypeConv = returnType_->convertibility(*otherFunctor.returnType_);
            if (returnTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (returnTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        return result;
    }
    if (other.code() == TypeCode::ANY) {
        return TypeConv::SAFE;
    }
    // primary types and special types are forbidden
    return TypeConv::FORBIDDEN;
}
