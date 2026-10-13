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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "camel/core/error/diagnostics.h"

namespace camel::parse::ast {

enum class DataType {
    UnaryExpr,
    BinaryExpr,
    ReservedExpr,
    IfExpr,
    MatchExpr,
    MatchCase,
    TryExpr,
    LoopExpr,
    Literal,
    Array,
    Tuple,
    Struct,
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
    Or,         // `|`: both sides evaluated, an ordinary operator
    And,        // `&`
    LogicalOr,  // `||`: reserved, the right side evaluated only when it decides
    LogicalAnd, // `&&`
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
    Pow,
    Index
};

enum class ReservedDataOp { NullThen, ErrorThen, NotNullThen, Access, Call, Bind, Comp, As, Is };

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

class MatchCaseLoad : public DataLoad {
  public:
    MatchCaseLoad() : DataLoad(DataType::MatchCase) {}

    const std::string toString() const override { return "MatchCase" + this->status(); }
    const std::string geneCode() const override {
        throw camel::core::error::DiagnosticBuilder::of(
            camel::core::error::InternalDiag::UnknownInternalError)
            .commit("MatchCaseLoad::geneCode() not implemented");
    }
};

class TryExprLoad : public DataLoad {
  public:
    TryExprLoad() : DataLoad(DataType::TryExpr) {}

    const std::string toString() const override { return "TryExpr" + this->status(); }
    const std::string geneCode() const override;
};

/// loop STATE = INIT (for BINDER in SEQUENCE)? (while CONDITION)? { BODY }
///
/// A loop is an expression with a state (design principles 2.10). The load holds the names:
///   state()    the names of the state, one for `loop acc = ...`, several for the tuple
///              pattern `loop (i, acc) = ...` (isTuple() tells `loop (a,) = ...` from
///              `loop a = ...`);
///   binder()   the name bound to the element of each round; empty without `for`.
/// The node has four children, always in this order:
///   [0] Data          INIT: the first value of the state, evaluated once, outside the scope
///                     of the state names and of the binder;
///   [1] Optional Data SEQUENCE: what `for` iterates over, evaluated once, outside that scope
///                     too; empty without `for`;
///   [2] Optional Data CONDITION of `while`: evaluated before each round with the state names
///                     (and the binder, with `for`) in scope; empty without `while`;
///   [3] StmtBlock     BODY: evaluated each round with the same names in scope; its value is
///                     the state of the next round.
/// The value of the expression is the state after the last round: of the type of INIT, a
/// tuple when the state is a tuple pattern. With `for` and `while` both, a round runs while
/// there is an element left and the condition holds.
class LoopExprLoad : public DataLoad {
  public:
    LoopExprLoad(std::vector<Reference> state, bool tuple, Reference binder)
        : DataLoad(DataType::LoopExpr), state_(std::move(state)), tuple_(tuple),
          binder_(std::move(binder)) {}

    const std::vector<Reference> &state() const { return state_; }
    bool isTuple() const { return tuple_; }
    bool hasFor() const { return !binder_.empty(); }
    const Reference &binder() const { return binder_; }

    const std::string toString() const override;

  private:
    std::vector<Reference> state_;
    bool tuple_;
    Reference binder_;
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

class ArrayDataLoad : public DataLoad {
  public:
    ArrayDataLoad() : DataLoad(DataType::Array) {}

    const std::string toString() const override { return "ArrayData" + this->status(); }
    const std::string geneCode() const override;
};

class StructDataLoad : public DataLoad {
  public:
    StructDataLoad() : DataLoad(DataType::Struct) {}

    const std::string toString() const override { return "StructData" + this->status(); }
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

} // namespace camel::parse::ast
