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
 * Created: May. 05, 2024
 * Updated: Apr. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"
#include "entity.h"
namespace AbstractSyntaxTree {

class Node;
using node_ptr_t = std::shared_ptr<Node>;

class Load;
using load_ptr_t = std::shared_ptr<Load>;

enum class NodeType {
    Program,

    Decl,
    ModuleDecl,
    ImportDecl,
    ExportDecl,
    LetDecl,
    UseDecl,
    FuncDecl,
    TypeDecl,
    EnumDecl,

    Stmt,
    StmtList,
    StmtBlock,
    RetStmt,
    WaitStmt,
    LambdaExpr,

    KeyTypePair,
    KeyValuePair,
    KeyParamPair,

    IdentDefList,
    ValueList,
    IndexValues,
    PairedValues,
    PairedParams,
    ArgumentList,
    MemberAccess,

    DataExpr,
    IdentList,
    DataList,
    CtrlExpr,
    IfLoad,
    CatchClause,
    BlockExpr,
    WaitExpr,
    Wildcard,
    MatchCase,

    AssignExpr,
    LogicalOrExpr,
    LogicalAndExpr,
    EqualityExpr,
    RelationalExpr,
    AdditiveExpr,
    MultiplicativeExpr,
    NullableExpr,
    UnaryExpr,
    LinkExpr,
    BindExpr,
    WithExpr,
    AnnoExpr,
    DictExpr,
    DictType,
    ListExpr,
    Literal,
    TypeExpr,
    UnionType,
    UnionUnit,
    ListType,
    ArgsType,
    PrimaryType,
    DictExprType,
    TypeList,
    LambdaType,
    IdentDef,
    IdentRef
};

class Load {
  protected:
    NodeType type_;
    size_t tokenStart_;
    size_t tokenEnd_;

  public:
    Load(NodeType type) : type_(type) {}
    virtual ~Load() = default;

    void setToken(size_t start, size_t end) {
        tokenStart_ = start;
        tokenEnd_ = end;
        // TODO: add token range check
    }
    const std::string geneCode() const;
    // TODO: add code print function

    NodeType type() const { return type_; }
    std::pair<size_t, size_t> range() const { return {tokenStart_, tokenEnd_}; }
    const std::string typeStr() const;

    virtual const std::string toString() const { return typeStr(); }
    virtual void visit() { throw std::runtime_error("Load::visit() not implemented"); };
};

class Program : public Load {
  public:
    Program() : Load(NodeType::Program) {}
    // const std::string toString() const override;
};

inline std::shared_ptr<Program> program_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<Program>(ptr);
}

class Decl : public Load {
  public:
    Decl() : Load(NodeType::Decl) {}
    // const std::string toString() const override;
};

inline std::shared_ptr<Decl> decl_load_ptr_cast(const load_ptr_t &ptr) { return std::dynamic_pointer_cast<Decl>(ptr); }

class StmtList : public Load {
  public:
    StmtList() : Load(NodeType::StmtList) {}
};

inline std::shared_ptr<StmtList> stmt_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<StmtList>(ptr);
}

class StmtBlock : public Load {
  public:
    StmtBlock() : Load(NodeType::StmtBlock) {}
    // const std::string toString() const override;
};

inline std::shared_ptr<StmtBlock> stmt_block_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<StmtBlock>(ptr);
}

class ModuleDecl : public Load {
    std::string ident_;

  public:
    ModuleDecl(const std::string &ident) : Load(NodeType::ModuleDecl), ident_(ident) {}
    const std::string ident() const { return ident_; }
    const std::string toString() const override;
};

inline std::shared_ptr<ModuleDecl> module_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ModuleDecl>(ptr);
}

class ImportDecl : public Load {
    std::string path_;
    std::vector<std::string> idents_;

  public:
    ImportDecl(const std::string &path, const std::vector<std::string> &idents)
        : Load(NodeType::ImportDecl), path_(path), idents_(idents) {}
    const std::vector<std::string> idents() const { return idents_; }
    const std::string toString() const override;
};

inline std::shared_ptr<ImportDecl> import_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ImportDecl>(ptr);
}

class ExportDecl : public Load {
    std::vector<std::string> idents_;

  public:
    ExportDecl(const std::vector<std::string> &idents) : Load(NodeType::ExportDecl), idents_(idents) {}
    const std::string toString() const override;
};

