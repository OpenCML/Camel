// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Builds the abstract syntax tree from the concrete syntax tree of the hand-written parser.
 *
 * One function per production, as the grammar names them; where the tree has no node for a
 * production (the levels that only pass on, see node_kinds.def) the function that would have
 * handled it is one of the four dispatchers: visitDecl, visitStmt, visitExpr, visitType.
 *
 * The tree built here is, node for node, the one the ANTLR-based builder produced: same loads,
 * same token ranges, same origins and semantic bundles. The later stages did not change.
 */

#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "camel/core/error/diagnostics.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/ast/value.h"
#include "camel/parse/syntax/cst.h"

namespace camel::parse::ast {

/// The arguments of a call or a binding, as they are written: the positional ones, the named
/// ones, and the first positional one written after a named one (`f(a: 1, 2)`), which the call
/// refuses where it is built (design principles 4.4). The parser keeps the two
/// lists apart; the third field is the evidence that they were interleaved.
struct ArgumentValues {
    node_ptr_t data                   = nullptr;
    node_ptr_t named                  = nullptr;
    node_ptr_t positionalAfterNamed   = nullptr;
};

class Builder {
  public:
    Builder() = default;

    node_ptr_t build(const syntax::Tree &tree, camel::core::error::diagnostics_ptr_t diags) {
        diags_         = diags;
        sourceContext_ = diags ? diags->sourceContext() : nullptr;
        sourceFileId_  = camel::source::kInvalidSourceFileId;
        if (sourceContext_ && diags) {
            auto fileId = sourceContext_->findFileId(diags->modulePath());
            if (fileId.has_value()) {
                sourceFileId_ = *fileId;
            }
        }
        if (sourceContext_) {
            // One span and one origin per AST node, a few more for the anchors of names.
            sourceContext_->reserveOrigins(tree.nodeCount() + tree.nodeCount() / 2);
        }
        root_ = nullptr;
        namespaceStack_.clear();
        programPermissions_.clear();
        visitProgram(syntax::cst::Program(tree.root()));
        return root_;
    }

  private:
    node_ptr_t root_;
    camel::core::error::diagnostics_ptr_t diags_;
    camel::source::source_context_ptr_t sourceContext_;
    camel::source::source_file_id_t sourceFileId_ = camel::source::kInvalidSourceFileId;

    std::shared_ptr<ModuleLoad> module_ = std::make_shared<ModuleLoad>();
    /// The tags written before `module` (`#gpu module m`, design principles 13.4), made by
    /// visitModuleDecl and attached to the module node by visitProgram. Null when there are none.
    node_ptr_t moduleTagUses_ = nullptr;
    std::vector<std::shared_ptr<ImportLoad>> imports_;
    std::shared_ptr<ExportLoad> export_ = std::make_shared<ExportLoad>();
    std::vector<std::string> namespaceStack_;

    Reference qualifyRef(const Reference &ref) const;
    void pushNamespace(const Reference &ref);
    void popNamespace(size_t previousSize);

    // Dispatch on the kind of a node.
    Value visitDecl(syntax::Node context);
    Value visitStmt(syntax::Node context);
    Value visitBlockExpr(syntax::Node context);
    Value visitExpr(syntax::Node context);
    Value visitType(syntax::Node context);

