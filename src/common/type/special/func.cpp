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

FunctionType::FunctionType(const shared_ptr<ParamsType> &withType, const shared_ptr<ParamsType> &paramsType,
                           const type_ptr_t &returnType)
    : SpecialType(TypeCode::FUNCTOR), withType_(withType), linkType_(paramsType), returnType_(returnType) {}

const std::string &FunctionType::argNameAt(size_t idx) const {
    if (idx < withType_->size())
        return std::get<0>(withType_->elementAt(idx));
    else
        return std::get<0>(linkType_->elementAt(idx - withType_->size()));
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

type_ptr_t FunctionType::withType() const { return dynamic_pointer_cast<Type>(withType_); }

type_ptr_t FunctionType::linkType() const { return dynamic_pointer_cast<Type>(linkType_); }

type_ptr_t FunctionType::returnType() const { return dynamic_pointer_cast<Type>(returnType_); }

bool FunctionType::checkModifiers() const { return true; }

string FunctionType::toString() const {
    string result;
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += string(modifiers_) + " ";
    }
    if (withType_ && withType_->size() > 0) {
        result += "<";
        const auto &with = dynamic_cast<const ParamsType &>(*withType_);
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
    if (linkType_ && linkType_->size() > 0) {
        const auto &params = dynamic_cast<const ParamsType &>(*linkType_);
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
    if (other.code() != TypeCode::FUNCTOR) {
        return false;
    }
    const FunctionType &otherFunctor = dynamic_cast<const FunctionType &>(other);
    if (withType_ != nullptr && !withType_->equals(otherFunctor.withType_)) {
        return false;
    }
    if (linkType_ != nullptr && !linkType_->equals(otherFunctor.linkType_)) {
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
    if (other.code() == TypeCode::FUNCTOR) {
        TypeConv result = TypeConv::SAFE;
        const FunctionType &otherFunctor = dynamic_cast<const FunctionType &>(other);
        if (withType_ && !otherFunctor.withType_) {
            const TypeConv withTypeConv = withType_->convertibility(*otherFunctor.withType_);
            if (withTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (withTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (linkType_ && !otherFunctor.linkType_) {
            const TypeConv paramsTypeConv = linkType_->convertibility(*otherFunctor.linkType_);
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
