// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

#include "camel/core/operator_semantics.h"

#include "base.h"

namespace camel::parse::ast {

// `Name` was the retired `use` declaration (design principles 10.2); the value is kept so that
// the numbering of the others does not change.
enum class StmtType { Name, Data, Type, Func, Expr, Exit, Block, UsingNamespace, Tag, Resource };

/// Where a tag may be written (design principles 6.2). The bits are those of
/// camel::core::tag::Position: the AST builder converts them with a cast.
enum class TagPosition : uint8_t {
    None  = 0,
    Func  = 1u << 0,
    Type  = 1u << 1,
    Field = 1u << 2,
    Nodes = 1u << 3,
    Param = 1u << 4,
};

enum class UnpackType { Array, Tuple, Struct };

enum class ExitType { Yield, Raise, Return, Throw };

class StmtLoad : public Load {
  public:
    StmtLoad(StmtType type_) : Load(LoadType::Stmt), StmtType_(type_) {}

    StmtType stmtType() const { return StmtType_; }
    const std::string geneCode() const override;

    /// The documentation of a declaration: the text of the `///` lines right above it, without
    /// the markers; empty if there is none.
    const std::string &doc() const { return doc_; }
    void setDoc(std::string doc) { doc_ = std::move(doc); }

    /// For a declaration written in namespace blocks: the `using namespace` written in those
    /// blocks (design principles 10.2: a `using` applies to the block it is written in; the
    /// blocks are flattened into the module's declarations, so each declaration carries them).
    const std::vector<Reference> &scopeUsings() const { return scopeUsings_; }
    void addScopeUsings(const std::vector<Reference> &usings) {
        scopeUsings_.insert(scopeUsings_.end(), usings.begin(), usings.end());
    }

  protected:
    /// ` /// first line (+n lines)` for the dump of a documented declaration, else nothing.
    std::string docSuffix() const;

  private:
    StmtType StmtType_;
    std::string doc_;
    std::vector<Reference> scopeUsings_;
};

class DataDeclLoad : public StmtLoad {
  public:
    DataDeclLoad(bool isVar, UnpackType type, const std::vector<Reference> &refs)
        : StmtLoad(StmtType::Data), isVar_(isVar), type_(type), refs_(refs) {}
    const std::string toString() const override;

    bool isVar() const { return isVar_; }
    UnpackType unpackType() const { return type_; }
    const std::vector<Reference> &refs() const { return refs_; }
    const std::string geneCode() const override;

  private:
    bool isVar_;
    UnpackType type_;
    std::vector<Reference> refs_;
};

class FuncDeclLoad : public StmtLoad {
  public:
    FuncDeclLoad(Reference ref) : StmtLoad(StmtType::Func), ref_(ref) {}

    const Reference &ref() const { return ref_; }

    /// An operator overload, `func operator +(...)` (design principles 4.3): the symbol as
    /// written (`+`, `[]`); empty for an ordinary function. ref() is then the reserved name of
    /// the operator's function (`__add__`), or `operator <symbol>` when the symbol cannot be
    /// overloaded with that many parameters, which the GCT builder reports.
    const std::string &operatorSymbol() const { return operatorSymbol_; }
    void setOperatorSymbol(std::string symbol) { operatorSymbol_ = std::move(symbol); }
    /// For an operator overload: the namespaces of the block it is written in. Its name is not
    /// qualified by them (the operator belongs to the module, design principles 4.3), but the
    /// names in its signature and body are looked up there, as for any declaration of the block.
    const std::vector<std::string> &declaredIn() const { return declaredIn_; }
    void setDeclaredIn(std::vector<std::string> path) { declaredIn_ = std::move(path); }

    const std::string toString() const override {
        return "FuncDecl: " + ref_.toString() +
               (operatorSymbol_.empty() ? std::string() : " (operator " + operatorSymbol_ + ")") +
               docSuffix();
    }
    const std::string geneCode() const override;

  private:
    Reference ref_;
    std::string operatorSymbol_;
    std::vector<std::string> declaredIn_;
};

class TypeDeclLoad : public StmtLoad {
  public:
    TypeDeclLoad(Reference ref, std::string uri = "")
        : StmtLoad(StmtType::Type), ref_(ref), uri_(std::move(uri)) {}

    const Reference &ref() const { return ref_; }
    /// The parameters of a type function, `T` and `U` in `type Pair<T, U> = ...`; empty for
    /// an ordinary type declaration.
    const std::vector<std::string> &params() const { return params_; }
    void setParams(std::vector<std::string> params) { params_ = std::move(params); }
    const std::string &uri() const { return uri_; }

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    Reference ref_;
    std::string uri_;
    std::vector<std::string> params_;
};

/// A resource declaration (design effects plan 3.1, design principles 3.3):
///
///   resource fs
///   resource io = [stdout, fs]
///   resource net unordered
///
/// A single resource names ambient state that no value carries; `unordered` says a call's
/// accesses to it may be reordered (step 8 gives that meaning); the list form makes the name a
/// group of the resources and groups it lists, expanded where it is used.
class ResourceDeclLoad : public StmtLoad {
  public:
    ResourceDeclLoad(Reference ref) : StmtLoad(StmtType::Resource), ref_(ref) {}

