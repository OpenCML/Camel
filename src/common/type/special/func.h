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

#pragma once

#include "common/func.h"
#include "common/impl.h"
#include "special.h"

class FunctionType : public SpecialType {
  public:
    FunctionType() = delete;
    FunctionType(const std::shared_ptr<ParamsType> &withType, const std::shared_ptr<ParamsType> &paramsType,
                 const type_ptr_t &returnType);

    const std::string &argNameAt(size_t idx) const;

    ImplMark implMark() const { return implMark_; }
    void setImplMark(ImplMark mark) { implMark_ = mark; }

    const std::string &uri() const { return uri_; }
    void setUri(const std::string &uri) { uri_ = uri; }

    const ModifierSet &modifiers() const { return modifiers_; }
    void setModifiers(const ModifierSet &mod) { modifiers_ = mod; }

    bool checkModifiers() const;

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

  private:
    ImplMark implMark_ = ImplMark::Graph;
    std::string uri_;
    ModifierSet modifiers_ = Modifier::None;
    std::shared_ptr<ParamsType> withType_;
    std::shared_ptr<ParamsType> linkType_;
    std::unordered_map<std::string, bool> variableMap_;
    bool hasSideEffect_ = false;
    type_ptr_t returnType_;
};

using func_type_ptr_t = std::shared_ptr<FunctionType>;
