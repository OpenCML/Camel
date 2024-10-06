/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "functor.h"
#include "common/function.h"

using namespace std;

FunctorType::FunctorType(const shared_ptr<ParamsType> &withType = nullptr,
                         const shared_ptr<ParamsType> &paramsType = nullptr, const type_ptr_t &returnType = nullptr)
    : SpecialType(TypeCode::FUNCTOR), withType_(withType), paramsType_(paramsType), returnType_(returnType) {}

void FunctorType::addModifier(FunctionModifier modifier) { modifiers_.insert(modifier); }

void FunctorType::setModifiers(const unordered_set<FunctionModifier> &modifiers) { modifiers_ = modifiers; }

bool FunctorType::addIdent(const string &ident) {
    if (innerIdents_.find(ident) != innerIdents_.end()) {
        return false;
    }
    innerIdents_.insert(ident);
    return true;
}

bool FunctorType::hasSideEffect() const { return hasSideEffect_; }

const unordered_set<string> &FunctorType::innerIdents() const { return innerIdents_; }

type_ptr_t FunctorType::withType() const { return dynamic_pointer_cast<Type>(withType_); }

type_ptr_t FunctorType::paramsType() const { return dynamic_pointer_cast<Type>(paramsType_); }

type_ptr_t FunctorType::returnType() const { return dynamic_pointer_cast<Type>(returnType_); }

string FunctorType::toString() const {
    string result = "";
    for (const auto &modifier : modifiers_) {
        result += modifier2str(modifier) + " ";
    }
    if (withType_ && withType_->size() > 0) {
        result += "<";
        const auto &with = dynamic_cast<const ParamsType &>(*withType_);
        const auto &elements = with.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
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
    if (paramsType_ && paramsType_->size() > 0) {
        const auto &params = dynamic_cast<const ParamsType &>(*paramsType_);
        const auto &elements = params.elements();
        for (const auto &tuple : elements) {
            const auto &[name, type, value] = tuple;
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

bool FunctorType::operator==(const Type &other) const {
    if (other.code() != TypeCode::FUNCTOR) {
        return false;
    }
    const FunctorType &otherFunctor = dynamic_cast<const FunctorType &>(other);
    if (withType_ != nullptr && !withType_->equals(otherFunctor.withType_)) {
        return false;
    }
    if (paramsType_ != nullptr && !paramsType_->equals(otherFunctor.paramsType_)) {
        return false;
    }
    if (returnType_ != nullptr && !returnType_->equals(otherFunctor.returnType_)) {
        return false;
    }
    return true;
}

bool FunctorType::operator!=(const Type &other) const { return !(*this == other); }

TypeConv FunctorType::convertibility(const Type &other) const {
    // TODO: not fully implemented
    if (other.code() == TypeCode::FUNCTOR) {
        TypeConv result = TypeConv::SAFE;
        const FunctorType &otherFunctor = dynamic_cast<const FunctorType &>(other);
        if (withType_ && !otherFunctor.withType_) {
            const TypeConv withTypeConv = withType_->convertibility(*otherFunctor.withType_);
            if (withTypeConv == TypeConv::FORBIDDEN) {
                return TypeConv::FORBIDDEN;
            }
            if (withTypeConv == TypeConv::UNSAFE) {
                result = TypeConv::UNSAFE;
            }
        }
        if (paramsType_ && !otherFunctor.paramsType_) {
            const TypeConv paramsTypeConv = paramsType_->convertibility(*otherFunctor.paramsType_);
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
    // primitive types and special types are forbidden
    return TypeConv::FORBIDDEN;
}