    const Reference &ref() const { return ref_; }

    bool unordered() const { return unordered_; }
    void setUnordered(bool unordered) { unordered_ = unordered; }

    void addMember(Reference member) { members_.push_back(std::move(member)); }
    const std::vector<Reference> &members() const { return members_; }
    /// A group (`resource io = [stdout, fs]`): every use of it stands for its members.
    bool isGroup() const { return group_; }
    void setGroup(bool group) { group_ = group; }

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    Reference ref_;
    bool unordered_ = false;
    bool group_     = false;
    std::vector<Reference> members_;
};

class UsingNamespaceLoad : public StmtLoad {
  public:
    UsingNamespaceLoad(Reference ref) : StmtLoad(StmtType::UsingNamespace), ref_(ref) {}

    const Reference &ref() const { return ref_; }
    const std::string toString() const override { return "UsingNamespace: " + ref_.toString(); }
    const std::string geneCode() const override;

  private:
    Reference ref_;
};

class ExprStmtLoad : public StmtLoad {
  public:
    ExprStmtLoad() : StmtLoad(StmtType::Expr) {}
    const std::string toString() const override { return "ExprStmt"; }
};

class ExitStmtLoad : public StmtLoad {
  public:
    ExitStmtLoad(ExitType type) : StmtLoad(StmtType::Exit), type_(type) {}
    ExitType exitType() const { return type_; }
    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    ExitType type_;
};

class StmtBlockLoad : public StmtLoad {
  public:
    StmtBlockLoad(bool sync = false) : StmtLoad(StmtType::Block), synced_(sync) {}

    void sync() { synced_ = true; }
    bool synced() const { return synced_; }

    void wait() { waited_ = true; }
    bool waited() const { return waited_; }

    // The children of a block, in this order (each only when it is there):
    //   [0] RepeatedLoad("Stmt")          the statements (always)
    //   [1] RepeatedLoad("FuncAnno")      the decorators after the statements' child, in the
    //                                    order written (`@d { ... }`, language reference 7.6)
    //   [2] RepeatedLoad("TagUse")        the tag uses written `#tag` before the block
    // The GCT builder finds them by their nodeName, not by their index.

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    bool synced_     = false;
    bool waited_     = false;
};

/// One `#` form (design principles 6.2): `#name`, `#name<args>`, and each member of
/// `#[a, b<v>]`. Its arguments, when it has any, are one RepeatedLoad("Data") child of data
/// expressions (compile-time values). The tag is resolved where it is written, when the tree is
/// built: a tag written at a position it does not list, or with the wrong number of arguments,
/// is reported at the tag.
class TagUseLoad : public Load {
  public:
    TagUseLoad(Reference name) : Load(LoadType::TagUse), name_(std::move(name)) {}

    const Reference &name() const { return name_; }
    const std::string toString() const override { return "TagUse: " + name_.toString(); }
    const std::string geneCode() const override { return ""; }

  private:
    Reference name_;
};

/// A tag declaration (design principles 6.2):
///
///   tag vjp<rule: func> on func read_by autodiff::vjp
///   tag finite_math = [no_nans, no_infs]
///
/// The parameters are names with a type, as written; a tag does not use them yet (the pass
/// that reads the tag checks its arguments), but the number of a use's arguments is checked
/// against them.
class TagDeclLoad : public StmtLoad {
  public:
    TagDeclLoad(Reference ref) : StmtLoad(StmtType::Tag), ref_(ref) {}

    const Reference &ref() const { return ref_; }

    struct Param {
        std::string name;
        /// The type as written, for the report; empty when the parser recovered.
        std::string type;
    };
    void addParam(Param param) { params_.push_back(std::move(param)); }
    const std::vector<Param> &params() const { return params_; }

    void addPosition(TagPosition position) {
        on_ = static_cast<uint8_t>(on_) | static_cast<uint8_t>(position);
    }
    uint8_t on() const { return on_; }
    bool allows(TagPosition position) const {
        return (on_ & static_cast<uint8_t>(position)) != 0;
    }

    /// The tags of an alias group (`tag finite_math = [no_nans, no_infs]`); empty for an
    /// ordinary tag.
    void addAlias(Reference alias) { aliases_.push_back(std::move(alias)); }
    const std::vector<Reference> &aliases() const { return aliases_; }
    bool isGroup() const { return !aliases_.empty(); }

    /// The passes the declaration says read it, as written (`std::opt`, `tensor::fuse`).
    void addReader(Reference reader) { readBy_.push_back(std::move(reader)); }
    const std::vector<Reference> &readBy() const { return readBy_; }

    const std::string toString() const override;
    const std::string geneCode() const override;

  private:
    Reference ref_;
    std::vector<Param> params_;
    uint8_t on_ = 0;
    std::vector<Reference> aliases_;
    std::vector<Reference> readBy_;
};

} // namespace camel::parse::ast