inline std::shared_ptr<ExportDecl> export_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ExportDecl>(ptr);
}

class LambdaExpr : public Load {
  public:
    enum class Modifier { ATOMIC, SHARED, SYNC, MACRO };
    LambdaExpr(const std::string type, const std::vector<Modifier> &modifiers)
        : Load(NodeType::LambdaExpr), type_(type), modifiers_(modifiers) {}
    const std::string toString() const override;

  private:
    std::string type_;
    std::vector<Modifier> modifiers_;
};

inline std::shared_ptr<LambdaExpr> lambda_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LambdaExpr>(ptr);
}

class FuncDecl : public Load {
  public:
    enum class Modifier { ATOMIC, SHARED, SYNC, MACRO };
    FuncDecl(const std::string &implMark, const std::vector<Modifier> &modifiers, const std::string &name,
             const std::string &type)
        : Load(NodeType::FuncDecl), implMark_(implMark), modifiers_(modifiers), name_(name), type_(type) {}
    const std::string implMark() const { return implMark_; }
    const std::string name() const { return name_; }
    const std::string type() const { return type_; }
    const std::string toString() const override;

  private:
    std::string implMark_; // INNNER or OUTER
    std::vector<Modifier> modifiers_;
    std::string name_; // func name
    std::string type_; // func type
};

inline std::shared_ptr<FuncDecl> func_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncDecl>(ptr);
}

class LetDecl : public Load {
    std::string modifier_; // LET OR VAR
  public:
    LetDecl(const std::string &modifier) : Load(NodeType::LetDecl), modifier_(modifier) {}
    const std::string modifier() const { return modifier_; }
    const std::string toString() const override;
};

inline std::shared_ptr<LetDecl> let_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LetDecl>(ptr);
}

class UseDecl : public Load {
    std::string identDef_;
    std::string identRef_;

  public:
    UseDecl(const std::string &identDef, const std::string &identRef)
        : Load(NodeType::UseDecl), identDef_(identDef), identRef_(identRef) {}
    const std::string ident() const { return identRef_; }
    const std::string toString() const override;
};

inline std::shared_ptr<UseDecl> use_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<UseDecl>(ptr);
}

class RetStmt : public Load {
    std::string modifier_;

  public:
    RetStmt(std::string modifier) : Load(NodeType::RetStmt), modifier_(modifier) {}
    const std::string toString() const override;
};

inline std::shared_ptr<RetStmt> ret_stmt_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RetStmt>(ptr);
}

class TypeDecl : public Load {
    std::string implMark_; // INNNER or OUTER
    std::string ident_;
    std::string type_;

  public:
    TypeDecl(const std::string &implMark, const std::string &ident, const std::string &type)
        : Load(NodeType::TypeDecl), implMark_(implMark), ident_(ident), type_(type) {}
    const std::string implMark() const { return implMark_; }
    const std::string ident() const { return ident_; }
    const std::string type() const { return type_; }
    const std::string toString() const override;
};

inline std::shared_ptr<TypeDecl> type_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeDecl>(ptr);
}

class EnumDecl : public Load {
    std::string name_;
    std::string type_;

  public:
    EnumDecl(const std::string &name, const std::string &type) : Load(NodeType::EnumDecl), name_(name), type_(type) {}
    const std::string name() const { return name_; }
    const std::string type() const { return type_; }
    const std::string toString() const override;
};

inline std::shared_ptr<EnumDecl> enum_decl_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<EnumDecl>(ptr);
}

class KeyTypePair : public Load {
    std::string ident_;
    std::string type_;

  public:
    KeyTypePair(const std::string &ident, const std::string &type)
        : Load(NodeType::KeyTypePair), ident_(ident), type_(type) {}
    const std::string ident() const { return ident_; }
    const std::string type() const { return type_; }
    const std::string toString() const override;
};

inline std::shared_ptr<KeyTypePair> key_type_pair_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<KeyTypePair>(ptr);
}

class KeyValuePair : public Load {
    std::string ident_;

  public:
    KeyValuePair(const std::string &ident) : Load(NodeType::KeyValuePair), ident_(ident) {}
    const std::string ident() const { return ident_; }
    const std::string toString() const override;
};

inline std::shared_ptr<KeyValuePair> key_value_pair_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<KeyValuePair>(ptr);
}

class KeyParamPair : public Load {
    bool isVar_;
    std::string ident_;
    std::string type_;

