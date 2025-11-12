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
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include "core/func.h"
#include "core/impl.h"

using param_t           = std::pair<type_ptr_t, bool>; // bool表示是否为可变参数
using param_init_list_t = std::initializer_list<param_t>;
using param_vec_t       = std::vector<param_t>;

class FunctionType;
using func_type_ptr_t = std::shared_ptr<FunctionType>;

class FunctionType : public CompositeType {
  private:
    ImplMark implMark_     = ImplMark::Graph;
    ModifierSet modifiers_ = Modifier::None;

    param_vec_t withTypes_;
    param_vec_t normTypes_;
    type_ptr_t exitType_ = nullptr;

    // 只在编译期记录并使用
    bool hasCompileInfo_ = true;
    std::vector<std::string> argNames_;
    std::vector<std::string> closureRefs_;

  public:
    FunctionType();
    FunctionType(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes,
        const type_ptr_t &returnType, const ModifierSet &modifiers = Modifier::None);
    FunctionType(
        const param_vec_t &withTypes, const param_vec_t &normTypes, const type_ptr_t &returnType,
        const ModifierSet &modifiers = Modifier::None);
    FunctionType(
        const param_vec_t &&withTypes, const param_vec_t &&normTypes, const type_ptr_t &returnType,
        const ModifierSet &modifiers = Modifier::None);

    virtual ~FunctionType() = default;

    static func_type_ptr_t create() {
        return std::make_shared<FunctionType>(
            param_vec_t{},
            param_vec_t{},
            Type::Void(),
            Modifier::None);
    }
    static func_type_ptr_t create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes,
        const type_ptr_t &returnType, const ModifierSet &modifiers = Modifier::None) {
        return std::make_shared<FunctionType>(withTypes, normTypes, returnType, modifiers);
    }
    static func_type_ptr_t create(
        const param_vec_t &withTypes, const param_vec_t &normTypes, const type_ptr_t &returnType,
        const ModifierSet &modifiers = Modifier::None) {
        return std::make_shared<FunctionType>(withTypes, normTypes, returnType, modifiers);
    }
    static func_type_ptr_t create(
        const param_vec_t &&withTypes, const param_vec_t &&normTypes, const type_ptr_t &returnType,
        const ModifierSet &modifiers = Modifier::None) {
        return std::make_shared<FunctionType>(
            std::move(withTypes),
            std::move(normTypes),
            returnType,
            modifiers);
    }

    ImplMark implMark() const { return implMark_; }
    void setImplMark(ImplMark mark) { implMark_ = mark; }

    const ModifierSet &modifiers() const { return modifiers_; }
    void setModifiers(const ModifierSet &mod) { modifiers_ = mod; }
    bool hasModifier(Modifier mod) const { return modifiers_.has(mod); }
    bool checkModifiers() const;

    // 供编译期由GCT构造使用
    bool hasCompileInfo() const { return hasCompileInfo_; }
    bool addWithArg(const std::string &ident, const type_ptr_t type, bool isVar);
    bool addNormArg(const std::string &ident, const type_ptr_t type, bool isVar);
    bool addClosureRef(const std::string &ident);

    const param_vec_t &withTypes() const { return withTypes_; }
    const param_vec_t &normTypes() const { return normTypes_; }
    const std::vector<std::string> &closureRefs() const { return closureRefs_; }

    void setExitType(const type_ptr_t &type) { exitType_ = type; }
    type_ptr_t exitType() const;
    bool hasExitType() const { return exitType_ != nullptr; }

    const std::string &argNameAt(size_t idx) const;
    void setArgNames(const std::vector<std::string> &names) { argNames_ = names; }
    const std::vector<std::string> &argNames() const { return argNames_; }
    std::vector<std::tuple<std::string, type_ptr_t, bool>> withArgsInfo() const;
    std::vector<std::tuple<std::string, type_ptr_t, bool>> normArgsInfo() const;

    virtual type_ptr_t resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone() const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
