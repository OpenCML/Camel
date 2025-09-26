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
 * Updated: Sep. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

namespace AbstractSyntaxTree {

enum class DataType {
    UnaryExpr,
    BinaryExpr,
    ReservedExpr,
    IfExpr,
    MatchExpr,
    TryExpr,
    Literal,
    List,
    Dict,
    Tuple,
    Func,
    Ref
};

enum class UnaryDataOp { Not, Neg, Inv };

enum class BinaryDataOp {
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
    Index
};

enum class ReservedDataOp { NullThen, ErrorThen, NotNullThen, Access, Call, With, Bind, As, Is };

std::string to_string(DataType type);
std::string to_string(UnaryDataOp op);
std::string to_string(BinaryDataOp op);
std::string to_string(ReservedDataOp op);

class DataLoad : public Load {
  public:
    DataLoad(DataType type, bool notNull = false, bool waited = false)
        : Load(LoadType::Data), dataType_(type), notNull_(notNull), waited_(waited) {}
    const std::string toString() const override {
        std::string result = "DataLoad: " + to_string(dataType_) + this->status();
        return result;
    }

    DataType dataType() const { return dataType_; }

    const std::string status() const {
        std::string status;
        if (waited_) {
            status += " (waited)";
        }
        if (notNull_) {
            status += " (non-null)";
        }
        return status;
    }

    void wait() { waited_ = true; }
    bool waited() const { return waited_; }

    void setNonNull(bool notNull) { notNull_ = notNull; }
    bool isNonNull() const { return notNull_; }
    const std::string geneCode() const override;

  private:
    DataType dataType_;
    bool notNull_;
    bool waited_;
};

class UnaryExprLoad : public DataLoad {
  public:
    UnaryExprLoad(UnaryDataOp op) : DataLoad(DataType::UnaryExpr), op_(op) {}
    const std::string toString() const override {
        return "UnaryExpr: " + to_string(op_) + this->status();
    }

    UnaryDataOp op() const { return op_; }
    const std::string geneCode() const override;

  private:
    UnaryDataOp op_;
};

class BinaryExprLoad : public DataLoad {
  public:
    BinaryExprLoad(BinaryDataOp op) : DataLoad(DataType::BinaryExpr), op_(op) {}
    const std::string toString() const override {
        return "BinaryExpr: " + to_string(op_) + this->status();
    }

    BinaryDataOp op() const { return op_; }
    const std::string geneCode() const override;

  private:
    BinaryDataOp op_;
};

class ReservedExprLoad : public DataLoad {
  public:
    ReservedExprLoad(ReservedDataOp op) : DataLoad(DataType::ReservedExpr), op_(op) {}
    const std::string toString() const override {
        return "ReservedExpr: " + to_string(op_) + this->status();
    }

    ReservedDataOp op() const { return op_; }
    const std::string geneCode() const override;

  private:
    ReservedDataOp op_;
};

class IfExprLoad : public DataLoad {
  public:
    IfExprLoad() : DataLoad(DataType::IfExpr) {}
    const std::string toString() const override { return "IfExpr" + this->status(); }
    const std::string geneCode() const override;
};

class MatchExprLoad : public DataLoad {
  public:
    MatchExprLoad(const Reference &ref) : DataLoad(DataType::MatchExpr), ref_(ref) {}

    const Reference &ref() const { return ref_; }

    const std::string toString() const override {
        return "MatchExpr: " + ref_.toString() + this->status();
    }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class MatchCaseLoad : public Load {
  public:
    MatchCaseLoad() : Load(LoadType::Data) {}
    const std::string toString() const override { return "MatchCase"; }
    const std::string geneCode() const override {
        throw CamelBaseException("MatchCaseLoad::geneCode() not implemented");
    }
};

class TryExprLoad : public DataLoad {
  public:
    TryExprLoad() : DataLoad(DataType::TryExpr) {}
    const std::string toString() const override { return "TryExpr" + this->status(); }
    const std::string geneCode() const override;
};

class LiteralLoad : public DataLoad {
  public:
    LiteralLoad(Literal value) : DataLoad(DataType::Literal), value_(value) {}
    const std::string toString() const override { return value_.toString() + this->status(); }

    const Literal &value() const { return value_; }
    const std::string geneCode() const override;

  private:
    Literal value_;
};

class ListDataLoad : public DataLoad {
  public:
    ListDataLoad() : DataLoad(DataType::List) {}
    const std::string toString() const override { return "ListData" + this->status(); }
    const std::string geneCode() const override;
};

class DictDataLoad : public DataLoad {
  public:
    DictDataLoad() : DataLoad(DataType::Dict) {}
    const std::string toString() const override { return "DictData" + this->status(); }
    const std::string geneCode() const override;
};

class TupleDataLoad : public DataLoad {
  public:
    TupleDataLoad() : DataLoad(DataType::Tuple) {}
    const std::string toString() const override { return "TupleData" + this->status(); }
    const std::string geneCode() const override;
};

class FuncDataLoad : public DataLoad {
  public:
    FuncDataLoad() : DataLoad(DataType::Func) {} // anonymous function
    FuncDataLoad(const Reference &ref) : DataLoad(DataType::Func), ref_(ref) {}
    const std::string toString() const override {
        return "FuncData: " + ref_.toString() + this->status();
    }

    const Reference &ref() const { return ref_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class RefDataLoad : public DataLoad {
  public:
    RefDataLoad(const Reference &ref) : DataLoad(DataType::Ref), ref_(ref) {}
    const std::string toString() const override {
        return "RefData: " + ref_.toString() + this->status();
    }

    const Reference &ref() const { return ref_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

} // namespace AbstractSyntaxTree
