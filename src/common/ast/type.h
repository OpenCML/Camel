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

enum class TypeType { Expr, List, Dict, Tuple, Func, Spec, Unit, Infer, Data, Ref };

enum class TypeOp { Union, Inter, Diff, KeyUnion, KeyInter, KeyDiff, ErrorThen, Specialize, TypeOf, TypeAs };

std::string typeTypeToString(TypeType type);

std::string typeOpToString(TypeOp op);

class TypeLoad : public Load {
  public:
    TypeLoad(TypeType type) : Load(LoadType::Type), typeType_(type) {}
    const std::string toString() const override { return "TypeLoad"; }

  private:
    TypeType typeType_;
};

class TypeExprLoad : public TypeLoad {
  public:
    TypeExprLoad(TypeOp op) : TypeLoad(TypeType::Expr), op_(op) {}
    const std::string toString() const override { return "TypeExpr: " + typeOpToString(op_); }

  private:
    TypeOp op_;
};

class ListTypeLoad : public TypeLoad {
  public:
    ListTypeLoad(size_t dims) : TypeLoad(TypeType::List), dims_(dims) {}
    const std::string toString() const override { return "ListType: [" + std::to_string(dims_) + "]"; }

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
    void setAtomic(bool atomic) { atomic_ = atomic; }
    void setShared(bool shared) { shared_ = shared; }
    void setSync(bool sync) { sync_ = sync; }
    void setMacro(bool macro) { macro_ = macro; }
    bool isAtomic() const { return atomic_; }
    bool isShared() const { return shared_; }
    bool isSync() const { return sync_; }
    bool isMacro() const { return macro_; }
    void setImplMark(ImplMark implMark) { implMark_ = implMark; }

  private:
    bool atomic_ = false;
    bool shared_ = false;
    bool sync_ = false;
    bool macro_ = false;
    ImplMark implMark_ = ImplMark::Graph;
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

  private:
    Reference ref_;
};

} // namespace AbstractSyntaxTree