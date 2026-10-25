// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "base.h"

namespace camel::parse::ast {

enum class TypeType {
    Null,
    Stream,
    Expr,
    Array,
    ArrayDim,
    Tuple,
    Struct,
    Func,
    Spec,
    Unit,
    Infer,
    Data,
    Ref,
    Enum
};

enum class TypeOp { Inter, Diff, KeyUnion, KeyInter, KeyDiff, Specialize, TypeOf };

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

/// A type with one postfix mark: `T?` (may be null) or, when `fallible`, `T!` (may have failed).
class NullableTypeLoad : public TypeLoad {
  public:
    NullableTypeLoad(bool fallible = false) : TypeLoad(TypeType::Null), fallible_(fallible) {}
    bool fallible() const { return fallible_; }
    const std::string toString() const override {
        return fallible_ ? "FallibleType" : "NullableType";
    }
    const std::string geneCode() const override;

  private:
    bool fallible_;
};

/// The stream suffix `T..` (design principles 5.4): the elements of `T` that arrive one after
/// another. One mark among the postfix marks, read left to right with `?` and `!`: `T?..` is
/// the stream of `T?`, `T..?` is the possibly absent stream (the rule of
/// 2026-10-24).
class StreamTypeLoad : public TypeLoad {
  public:
    StreamTypeLoad() : TypeLoad(TypeType::Stream) {}
    const std::string toString() const override { return "StreamType"; }
    const std::string geneCode() const override;
};

class TypeExprLoad : public TypeLoad {
  public:
    TypeExprLoad(TypeOp op) : TypeLoad(TypeType::Expr), op_(op) {}
    const std::string toString() const override { return "TypeExpr: " + to_string(op_); }
    const std::string geneCode() const override;
    TypeOp op() const { return op_; }

  private:
    TypeOp op_;
};

class SpecTypeLoad : public TypeLoad {
  public:
    SpecTypeLoad() : TypeLoad(TypeType::Spec) {}
    const std::string toString() const override { return "SpecType"; }
    const std::string geneCode() const override;
};

/// How a dimension group ends (design principles 5.4): no pack, `..`, or `..name`.
enum class DimPack { None, Anonymous, Named };

/// One `[...]` of an array type or a shape argument: the extents written inside it and the pack
/// that may follow them. An extent is the expression written for it; `nullptr` is `?`, the
/// unknown extent. `packItem` is the position of the `..` among all the items and `itemCount`
/// how many items were written, so a reader can tell a pack that is not last (or repeated) from
/// one that is; the parser keeps such a group rather than dropping the rest of it.
struct DimGroup {
    std::vector<node_ptr_t> extents;
    DimPack pack = DimPack::None;
    std::string packName;
    size_t packItem  = 0;
    size_t itemCount = 0;
    bool packRepeated = false;

    bool empty() const { return extents.empty() && pack == DimPack::None; }
};

class ArrayTypeLoad : public TypeLoad {
  public:
    explicit ArrayTypeLoad(std::vector<DimGroup> dims)
        : TypeLoad(TypeType::Array), dims_(std::move(dims)) {}
    const std::string toString() const override {
        return "ArrayType: [" + std::to_string(dims_.size()) + "]";
    }

    const std::vector<DimGroup> &dims() const { return dims_; }
    const std::string geneCode() const override;

  private:
    std::vector<DimGroup> dims_;
};

/// A `[extent, ...]` written as a type argument (`f32[n, 4]`): a shape, not a type.
class ArrayDimLoad : public TypeLoad {
  public:
    explicit ArrayDimLoad(DimGroup dim) : TypeLoad(TypeType::ArrayDim), dim_(std::move(dim)) {}
    const std::string toString() const override { return "ArrayDim"; }

    const DimGroup &dim() const { return dim_; }
    const std::string geneCode() const override;