    Value visitProgram(syntax::cst::Program context);
    Value visitStmtList(syntax::cst::StmtList context);
    Value visitModuleDecl(syntax::cst::ModuleDecl context);
    Value visitImportDecl(syntax::cst::ImportDecl context);
    Value visitExportDecl(syntax::cst::ExportDecl context);
    Value visitUsingNamespaceDecl(syntax::cst::UsingNamespaceDecl context);
    Value visitRuleDecl(syntax::cst::RuleDecl context);
    Value visitFactDecl(syntax::cst::FactDecl context);
    /// The import of the module `rewrite` that rule declarations need was made.
    bool ruleImportAdded_ = false;
    /// The imports a fact declaration needs (`__fact`, `graph.Node`) were made.
    bool factImportAdded_ = false;
    /// The imports a rule condition needs (`__rule_when`, `graph.Node`) were made.
    bool whenImportAdded_ = false;
    /// The import of the maker a generic rule needs (`__rule_generic`) was made: a rule whose head
    /// declares type parameters carries its templates and is made by that maker alone.
    bool genericRuleImportAdded_ = false;
    /// The import of `rewrite.Rule` was made: the function a rule with `with` parameters is
    /// written as gives a `Rule`, and the type is named in its signature.
    bool ruleTypeImportAdded_ = false;
    /// The import of `graph.Node` for a rule condition was made: a condition whose parameters are
    /// all `const` names values, not nodes, and does not need it.
    bool whenNodeImportAdded_ = false;
    /// The `node_of` names of the condition being built: a `const` parameter of a rule that the
    /// condition reaches with `node_of(c)`, and the hidden node parameter appended to the
    /// condition for it. Empty outside a rule condition, so `node_of` written anywhere else is
    /// refused by name.
    std::map<std::string, std::string> ruleWhenNodeParams_;
    /// The rule whose condition is being built, for the refusal above.
    std::string ruleWhenName_;
    Value visitBlockStmt(syntax::cst::BlockStmt context);
    Value visitStmtBlock(syntax::cst::StmtBlock context);
    Value visitFuncData(syntax::cst::FuncData context);
    Value visitFuncAnno(syntax::cst::FuncAnno context);
    /// Rejects the removed `@name(args)` spelling at the annotation, naming the tag form that
    /// replaced it (design principles 7.2: a numeric permission is a tag). Plain decorators
    /// (`@name`, `@name<args>`) are not affected.
    bool rejectParenthesizedName(syntax::cst::FuncAnno context);
    /// Rejects the removed `@vjp<rule>` decorator at the annotation, naming the declaration tag
    /// that replaced it (design principles 6.2, 8: a custom derivative rule is a tag).
    bool rejectVjpAnnotation(syntax::cst::FuncAnno context);
    Value visitFuncDecl(syntax::cst::FuncDecl context);
    Value visitTagDecl(syntax::cst::TagDecl context);
    /// The permission groups the program declares (`tag fast = [reassociate, contract]`), by
    /// the name they are declared with, expanded to the permissions their members grant. A
    /// `#requires(fast)` written in the same program can name one of them (design principles
    /// 7.2: a permission is a tag, and a group of permissions is a tag too). Built before the
    /// declarations are visited, because a rule's requirement is read while it is parsed.
    void collectProgramPermissionGroups(syntax::cst::Program program);
    /// The permissions the program's group `name` grants; 0 when `name` is not a group of
    /// permissions (including a name the program does not declare).
    camel::core::sem::PermissionMask programPermissionOf(std::string_view name) const;
    std::map<std::string, camel::core::sem::PermissionMask> programPermissions_;
    /// The tag uses of one `#` form, one node each (a `#[a, b]` group makes one for each
    /// member). An argument list is attached to the use as a RepeatedLoad("Data").
    std::vector<node_ptr_t> tagUsesOf(syntax::cst::TagUse context);
    /// The tag uses written before `context`, attached to `owner` as its next child, a
    /// RepeatedLoad("TagUse"); nothing when it has none.
    void addTagUses(const node_ptr_t &owner, const syntax::Node &context);
    /// The tag uses written in `context` as one RepeatedLoad("TagUse"), or null when there are
    /// none. A module's tags are made this way: their owner (the module node) exists only after
    /// the declaration has been visited.
    node_ptr_t tagUseListOf(const syntax::Node &context);
    Value visitNamespaceDecl(syntax::cst::NamespaceDecl context);
    /// `topLevel`: a declaration of the module or of a namespace block, whose names take the
    /// path of the enclosing namespaces (`namespace Config { let limit = 10 }` declares
    /// `Config::limit`, design principles 10.2); a local declaration does not.
    Value visitDataDecl(syntax::cst::DataDecl context, bool topLevel = false);
    Value visitTypeDecl(syntax::cst::TypeDecl context);
    Value visitResourceDecl(syntax::cst::ResourceDecl context);
    Value visitResourceClause(syntax::cst::ResourceClause context);
    Value visitRetStmt(syntax::cst::RetStmt context);
    Value visitRestValue(syntax::cst::RestValue context);
    Value visitKeyTypePair(syntax::cst::KeyTypePair context);
    Value visitKeyValuePair(syntax::cst::KeyValuePair context);
    Value visitKeyParamPair(syntax::cst::KeyParamPair context);
    Value visitTupleParam(syntax::cst::TupleParam context);
    Value visitDataList(syntax::cst::DataList context);
    Value visitIdentList(syntax::cst::IdentList context);
    Value visitIndexValues(syntax::cst::IndexValues context);
    Value visitPairedValues(syntax::cst::PairedValues context);
    Value visitPairedParams(syntax::cst::PairedParams context);
    Value visitStrayPositional(syntax::cst::StrayPositional context);
    Value visitArgumentList(syntax::cst::ArgumentList context);
    Value visitIndices(syntax::cst::Indices context);
    Value visitParentParams(syntax::cst::ParentParams context);
    Value visitParentArgues(syntax::cst::ParentArgues context);
    Value visitAngledParams(syntax::cst::AngledParams context);
    Value visitAngledValues(syntax::cst::AngledValues context);
    Value visitPattern(syntax::cst::Pattern context);
    Value visitMatchCase(syntax::cst::MatchCase context);
    Value visitMatchTypeExpr(syntax::cst::MatchTypeExpr context);
    Value visitTypeCase(syntax::cst::TypeCase context);
    Value visitStaticForExpr(syntax::cst::StaticForExpr context);
    Value visitStaticLoopExpr(syntax::cst::StaticLoopExpr context);
    TypeCaseKind typeCaseKindOf(syntax::cst::TypeCasePattern pattern);
    /// IfExpr, MatchExpr, MatchTypeExpr, StaticForExpr.
    Value visitCtrlExpr(syntax::Node context);
    Value visitLoopExpr(syntax::cst::LoopExpr context);
    Value visitAssignExpr(syntax::cst::AssignExpr context);
    Value visitLogicalOrExpr(syntax::cst::LogicalOrExpr context);
    Value visitLogicalAndExpr(syntax::cst::LogicalAndExpr context);
    /// OrExpr (`a | b`) and AndExpr (`a & b`).
    Value visitEagerLogicExpr(syntax::Node context);
    Value visitEqualityExpr(syntax::cst::EqualityExpr context);
    Value visitRelationalExpr(syntax::cst::RelationalExpr context);
    Value visitAdditiveExpr(syntax::cst::AdditiveExpr context);
    Value visitMultiplicativeExpr(syntax::cst::MultiplicativeExpr context);
    Value visitNullableExpr(syntax::cst::NullableExpr context);
    /// CastExpr (`e as T`, `e is T`) and UnaryExpr (`!e`, `-e`, `~e`).
    Value visitUnaryExpr(syntax::Node context);
    Value visitLinkExpr(syntax::cst::LinkExpr context);
    Value visitCompExpr(syntax::cst::CompExpr context);
    Value visitAnnoExpr(syntax::cst::AnnoExpr context);
    Value visitAccessExpr(syntax::cst::AccessExpr context);
    /// `lhs.member` (a field or tuple element), located by `range`.
    node_ptr_t
    makeMemberAccess(node_ptr_t lhs, syntax::Child dot, syntax::Child member, syntax::Node range);
    /// `lhs.[key]`: a field or tuple element named by a compile-time key.
    node_ptr_t
    makeStaticAccess(node_ptr_t lhs, syntax::Child dot, syntax::Node key, syntax::Node range);
    Value visitStructData(syntax::cst::StructData context);
    Value visitArrayData(syntax::cst::ArrayData context);
    Value visitTupleData(syntax::cst::TupleData context);
    /// `T { static for ... }`: the rounds' values under the field names and kind of `T`.
    Value visitShapeComp(syntax::cst::ShapeComp context);
    Value visitRefData(syntax::cst::RefData context);
    Value visitConstasData(syntax::cst::ConstasData context);
    Value visitLiteralData(syntax::cst::Literal context);
    /// A formatted string (`FString`): `'...'->format<...>`, built here as that same tree.
    Value visitFStringData(syntax::cst::Literal context);
    Value visitLiteral(syntax::cst::Literal context);
    Value visitTypeExpr(syntax::cst::TypeExpr context);
    Value visitInterType(syntax::cst::InterType context);
    Value visitDiffType(syntax::cst::DiffType context);
    Value visitKeyUnionDiffType(syntax::cst::KeyUnionDiffType context);
    Value visitKeyInterType(syntax::cst::KeyInterType context);
    Value visitArrayType(syntax::cst::ArrayType context);
    Value visitArrayDim(syntax::cst::ArrayDim context);
    /// The extents and the pack one `[...]` was written with.
    DimGroup dimGroupOf(syntax::cst::ArrayDim context);
    Value visitConstData(syntax::cst::ConstData context);
    Value visitSpecType(syntax::cst::SpecType context);
    Value visitRefType(syntax::cst::RefType context);
    Value visitTypeOfType(syntax::cst::TypeOfType context);
    Value visitStructType(syntax::cst::StructType context);
    Value visitEnumType(syntax::cst::EnumType context);
    Value visitEnumPattern(syntax::cst::Pattern context, syntax::cst::IdentRef ref);
    Value visitTypeList(syntax::cst::TypeList context);
    Value visitTupleType(syntax::cst::TupleType context);
    Value visitFuncType(syntax::cst::FuncType context);
    Value visitIdentDef(syntax::cst::IdentDef context);
    Value visitIdentRef(syntax::cst::IdentRef context);
};

} // namespace camel::parse::ast

