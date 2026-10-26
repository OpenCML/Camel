// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    StaticLoop,
    StaticAccess,
    LoopExpr,
    Literal,
    Array,
    Tuple,
    Struct,
    //  Comprehensions.: a `static for` in a value position whose rounds are packed
    // back into a struct (`StructComp`) or a tuple (`TupleComp`) of the same shape.
    TupleComp,
    StructComp,
    // `T { static for ... }`: the declared shape `T` gives the field names and the kind.
    ShapeComp,
    Func,
    Ref,
    ConstAs,
    Slice,
    EnumPattern,
    // `..e` in an argument list: the operand is a compile-time tuple whose elements are the
    // arguments (design principles 5.5).
    SpreadArg
};

/// What a `match type` case selects (design principles 5.5, R3): the shape of a type
/// (a structure, an ordinary tuple, a function, every leaf, or anything) or its identity
/// (`case int`, `case MyStruct`, `case T`).
enum class TypeCaseKind : uint8_t {
    Struct   = 0,
    Tuple    = 1,
    Leaf     = 2,
    Wildcard = 3,
    Func     = 4,
    Identity = 5
};

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
    As
};

std::string to_string(DataType type);
std::string to_string(UnaryDataOp op);
std::string to_string(BinaryDataOp op);
std::string to_string(ReservedDataOp op);

class DataLoad : public Load {
  public:
    DataLoad(DataType type) : Load(LoadType::Data), dataType_(type) {}

    const std::string toString() const override {
        std::string result = "DataLoad: " + to_string(dataType_);
        return result;
    }

    DataType dataType() const { return dataType_; }

    const std::string geneCode() const override;

  private:
    DataType dataType_;
};

class UnaryExprLoad : public DataLoad {
  public:
    UnaryExprLoad(UnaryDataOp op) : DataLoad(DataType::UnaryExpr), op_(op) {}

    const std::string toString() const override {
        return "UnaryExpr: " + to_string(op_);
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
        return "BinaryExpr: " + to_string(op_);
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
        return "ReservedExpr: " + to_string(op_);
    }

    ReservedDataOp op() const { return op_; }
    const std::string geneCode() const override;

    /// A positional argument written after a named one (`f(a: 1, 2)`) is refused where the call
    /// is built (design principles 4.4): its origin is kept here, beside the argument list, so
    /// the report can point at the argument that is out of place. Invalid for every other call.
    void setPositionalAfterNamed(camel::source::origin_id_t origin) {
        positionalAfterNamed_ = origin;
    }
    camel::source::origin_id_t positionalAfterNamed() const {
        return positionalAfterNamed_;
    }

  private:
    ReservedDataOp op_;
    camel::source::origin_id_t positionalAfterNamed_ = camel::source::kInvalidOriginId;
};

class IfExprLoad : public DataLoad {
  public:
    IfExprLoad() : DataLoad(DataType::IfExpr) {}

    const std::string toString() const override { return "IfExpr"; }
    const std::string geneCode() const override;
};

class MatchExprLoad : public DataLoad {
  public:
    MatchExprLoad() : DataLoad(DataType::MatchExpr) {}

    const std::string toString() const override { return "MatchExpr"; }
    const std::string geneCode() const override;
};

/// `match type T { case ... => ... }`: children [0] the type, [1] Repeated TypeCase.
/// The cases are all kept: the graph builder selects one when the type is known.
class TypeMatchLoad : public DataLoad {
  public:
    TypeMatchLoad() : DataLoad(DataType::TypeMatch) {}

    const std::string toString() const override { return "TypeMatch"; }
    const std::string geneCode() const override { return "match type"; }
};

/// One pattern of a `match type` case. `kind` is a shape, or Identity with the type written
/// in `type` (a TypeLoad): `case int`, `case f32[..]`, `case T`.
struct TypeCasePattern {
    TypeCaseKind kind = TypeCaseKind::Wildcard;
    node_ptr_t type; // Identity only
};

/// One case of `match type`; the body is child [0]. The patterns are kept in the load because
/// they are shapes and types, not expressions: the shapes select a kind, and an identity
/// pattern keeps the type it names so the graph builder can resolve it (and report an unknown
/// name) where the declaration is read.
class TypeCaseLoad : public DataLoad {
  public:
    explicit TypeCaseLoad(std::vector<TypeCasePattern> patterns)
        : DataLoad(DataType::TypeCase), patterns_(std::move(patterns)) {}

    const std::vector<TypeCasePattern> &patterns() const { return patterns_; }
    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    std::vector<TypeCasePattern> patterns_;
};

