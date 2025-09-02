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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <stdexcept>
#include <string>

#include "base.h"

namespace AbstractSyntaxTree {

enum class TypeType { Null, Expr, List, Dict, Tuple, Func, Spec, Unit, Infer, Data, Ref };

enum class TypeOp {
    Union,
    Inter,
    Diff,
    KeyUnion,
    KeyInter,
    KeyDiff,
    ErrorThen,
    Specialize,
    TypeOf,
    TypeAs
};

std::string to_string(TypeType type);
std::string to_string(TypeOp op);

class TypeLoad : public Load {
  public:
    TypeLoad(TypeType type) : Load(LoadType::Type), typeType_(type) {}
    const std::string toString() const override { return "TypeLoad"; }

    TypeType typeType() const { return typeType_; }

  private:
    TypeType typeType_;
};

class NullableTypeLoad : public TypeLoad {
  public:
    NullableTypeLoad() : TypeLoad(TypeType::Null) {}
    const std::string toString() const override { return "NullableType"; }
};

class TypeExprLoad : public TypeLoad {
  public:
    TypeExprLoad(TypeOp op) : TypeLoad(TypeType::Expr), op_(op) {}
    const std::string toString() const override { return "TypeExpr: " + to_string(op_); }

    TypeOp op() const { return op_; }

  private:
    TypeOp op_;
};

class ListTypeLoad : public TypeLoad {
  public:
    ListTypeLoad(size_t dims) : TypeLoad(TypeType::List), dims_(dims) {}
    const std::string toString() const override {
        return "ListType: [" + std::to_string(dims_) + "]";
    }

    size_t dims() const { return dims_; }

  private:
    size_t dims_ = 0;
};

class DictTypeLoad : public TypeLoad {
  public:
    DictTypeLoad() : TypeLoad(TypeType::Dict) {}
    const std::string toString() const override { return "DictType"; }
};

class TupleTypeLoad : public TypeLoad {
  public:
    TupleTypeLoad() : TypeLoad(TypeType::Tuple) {}
    const std::string toString() const override { return "TupleType"; }
};

class FuncTypeLoad : public TypeLoad {
  public:
    FuncTypeLoad() : TypeLoad(TypeType::Func) {}
    FuncTypeLoad(const std::string &uri, ImplMark impl = ImplMark::Graph)
        : TypeLoad(TypeType::Func), implMark_(impl), uri_(uri) {}
    const std::string toString() const override;

    const std::string uri() const { return uri_; }
    void setUri(const std::string &uri) { uri_ = uri; }
    const ModifierSet &modifiers() const { return modifiers_; }
    void setModifiers(const ModifierSet &modifiers) { modifiers_ = modifiers; }
    ImplMark implMark() const { return implMark_; }
    void setImplMark(ImplMark implMark) { implMark_ = implMark; }

  private:
    ImplMark implMark_ = ImplMark::Graph;
    ModifierSet modifiers_;
    std::string uri_;
};

class UnitTypeLoad : public TypeLoad {
  public:
    UnitTypeLoad(Reference ref) : TypeLoad(TypeType::Unit), ref_(ref) {}
    const std::string toString() const override { return "UnitType"; }

  private:
    Reference ref_;
};

class InferTypeLoad : public TypeLoad {
  public:
    InferTypeLoad(Reference ref) : TypeLoad(TypeType::Infer), ref_(ref) {}
    const std::string toString() const override { return "InferType"; }

  private:
    Reference ref_;
};

class DataTypeLoad : public TypeLoad {
  public:
    DataTypeLoad(Reference ref) : TypeLoad(TypeType::Data), ref_(ref) {}
    const std::string toString() const override { return "DataType: " + ref_.toString(); }

  private:
    Reference ref_;
};

class RefTypeLoad : public TypeLoad {
  public:
    RefTypeLoad(Reference ref) : TypeLoad(TypeType::Ref), ref_(ref) {}
    const std::string toString() const override { return "RefType: " + ref_.toString(); }

    const Reference &ref() const { return ref_; }

  private:
    Reference ref_;
};

} // namespace AbstractSyntaxTree