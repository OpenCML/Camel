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
 * Created: Oct. 06, 2024
 * Updated: Oct. 19, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "functor.h"

#include "../../data.h"
#include "../struct/params.h"

using namespace std;

FunctorModifier str2modifier(const string &str) {
    if (str == "inner") {
        return FunctorModifier::INNER;
    } else if (str == "outer") {
        return FunctorModifier::OUTER;
    } else if (str == "atomic") {
        return FunctorModifier::ATOMIC;
    } else if (str == "shared") {
        return FunctorModifier::SHARED;
    } else if (str == "sync") {
        return FunctorModifier::SYNC;
    } else {
        throw runtime_error("Unknown modifier: " + str);
    }
}

string modifier2str(FunctorModifier modifier) {
    switch (modifier) {
    case FunctorModifier::INNER:
        return "inner";
    case FunctorModifier::OUTER:
        return "outer";
    case FunctorModifier::ATOMIC:
        return "atomic";
    case FunctorModifier::SHARED:
        return "shared";
    case FunctorModifier::SYNC:
        return "sync";
    default:
        throw runtime_error("Unknown modifier: " + to_string(static_cast<int>(modifier)));
    }
}

FunctorType::FunctorType(const std::string &&name, const shared_ptr<ParamsType> &withType,
                         const shared_ptr<ParamsType> &paramsType, const type_ptr_t &returnType)
    : SpecialType(TypeCode::FUNCTOR), name_(std::move(name)), withType_(withType), linkType_(paramsType),
      returnType_(returnType) {}

void FunctorType::addModifier(FunctorModifier modifier) { modifiers_.insert(modifier); }

void FunctorType::setModifiers(const unordered_set<FunctorModifier> &modifiers) { modifiers_ = modifiers; }

void FunctorType::checkModifiers() const {
    if (hasSideEffect_ && !sync()) {
        throw runtime_error("Functor with side effect must be sync.");
    }
    if (inner() && outer()) {
        throw runtime_error("Functor cannot be both inner and outer.");
    }
}

bool FunctorType::addIdent(const string &ident, bool isVar) {
    if (variableMap_.find(ident) != variableMap_.end()) {
        return false;
    }
    variableMap_.insert({ident, isVar});
    if (isVar) {
        hasSideEffect_ = true;
    }
    return true;
}

bool FunctorType::hasSideEffect() const { return hasSideEffect_; }

type_ptr_t FunctorType::withType() const { return dynamic_pointer_cast<Type>(withType_); }

type_ptr_t FunctorType::linkType() const { return dynamic_pointer_cast<Type>(linkType_); }

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
    if (linkType_ && linkType_->size() > 0) {
        const auto &params = dynamic_cast<const ParamsType &>(*linkType_);
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
    if (linkType_ != nullptr && !linkType_->equals(otherFunctor.linkType_)) {
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
