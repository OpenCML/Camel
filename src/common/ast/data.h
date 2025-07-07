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

#include "base.h"

namespace AbstractSyntaxTree {

enum class DataType { DataExpr, IfExpr, MatchExpr, TryExpr, UnaryExpr, Literal, List, Dict, Tuple, Index, Func, Ref };

enum class DataOp {
    Assign,
    AssignAdd,
    AssignSub,
    AssignMul,
    AssignDiv,
    AssignMod,
    AssignMat,
    AssignExp,
    AssignAnd,
    AssignOr,
    Or,
    And,
    Eq,
    Neq,
    StrictEq,
    StrictNeq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Mat,
    Exp,
    NullThen,
    ErrorThen,
    NotNullThen,
    Call,
    With,
    Bind,
    As,
    Is,
    Not,
    Neg,
    Inv
};

std::string dataOpToString(DataOp op);

std::string dataTypeToString(DataType type);

class DataLoad : Load {
  public:
    DataLoad(DataType type, bool notNull = false, bool waited = false)
        : Load(LoadType::Data), dataType_(type), notNull_(notNull), waited_(waited) {}
    const std::string toString() const override {
        std::string result = "DataLoad: " + dataTypeToString(dataType_);
        if (notNull_) {
            result += " NOT NULL";
        }
        if (waited_) {
            result += " WAITED";
        }
        return result;
    }

    void setNotNull(bool notNull) { notNull_ = notNull; }
    void setWaited(bool waited) { waited_ = waited; }

    bool isNotNull() const { return notNull_; }
    bool isWaited() const { return waited_; }

  private:
    DataType dataType_;
    bool notNull_;
    bool waited_;
};

class DataExprLoad : public DataLoad {
  public:
    DataExprLoad(DataOp op) : DataLoad(DataType::DataExpr), op_(op) {}
    const std::string toString() const override { return "DataExpr: " + dataOpToString(op_); }

  private:
    DataOp op_;
};

class IfExprLoad : public DataLoad {
  public:
    IfExprLoad() : DataLoad(DataType::IfExpr) {}
    const std::string toString() const override { return "IfExprLoad"; }
};

class LiteralLoad : public DataLoad {
  public:
    LiteralLoad(std::shared_ptr<Literal> value) : DataLoad(DataType::Literal), value_(std::move(value)) {}
    const std::string toString() const override {
        if (value_) {
            return value_->toString();
        } else {
            return "NULL";
        }
    }

  private:
    std::shared_ptr<Literal> value_;
};

class ListDataLoad : public DataLoad {
  public:
    ListDataLoad() : DataLoad(DataType::List) {}
    const std::string toString() const override { return "ListData"; }
};

class DictDataLoad : public DataLoad {
  public:
    DictDataLoad() : DataLoad(DataType::Dict) {}
    const std::string toString() const override { return "DictData"; }
};

class TupleDataLoad : public DataLoad {
  public:
    TupleDataLoad() : DataLoad(DataType::Tuple) {}
    const std::string toString() const override { return "TupleData"; }
};

class IndexDataLoad : public DataLoad {
  public:
    IndexDataLoad() : DataLoad(DataType::Index) {}
    const std::string toString() const override { return "IndexData"; }
};

class FuncDataLoad : public DataLoad {
  public:
    FuncDataLoad() : DataLoad(DataType::Func) {} // anonymous function
    FuncDataLoad(const Reference &ref) : DataLoad(DataType::Func), ref_(ref) {}
    const std::string toString() const override { return "FuncData: " + ref_.toString(); }

  private:
    Reference ref_;
};

class RefDataLoad : public DataLoad {
  public:
    RefDataLoad(const Reference &ref) : DataLoad(DataType::Ref), ref_(ref) {}
    const std::string toString() const override { return "RefData: " + ref_.toString(); }

  private:
    Reference ref_;
};

} // namespace AbstractSyntaxTree