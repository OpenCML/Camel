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
 * Updated: Oct. 27, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "special.h"

enum class FunctorModifier {
    INNER = 0b00000001,
    OUTER = 0b00000010,
    ATOMIC = 0b00000100,
    SHARED = 0b00001000,
    SYNC = 0b00010000,
    MACRO = 0b00100000,
};

FunctorModifier str2modifier(const std::string &str);
std::string modifier2str(FunctorModifier modifier);

class FunctorType : public SpecialType {
  private:
    std::string name_;
    std::unordered_set<FunctorModifier> modifiers_;
    std::shared_ptr<ParamsType> withType_;
    std::shared_ptr<ParamsType> linkType_;
    std::unordered_map<std::string, bool> variableMap_;
    bool hasSideEffect_ = false;
    type_ptr_t returnType_;

  public:
    FunctorType() = delete;
    FunctorType(const std::string &&name, const std::shared_ptr<ParamsType> &withType,
                const std::shared_ptr<ParamsType> &paramsType, const type_ptr_t &returnType);

    const std::string &name() const { return name_; }
    const std::string &argNameAt(size_t idx) const;

    void addModifier(FunctorModifier modifier);
    void setModifiers(const std::unordered_set<FunctorModifier> &modifiers);

    void checkModifiers() const; // throws exception

    bool sync() const { return modifiers_.find(FunctorModifier::SYNC) != modifiers_.end(); }
    bool shared() const { return modifiers_.find(FunctorModifier::SHARED) != modifiers_.end(); }
    bool atomic() const { return modifiers_.find(FunctorModifier::ATOMIC) != modifiers_.end(); }
    bool inner() const { return modifiers_.find(FunctorModifier::INNER) != modifiers_.end(); }
    bool outer() const { return modifiers_.find(FunctorModifier::OUTER) != modifiers_.end(); }
    bool macro() const { return modifiers_.find(FunctorModifier::MACRO) != modifiers_.end(); }

    bool addIdent(const std::string &ident, bool isVar);
    bool hasSideEffect() const;

    type_ptr_t withType() const;
    type_ptr_t linkType() const;
    type_ptr_t returnType() const;

    const std::unordered_map<std::string, bool> &variableMap() const { return variableMap_; }

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

using func_type_ptr_t = std::shared_ptr<FunctorType>;