/// `static for n, F in fields T => body` or `static for j in indices ts => body`:
/// children [0] the walked shape (a type for `fields`, a value for `indices`), [1] the body (a
/// block). The load holds the names: for `fields`, `n` is the field name (a tuple's index in
/// decimal), `F` the field type and `tags` the field's tags; for `indices`, only `j` (the
/// position as an integer) is bound. A head may name fewer of them than the element has parts,
/// never more (`SemanticDiag::StaticHeadNameCount`); `_` at a position binds nothing. None is a
/// declaration of the enclosing scope: they live in the body only.
class StaticForLoad : public DataLoad {
  public:
    StaticForLoad(
        std::string valueName, std::string typeName, std::string tagsName = {},
        bool byIndices = false, size_t nameCount = 0)
        : DataLoad(DataType::StaticFor), valueName_(std::move(valueName)),
          typeName_(std::move(typeName)), tagsName_(std::move(tagsName)),
          byIndices_(byIndices), nameCount_(nameCount) {}

    const std::string &valueName() const { return valueName_; }
    const std::string &typeName() const { return typeName_; }
    /// The third name of `(n, F, tags)`: the field's tags. Empty when the head names two things
    /// or one; also empty (written as `_`) when the position is the wildcard.
    const std::string &tagsName() const { return tagsName_; }
    /// How many names the head writes, in all: the count the element must have parts for
    /// (`SemanticDiag::StaticHeadNameCount`). The three names above are the first three written;
    /// a head may write more than three, and only the count says so.
    size_t nameCount() const { return nameCount_; }
    /// `indices ts` walks the positions of a tuple value; `fields T` walks the fields of a type.
    bool byIndices() const { return byIndices_; }
    const std::string toString() const override { return "StaticFor"; }
    const std::string geneCode() const override { return "static for"; }

  private:
    std::string valueName_;
    std::string typeName_;
    std::string tagsName_;
    bool byIndices_     = false;
    size_t nameCount_   = 0;
};

/// `static loop s1 = a1, ..., sk = ak for i in <序列> do body` (`in fields T`, `in indices ts`,
/// or a sequence the compiler works out —, dropped the
/// parentheses the head used to carry): children [0] a Repeated Data of the first values, [1] the
/// walked shape (a type for `fields`, a value for `indices` and for a sequence), [2] the body (a
/// block). The load holds the names of the state and the generator's bindings; the rounds are the
/// fields of `T`, the positions of `ts`, or the elements of the sequence. `byIndices` and
/// `bySequence` are never both set, and neither is set for `fields`.
class StaticLoopLoad : public DataLoad {
  public:
    StaticLoopLoad(
        std::vector<Reference> state, std::string valueName, std::string typeName,
        std::string tagsName = {}, bool byIndices = false, bool bySequence = false,
        size_t nameCount = 0)
        : DataLoad(DataType::StaticLoop), state_(std::move(state)),
          valueName_(std::move(valueName)), typeName_(std::move(typeName)),
          tagsName_(std::move(tagsName)), byIndices_(byIndices), bySequence_(bySequence),
          nameCount_(nameCount) {}

    const std::vector<Reference> &state() const { return state_; }
    const std::string &valueName() const { return valueName_; }
    const std::string &typeName() const { return typeName_; }
    /// The third name of `(n, F, tags)`: see `StaticForLoad::tagsName`.
    const std::string &tagsName() const { return tagsName_; }
    /// How many names the head writes, in all: see `StaticForLoad::nameCount`.
    size_t nameCount() const { return nameCount_; }
    bool byIndices() const { return byIndices_; }
    /// The head is `in <序列>`: the written sequence, whose elements the compiler must work out
    /// (a constant interval today). Only one name is bound, the element itself, not a position.
    bool bySequence() const { return bySequence_; }
    const std::string toString() const override { return "StaticLoop"; }
    const std::string geneCode() const override { return "static loop"; }

  private:
    std::vector<Reference> state_;
    std::string valueName_;
    std::string typeName_;
    std::string tagsName_;
    bool byIndices_  = false;
    bool bySequence_ = false;
    size_t nameCount_ = 0;
};

/// `t.[key]`: children [0] the target, [1] the key expression. The key must be a compile-time
/// constant (a literal or the `n` of an enclosing `static for`); the graph builder decides.
class StaticAccessLoad : public DataLoad {
  public:
    StaticAccessLoad() : DataLoad(DataType::StaticAccess) {}

    const std::string toString() const override { return "StaticAccess"; }
    const std::string geneCode() const override { return ".[]"; }
};