  private:
    DimGroup dim_;
};

class TupleTypeLoad : public TypeLoad {
  public:
    TupleTypeLoad() : TypeLoad(TypeType::Tuple) {}
    const std::string toString() const override { return "TupleType"; }
    const std::string geneCode() const override;
};

class StructTypeLoad : public TypeLoad {
  public:
    StructTypeLoad() : TypeLoad(TypeType::Struct) {}
    const std::string toString() const override { return "StructType"; }
    const std::string geneCode() const override;
};

class FuncTypeLoad : public TypeLoad {
  public:
    FuncTypeLoad() : TypeLoad(TypeType::Func) {}
    FuncTypeLoad(const std::string &uri) : TypeLoad(TypeType::Func), uri_(uri) {}
    const std::string toString() const override;

    const std::string uri() const { return uri_; }
    void setUri(const std::string &uri) { uri_ = uri; }
    const ModifierSet &modifiers() const { return modifiers_; }
    void setModifiers(const ModifierSet &modifiers) { modifiers_ = modifiers; }
    /// The type parameters a declaration declares at its head (design principles 5.5):
    /// `T` and `U` in `func first::<T, U>(xs: T[]): U`. Empty for an ordinary function type
    /// and for a declaration that is not generic.
    const std::vector<std::string> &typeParams() const { return typeParams_; }
    void setTypeParams(std::vector<std::string> typeParams) {
        typeParams_ = std::move(typeParams);
    }
    const std::string geneCode() const override;

  private:
    ModifierSet modifiers_;
    std::string uri_;
    std::vector<std::string> typeParams_;
};

/// The resources a function type publishes (design effects plan 3.1): `pure` says a call
/// touches none, `uses x, fs` names the parameters and the resources it touches. Attached to the
/// function type it is written after; a declaration's clause is attached to the function type of
/// its signature, so a body-less declaration and a function type carry the same thing.
class ResourceClauseLoad : public Load {
  public:
    ResourceClauseLoad() : Load(LoadType::ResourceClause) {}

    bool pure() const { return pure_; }
    void setPure(bool pure) { pure_ = pure; }

    void addUse(Reference use) { uses_.push_back(std::move(use)); }
    const std::vector<Reference> &uses() const { return uses_; }

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    bool pure_ = false;
    std::vector<Reference> uses_;
};

class UnitTypeLoad : public TypeLoad {
  public:
    UnitTypeLoad(Reference ref) : TypeLoad(TypeType::Unit), ref_(ref) {}
    const std::string toString() const override { return "UnitType"; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class InferTypeLoad : public TypeLoad {
  public:
    InferTypeLoad(Reference ref) : TypeLoad(TypeType::Infer), ref_(ref) {}
    const std::string toString() const override { return "InferType"; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class DataTypeLoad : public TypeLoad {
  public:
    DataTypeLoad(Reference ref) : TypeLoad(TypeType::Data), ref_(ref) {}
    const std::string toString() const override { return "DataType: " + ref_.toString(); }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class RefTypeLoad : public TypeLoad {
  public:
    RefTypeLoad(Reference ref) : TypeLoad(TypeType::Ref), ref_(ref) {}
    const std::string toString() const override { return "RefType: " + ref_.toString(); }

    const Reference &ref() const { return ref_; }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

/// How a variant of an enum carries its data (design principles 5.3): nothing, a tuple of
/// fields in the order written, or named fields.
enum class EnumVariantShape { Unit, Tuple, Struct };

/// One variant of an enum declaration, as written: its name, its shape and, for a struct
/// variant, the names of its fields. The field types themselves are the child nodes of the
/// declaration, one list per variant, in the same order.
struct EnumVariantDecl {
    std::string name;
    EnumVariantShape shape = EnumVariantShape::Unit;
    std::vector<std::string> fieldNames;
};

/// `enum { ... }`: the right-hand side of an enum type declaration.
class EnumTypeLoad : public TypeLoad {
  public:
    EnumTypeLoad() : TypeLoad(TypeType::Enum) {}
    const std::string toString() const override { return "EnumType"; }
    const std::string geneCode() const override;

    std::vector<EnumVariantDecl> &variants() { return variants_; }
    const std::vector<EnumVariantDecl> &variants() const { return variants_; }

  private:
    std::vector<EnumVariantDecl> variants_;
};

} // namespace camel::parse::ast
