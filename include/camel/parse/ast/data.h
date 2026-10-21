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
 * Updated: Oct. 19, 2026
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
    TypeMatch,
    TypeCase,
    StaticFor,
    StaticAccess,
    TryExpr,
    LoopExpr,
    Literal,
    Array,
    Tuple,
    Struct,
    Func,
    Ref,
    ConstAs,
    EnumPattern
};

/// The shapes a `match type` case selects (design principles 5.5, R3):
/// a structure, an ordinary tuple, every leaf (including `T?` / `T!` and cells), or anything.
enum class TypeCaseKind : uint8_t { Struct = 0, Tuple = 1, Leaf = 2, Wildcard = 3 };

std::string to_string(TypeCaseKind kind);

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

enum class ReservedDataOp {
    NullThen,
    ErrorThen,
    NotNullThen,
    Access,
    Call,
    Bind,
    Instantiate,
    Comp,
    As,
    Is
};

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
    MatchExprLoad() : DataLoad(DataType::MatchExpr) {}

    const std::string toString() const override { return "MatchExpr" + this->status(); }
    const std::string geneCode() const override;
};

/// `match type T { case ... => ... }`: children [0] the type, [1] Repeated TypeCase.
/// The cases are all kept: the graph builder selects one when the type is known.
class TypeMatchLoad : public DataLoad {
  public:
    TypeMatchLoad() : DataLoad(DataType::TypeMatch) {}

    const std::string toString() const override { return "TypeMatch" + this->status(); }
    const std::string geneCode() const override { return "match type"; }
};

/// One case of `match type`; the body is child [0]. The patterns are kept in the load
/// because they are shapes, not expressions or types.
class TypeCaseLoad : public DataLoad {
  public:
    explicit TypeCaseLoad(std::vector<TypeCaseKind> patterns)
        : DataLoad(DataType::TypeCase), patterns_(std::move(patterns)) {}

    const std::vector<TypeCaseKind> &patterns() const { return patterns_; }
    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    std::vector<TypeCaseKind> patterns_;
};

/// `static for (n, F) in fields T => body`: children [0] the type, [1] the body (a block).
/// The load holds the two names: `n` is the field name (a tuple's index in decimal) and `F` the
/// field type. Neither is a declaration of the enclosing scope: both live in the body only.
class StaticForLoad : public DataLoad {
  public:
    StaticForLoad(std::string valueName, std::string typeName)
        : DataLoad(DataType::StaticFor), valueName_(std::move(valueName)),
          typeName_(std::move(typeName)) {}

    const std::string &valueName() const { return valueName_; }
    const std::string &typeName() const { return typeName_; }
    const std::string toString() const override { return "StaticFor" + this->status(); }
    const std::string geneCode() const override { return "static for"; }

  private:
    std::string valueName_;
    std::string typeName_;
};

/// `t.[key]`: children [0] the target, [1] the key expression. The key must be a compile-time
/// constant (a literal or the `n` of an enclosing `static for`); the graph builder decides.
class StaticAccessLoad : public DataLoad {
  public:
    StaticAccessLoad() : DataLoad(DataType::StaticAccess) {}

    const std::string toString() const override { return "StaticAccess" + this->status(); }
    const std::string geneCode() const override { return ".[]"; }
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

/// loop S1 = INIT1, ..., Sk = INITk (for BINDER in SEQUENCE)? (while CONDITION)? do BODY
///
/// A loop is an expression with a state (design principles 3.10, the `do` form). The
/// load holds the names:
///   state()    the names of the state, in the order written (k >= 1);
///   binder()   the name bound to the element of each round; empty without `for`.
/// The node has four children, always in this order:
///   [0] Repeated Data INIT1 ... INITk: the first values of the state, evaluated once, outside
///                     the scope of the state names and of the binder;
///   [1] Optional Data SEQUENCE: what `for` iterates over, evaluated once, outside that scope
///                     too; empty without `for`;
///   [2] Optional Data CONDITION of `while`: evaluated before each round with the state names
///                     in scope (not the binder: the element is taken after the condition
///                     holds); empty without `while`;
///   [3] StmtBlock     BODY: evaluated each round with the state names and the binder in scope;
///                     its value is the state of the next round, a k-tuple when k > 1.
/// The value of the expression is the state after the last round: the one value when k = 1,
/// the k-tuple of the values when k > 1. With `for` and `while` both, a round runs while the
/// condition holds and there is an element left.
class LoopExprLoad : public DataLoad {
  public:
    LoopExprLoad(std::vector<Reference> state, Reference binder)
        : DataLoad(DataType::LoopExpr), state_(std::move(state)), binder_(std::move(binder)) {}

    const std::vector<Reference> &state() const { return state_; }
    /// The state has several names: the body gives, and the loop is, a tuple.
    bool isTuple() const { return state_.size() > 1; }
    bool hasFor() const { return !binder_.empty(); }
    const Reference &binder() const { return binder_; }

    const std::string toString() const override;

  private:
    std::vector<Reference> state_;
    Reference binder_;
};

class LiteralLoad : public DataLoad {
  public:
    LiteralLoad(Literal value) : DataLoad(DataType::Literal), value_(value) {}

    const std::string toString() const override { return value_.toString() + this->status(); }

    const Literal &value() const { return value_; }
    const std::string geneCode() const override;

    /// Set on the pattern literal that a formatted string was lowered to (the `'...'` of
    /// `'...'->format<...>`). A bad format specification is then reported where the
    /// interpolation is, not at the whole string.
    bool fromFormattedString() const { return fromFormattedString_; }
    void setFromFormattedString() { fromFormattedString_ = true; }

  private:
    Literal value_;
    bool fromFormattedString_ = false;
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

/// `constas n`: the name of a dimension, bound by the first parameter type of a signature that
/// writes it (design principles 5.4). Only meaningful inside a `const` type argument; the
/// compiler reports one anywhere else.
class ConstAsLoad : public DataLoad {
  public:
    ConstAsLoad(const Reference &ref) : DataLoad(DataType::ConstAs), ref_(ref) {}

    const std::string toString() const override {
        return "ConstAsData: " + ref_.toString() + this->status();
    }
    const Reference &ref() const { return ref_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

/// One field the pattern of an enum variant names. A tuple variant writes the bindings in
/// order (no name); a struct variant names the field it takes. An empty binding is `_`: the
/// value is matched but not bound.
struct EnumPatternField {
    std::string name;
    std::string binding;
};

/// A pattern over an enum variant, `Shape::Circle(r)` or `Shape::Rect { w, h: height }`: the
/// variant it names and what it binds out of the fields.
class EnumPatternLoad : public DataLoad {
  public:
    EnumPatternLoad(const Reference &ref, bool structShape)
        : DataLoad(DataType::EnumPattern), ref_(ref), structShape_(structShape) {}

    const Reference &ref() const { return ref_; }
    /// Whether the pattern was written with field names (`{ w, h }`) rather than in order.
    bool structShape() const { return structShape_; }
    std::vector<EnumPatternField> &fields() { return fields_; }
    const std::vector<EnumPatternField> &fields() const { return fields_; }

    const std::string toString() const override {
        return "EnumPattern: " + ref_.toString() + this->status();
    }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    bool structShape_ = false;
    std::vector<EnumPatternField> fields_;
};

} // namespace camel::parse::ast
