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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include "core/func.h"
#include "core/impl.h"

using param_t           = std::pair<Type *, bool>; // bool表示是否为可变参数
using param_init_list_t = std::initializer_list<param_t>;
using param_vec_t       = std::vector<param_t>;

class FunctionType;

class FunctionType : public CompositeType {
  private:
    ImplMark implMark_     = ImplMark::Graph;
    ModifierSet modifiers_ = Modifier::None;

    param_vec_t withTypes_;
    param_vec_t normTypes_;
    Type *exitType_ = nullptr;

    // 只在编译期记录并使用
    bool hasCompileInfo_ = true;
    std::vector<std::string> argNames_;
    std::vector<std::string> closureRefs_;

  public:
    FunctionType();
    FunctionType(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    FunctionType(
        const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    FunctionType(
        const param_vec_t &&withTypes, const param_vec_t &&normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);

    virtual ~FunctionType() = default;

    static FunctionType *create();
    static FunctionType *create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    static FunctionType *create(
        const param_vec_t &withTypes, const param_vec_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);
    static FunctionType *create(
        const param_vec_t &&withTypes, const param_vec_t &&normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None);

    ImplMark implMark() const { return implMark_; }
    void setImplMark(ImplMark mark) { implMark_ = mark; }

    const ModifierSet &modifiers() const { return modifiers_; }
    void setModifiers(const ModifierSet &mod) { modifiers_ = mod; }
    bool hasModifier(Modifier mod) const { return modifiers_.has(mod); }
    bool checkModifiers() const;

    // 供编译期由GCT构造使用
    bool hasCompileInfo() const { return hasCompileInfo_; }
    bool addWithArg(const std::string &ident, Type *type, bool isVar);
    bool addNormArg(const std::string &ident, Type *type, bool isVar);
    bool addClosureRef(const std::string &ident);

    const param_vec_t &withTypes() const { return withTypes_; }
    const param_vec_t &normTypes() const { return normTypes_; }
    const std::vector<std::string> &closureRefs() const { return closureRefs_; }

    void setExitType(Type *type) { exitType_ = type; }
    Type *exitType() const;
    bool hasExitType() const { return exitType_ != nullptr; }

    const std::string &argNameAt(size_t idx) const;
    void setArgNames(const std::vector<std::string> &names) { argNames_ = names; }
    const std::vector<std::string> &argNames() const { return argNames_; }
    std::vector<std::tuple<std::string, Type *, bool>> withArgsInfo() const;
    std::vector<std::tuple<std::string, Type *, bool>> normArgsInfo() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;
};