/// One arm of a `match`: `case pattern ('|' pattern)* when guard? => arm`.
///
/// A case with `|` becomes one arm per pattern (the graph builder's branch has one value per
/// arm), and the arms share the arm and the guard. Children, always in this order:
///   [0] Data     PATTERN: one per arm, the pattern that arm names;
///   [1] StmtBlock ARM: the body, shared by the arms of the case;
///   [2] Data     GUARD: the condition of `when`, present only when the case has one. It is
///                evaluated in the arm's scope, so it reads the names the pattern binds, and a
///                false guard sends the value on to the cases written after this one.
class MatchCaseLoad : public DataLoad {
  public:
    MatchCaseLoad() : DataLoad(DataType::MatchCase) {}

    const std::string toString() const override { return "MatchCase"; }
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

    const std::string toString() const override { return value_.toString(); }

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

    const std::string toString() const override { return "ArrayData"; }
    const std::string geneCode() const override;
};

class StructDataLoad : public DataLoad {
  public:
    StructDataLoad() : DataLoad(DataType::Struct) {}

    const std::string toString() const override { return "StructData"; }
    const std::string geneCode() const override;
};

class TupleDataLoad : public DataLoad {
  public:
    TupleDataLoad() : DataLoad(DataType::Tuple) {}

    const std::string toString() const override { return "TupleData"; }
    const std::string geneCode() const override;
};

/// `( static for n, F in fields T => e )`: the generator is child 0. The elements come from
/// the rounds in declaration order; the type is `TupleType` of the rounds' value types.
class TupleCompLoad : public DataLoad {
  public:
    TupleCompLoad() : DataLoad(DataType::TupleComp) {}

    const std::string toString() const override { return "TupleComp"; }
    const std::string geneCode() const override;
};

/// `{ static for n, F in fields T => e }`: the generator is child 0. The fields keep the names
/// and order of `T`; the type is a `StructType` of the rounds' value types.
class StructCompLoad : public DataLoad {
  public:
    StructCompLoad() : DataLoad(DataType::StructComp) {}

    const std::string toString() const override { return "StructComp"; }
    const std::string geneCode() const override;
};

/// `T { static for n, F in fields T => e }`: child 0 is the type `T`, child 1 the generator.
/// The result has the field names and the kind of `T`, and the rounds' value types.
class ShapeCompLoad : public DataLoad {
  public:
    ShapeCompLoad() : DataLoad(DataType::ShapeComp) {}

    const std::string toString() const override { return "ShapeComp"; }
    const std::string geneCode() const override;
};

class FuncDataLoad : public DataLoad {
  public:
    FuncDataLoad() : DataLoad(DataType::Func) {} // anonymous function
    FuncDataLoad(const Reference &ref) : DataLoad(DataType::Func), ref_(ref) {}

    const std::string toString() const override {
        return "FuncData: " + ref_.toString();
    }

    const Reference &ref() const { return ref_; }
    /// The declaration's applicability condition: the expression after `when`, or null. It is
    /// not one of the node's children (a body-less declaration has one child), because it is
    /// not part of the signature or the body: it is evaluated when the declaration is
    /// instantiated (design principles 4.4).
    const node_ptr_t &when() const { return when_; }
    void setWhen(const node_ptr_t &when) { when_ = when; }
    const std::string &whenText() const { return whenText_; }
    void setWhenText(std::string text) { whenText_ = std::move(text); }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    node_ptr_t when_;
    std::string whenText_;
};

class RefDataLoad : public DataLoad {
  public:
    RefDataLoad(const Reference &ref) : DataLoad(DataType::Ref), ref_(ref) {}

    const std::string toString() const override {
        return "RefData: " + ref_.toString();
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
        return "ConstAsData: " + ref_.toString();
    }
    const Reference &ref() const { return ref_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

/// One dimension of a subscript that is a slice, `t[a:b]`, `t[:b]`, `t[a:]`, `t[a:b:s]`: its
/// start, end and step, in that order. A part that was not written is a null load in its slot,
/// an empty part, never an omitted value (design principles 5.4).
class SliceLoad : public DataLoad {
  public:
    SliceLoad() : DataLoad(DataType::Slice) {}

    const std::string toString() const override {
        return "DataLoad: " + to_string(dataType());
    }
    const std::string geneCode() const override;
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
        return "EnumPattern: " + ref_.toString();
    }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    bool structShape_ = false;
    std::vector<EnumPatternField> fields_;
};

} // namespace camel::parse::ast