  public:
    KeyParamPair(bool isVar, const std::string &ident, const std::string &type)
        : Load(NodeType::KeyParamPair), isVar_(isVar), ident_(ident), type_(type) {}
    const std::string ident() const { return ident_; }
    const std::string type() const { return type_; }
    bool isVar() { return isVar_; }
    const std::string toString() const override;
};

inline std::shared_ptr<KeyParamPair> key_param_pair_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<KeyParamPair>(ptr);
}

class IdentList : public Load {
    std::vector<std::string> idents_;

  public:
    IdentList(const std::vector<std::string> &idents) : Load(NodeType::IdentList), idents_(idents) {}
    const std::vector<std::string> idents() const { return idents_; }
    const std::string toString() const override;
};

inline std::shared_ptr<IdentList> ident_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<IdentList>(ptr);
}

class DataList : public Load {
  public:
    DataList() : Load(NodeType::DataList) {}
};

inline std::shared_ptr<DataList> data_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataList>(ptr);
}

class ValueList : public Load {
  public:
    ValueList() : Load(NodeType::ValueList) {}
};

inline std::shared_ptr<ValueList> value_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ValueList>(ptr);
}

class IndexValues : public Load {
  public:
    IndexValues() : Load(NodeType::IndexValues) {}
};

inline std::shared_ptr<IndexValues> index_values_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<IndexValues>(ptr);
}

class PairedValues : public Load {
  public:
    PairedValues() : Load(NodeType::PairedValues) {}
};

inline std::shared_ptr<PairedValues> paired_values_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<PairedValues>(ptr);
}

class PairedParams : public Load {
  public:
    PairedParams() : Load(NodeType::PairedParams) {}
};

inline std::shared_ptr<PairedParams> paired_params_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<PairedParams>(ptr);
}

class ArgumentList : public Load {
  public:
    ArgumentList() : Load(NodeType::ArgumentList) {}
};

inline std::shared_ptr<ArgumentList> argument_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ArgumentList>(ptr);
}

class MemberAccess : public Load {
  public:
    MemberAccess() : Load(NodeType::MemberAccess) {}
};

inline std::shared_ptr<MemberAccess> member_access_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<MemberAccess>(ptr);
}

class WaitExpr : public Load {
    bool isWait_;

  public:
    WaitExpr(bool isWait) : Load(NodeType::WaitExpr), isWait_(isWait) {}
    const std::string toString() const override;
};

inline std::shared_ptr<WaitExpr> wait_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WaitExpr>(ptr);
}

class Wildcard : public Load {
  public:
    Wildcard() : Load(NodeType::Wildcard) {}
};

inline std::shared_ptr<Wildcard> wildcard_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<Wildcard>(ptr);
}

class MatchCase : public Load {
  public:
    MatchCase() : Load(NodeType::MatchCase) {}
};

inline std::shared_ptr<MatchCase> match_case_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<MatchCase>(ptr);
}

class IfLoad : public Load {
  public:
    IfLoad() : Load(NodeType::IfLoad) {}
};

inline std::shared_ptr<IfLoad> if_load_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<IfLoad>(ptr);
}

class CatchClause : public Load {
    std::string ident_;
    std::string type_;

  public:
    CatchClause(const std::string &ident, const std::string &type)
        : Load(NodeType::CatchClause), ident_(ident), type_(type) {}
    const std::string ident() const { return ident_; }
    const std::string type() const { return type_; }
    const std::string toString() const override;
};

inline std::shared_ptr<CatchClause> catch_clause_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<CatchClause>(ptr);
}

class AssignExpr : public Load {
  public:
    enum class AssignOp {
        ASSIGN,
        ADD_ASSIGN,
        MINUS_ASSIGN,
        MUL_ASSIGN,
        DIV_ASSIGN,
        MOD_ASSIGN,
        POW_ASSIGN,
        MATRIX_MUL_ASSIGN,
        AND_ASSIGN,
        OR_ASSIGN,
        INVALID // for error handling
    };

    AssignExpr(AssignOp op) : Load(NodeType::AssignExpr), op_(op) {}
    const std::string toString() const override;

  private:
    AssignOp op_;
};

inline std::shared_ptr<AssignExpr> assign_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AssignExpr>(ptr);
}

class LogicalOrExpr : public Load {
  public:
    LogicalOrExpr() : Load(NodeType::LogicalOrExpr) {}
};

