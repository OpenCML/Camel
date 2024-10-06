/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "special.h"

class FunctorType : public SpecialType {
  private:
    std::unordered_set<FunctionModifier> modifiers_;
    std::shared_ptr<ParamsType> withType_;
    std::shared_ptr<ParamsType> paramsType_;
    std::unordered_set<std::string> innerIdents_;
    bool hasSideEffect_ = false;
    type_ptr_t returnType_;

  public:
    FunctorType() = delete;
    FunctorType(const std::shared_ptr<ParamsType> &withType = nullptr,
                const std::shared_ptr<ParamsType> &paramsType = nullptr, const type_ptr_t &returnType = nullptr);

    void addModifier(FunctionModifier modifier);
    void setModifiers(const std::unordered_set<FunctionModifier> &modifiers);

    bool addIdent(const std::string &ident);
    bool hasSideEffect() const;
    const std::unordered_set<std::string> &innerIdents() const;

    type_ptr_t withType() const;
    type_ptr_t paramsType() const;
    type_ptr_t returnType() const;

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};