inline std::shared_ptr<LogicalOrExpr> logical_or_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LogicalOrExpr>(ptr);
}

class LogicalAndExpr : public Load {
  public:
    LogicalAndExpr() : Load(NodeType::LogicalAndExpr) {}
};

inline std::shared_ptr<LogicalAndExpr> logical_and_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LogicalAndExpr>(ptr);
}

class EqualityExpr : public Load {

  public:
    enum class EqualityOp {
        STRICT_EQUAL,
        NOT_STRICT_EQUAL,
        EQUAL,
        NOT_EQUAL,
        INVALID // for error handling
    };
    EqualityExpr(EqualityOp op) : Load(NodeType::EqualityExpr), op_(op) {}
    const std::string toString() const override;

  private:
    EqualityOp op_;
};

inline std::shared_ptr<EqualityExpr> equality_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<EqualityExpr>(ptr);
}

class RelationalExpr : public Load {
  public:
    enum class RelationalOp {
        LESS,
        GREATER,
        LESS_EQUAL,
        GREATER_EQUAL,
        INVALID // for error handling
    };
    RelationalExpr(RelationalOp op) : Load(NodeType::RelationalExpr), op_(op) {}
    const std::string toString() const override;

  private:
    RelationalOp op_;
};

inline std::shared_ptr<RelationalExpr> relational_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<RelationalExpr>(ptr);
}

class AdditiveExpr : public Load {
  public:
    enum class AdditiveOp {
        ADD,
        MINUS,
        INVALID // for error handling
    };
    AdditiveExpr(AdditiveOp op) : Load(NodeType::AdditiveExpr), op_(op) {}
    const std::string toString() const override;

  private:
    AdditiveOp op_;
};

inline std::shared_ptr<AdditiveExpr> additive_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AdditiveExpr>(ptr);
}

class MultiplicativeExpr : public Load {

  public:
    enum class MultiplicativeOp {
        MUL,
        DIV,
        POW,
        MATRIX_MUL,
        MOD,
        INVALID // for error handling
    };
    MultiplicativeExpr(MultiplicativeOp op) : Load(NodeType::MultiplicativeExpr), op_(op) {}
    const std::string toString() const override;

  private:
    MultiplicativeOp op_;
};

inline std::shared_ptr<MultiplicativeExpr> multiplicative_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<MultiplicativeExpr>(ptr);
}

class NullableExpr : public Load {
  public:
    enum class NullableOp {
        QUESTION_QUESTION,
        NOT_NOT,
        INVALID // for error handling
    };
    NullableExpr(NullableOp op) : Load(NodeType::NullableExpr), op_(op) {}
    const std::string toString() const override;

  private:
    NullableOp op_;
};

inline std::shared_ptr<NullableExpr> nullable_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<NullableExpr>(ptr);
}

class UnaryExpr : public Load {
  public:
    enum class UnaryOp {
        NOT,
        LINK,
        BIT_NOT,
        AS,
        IS,
        INVALID // for error handling
    };
    enum class TypeOp {
        IS,
        AS,
        INVALID // for error handling
    };
    UnaryExpr(UnaryOp unaryOp, TypeOp typeOp) 
        : Load(NodeType::UnaryExpr), unaryOp_(unaryOp), typeOp_(typeOp) {}
    const std::string toString() const override;

  private:
    UnaryOp unaryOp_;
    TypeOp typeOp_;
};

inline std::shared_ptr<UnaryExpr> unary_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<UnaryExpr>(ptr);
}

class LinkExpr : public Load {
  public:
    enum class LinkOp {
        ARROW,
        QUESTION_ARROW,
        INVALID // for error handling
    };
    LinkExpr(LinkOp op) : Load(NodeType::LinkExpr), op_(op) {}
    const std::string toString() const override;

  private:
    LinkOp op_;
};

inline std::shared_ptr<LinkExpr> link_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LinkExpr>(ptr);
}

class BindExpr : public Load {
  public:
    enum class BindOp {
        DOUBLE_DOT,
        QUESTION_DOUBLE_DOT,
        INVALID // for error handling
    };
    BindExpr(BindOp op) : Load(NodeType::BindExpr), op_(op) {}
    const std::string toString() const override;

  private:
    BindOp op_;
};

inline std::shared_ptr<BindExpr> bind_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<BindExpr>(ptr);
}

class WithExpr : public Load {

  public:
    enum class WithOp {
        DOT,
        QUESTION_DOT,
        INVALID // for error handling
    };
    WithExpr(WithOp op) : Load(NodeType::WithExpr), op_(op) {}
    const std::string toString() const override;

  private:
    WithOp op_;
};

inline std::shared_ptr<WithExpr> with_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<WithExpr>(ptr);
}

class AnnoExpr : public Load {
    bool isAssert_;

  public:
    AnnoExpr(bool isAssert) : Load(NodeType::AnnoExpr), isAssert_(isAssert) {}
    const std::string toString() const override;
};

inline std::shared_ptr<AnnoExpr> anno_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<AnnoExpr>(ptr);
}

class DictExpr : public Load {
  public:
    DictExpr() : Load(NodeType::DictExpr) {}
};

inline std::shared_ptr<DictExpr> dict_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DictExpr>(ptr);
}

class DictType : public Load{
  public:
    DictType() : Load(NodeType::DictType) {}
};

class ListExpr : public Load {
  public:
    ListExpr() : Load(NodeType::ListExpr) {}
};

inline std::shared_ptr<ListExpr> list_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ListExpr>(ptr);
}

class Literal : public Load {
  public:
    Literal(data_ptr_t data) : Load(NodeType::Literal), data_(data) {}
    const std::string toString() const override;

  private:
    data_ptr_t data_;
};

inline std::shared_ptr<Literal> literal_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<Literal>(ptr);
}

class TypeExpr : public Load {
  public:
    TypeExpr() : Load(NodeType::TypeExpr) {}
};

inline std::shared_ptr<TypeExpr> type_expr_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeExpr>(ptr);
}

class UnionType : public Load {
  public:
    UnionType() : Load(NodeType::UnionType) {}
    // const std::string toString() const override;
};

inline std::shared_ptr<UnionType> union_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<UnionType>(ptr);
}

class UnionUnit : public Load {
    std::string ident_;

  public:
    UnionUnit(std::string ident) : Load(NodeType::UnionUnit), ident_(ident) {}
    const std::string toString() const override;
};

inline std::shared_ptr<UnionUnit> union_unit_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<UnionUnit>(ptr);
}

class ListType_ : public Load {
    int dimonsion_;

  public:
    ListType_(int dimonsion) : Load(NodeType::ListType), dimonsion_(dimonsion) {}
    const std::string toString() const override;
};

inline std::shared_ptr<ListType_> list_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ListType_>(ptr);
}

class ArgsType : public Load {
  public:
    ArgsType() : Load(NodeType::ArgsType) {}
};

inline std::shared_ptr<ArgsType> args_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<ArgsType>(ptr);
}

class PrimaryType_ : public Load {
    std::string type_;
    std::string ident_;

  public:
    PrimaryType_(std::string type, std::string ident) : Load(NodeType::PrimaryType), type_(type), ident_(ident) {}
    const std::string toString() const override;
};

inline std::shared_ptr<PrimaryType_> primary_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<PrimaryType_>(ptr);
}

class DictExprType : public Load {
  public:
    enum class DictExprTypeOp {
        AMPERSAND,
        CARET,
        INVALID // for error handling
    };
    DictExprType(DictExprTypeOp op) : Load(NodeType::DictExprType), op_(op) {}
    const std::string toString() const override;

  private:
    DictExprTypeOp op_;
};

inline std::shared_ptr<DictExprType> dict_expr_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DictExprType>(ptr);
}

class TypeList : public Load {
  public:
    TypeList() : Load(NodeType::TypeList) {}
};

inline std::shared_ptr<TypeList> type_list_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<TypeList>(ptr);
}

class LambdaType : public Load {
  public:
    enum class LambdaTypeModifiers { ATOMIC, SHARED, SYNC, MACRO, INVALID };
    LambdaType(std::vector<LambdaTypeModifiers> modifiers) : Load(NodeType::LambdaType), modifiers_(modifiers) {}
    const std::string toString() const override;

  private:
    std::vector<LambdaTypeModifiers> modifiers_;
};

inline std::shared_ptr<LambdaType> lambda_type_load_ptr_cast(const load_ptr_t &ptr) {
    return std::dynamic_pointer_cast<LambdaType>(ptr);
}

} // namespace AbstractSyntaxTree

namespace AST = AbstractSyntaxTree;
