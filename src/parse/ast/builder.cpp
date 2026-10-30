// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/builder.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/parse/ast/type_case_words.h"
#include "camel/core/operator_names.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/tags.h"

#include <cctype>
#include <cstdint>
#include <functional>
#include <set>

#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"
#include "camel/parse/syntax/report.h"

#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"
#include "camel/utils/utf8.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace AST;
using namespace camel::core::error;
namespace syn = camel::parse::syntax;
namespace cst = camel::parse::syntax::cst;
using TK      = camel::parse::syntax::TokenKind;
using NK      = camel::parse::syntax::NodeKind;

namespace {
thread_local camel::source::source_context_ptr_t g_sourceContext = nullptr;
thread_local camel::source::source_file_id_t g_sourceFileId = camel::source::kInvalidSourceFileId;
// Where the nodes of the tree being built are allocated (value.h); empty outside a build.
thread_local std::shared_ptr<NodeArena> g_arena = nullptr;

struct ArenaScope {
    std::shared_ptr<NodeArena> previous = std::exchange(g_arena, std::make_shared<NodeArena>());
    ~ArenaScope() { g_arena = std::move(previous); }
};

inline camel::source::origin_id_t nodeOrigin(const node_ptr_t &node) {
    return node ? node->load()->origin() : camel::source::kInvalidOriginId;
}

// A token as a diagnostic shows it: quoted, on one line, not longer than a few words.
inline std::string quoteForDiagnostic(std::string text) {
    if (const size_t nl = text.find('\n'); nl != std::string::npos) {
        text = text.substr(0, nl) + "...";
    }
    if (text.size() > 24) {
        size_t cut = 20;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut) + "...";
    }
    return "'" + text + "'";
}

inline void setNodeOriginByOffsets(
    const node_ptr_t &node, size_t startOffset, size_t endOffset,
    const std::string &label = "ast") {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId) {
        return;
    }
    auto span = g_sourceContext->createSpan(g_sourceFileId, startOffset, endOffset);
    if (span == camel::source::kInvalidSpanId) {
        return;
    }
    auto origin = g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
    node->load()->setOrigin(origin);
}

inline syn::TokenRef tokenOf(syn::Child child) { return child.token(); }

// Where a diagnostic placed at tokens points: from the first character of `start` to the end
// of `end`. The end column is the start column of `end` plus its length in bytes, on the line
// `end` starts on (what the positions always were, also for a token with non-ASCII text).
inline CharRange rangeOf(syn::TokenRef start, syn::TokenRef end) {
    CharRange r{{0, 0}, {0, 0}};
    if (start) {
        r.start = {start.line(), start.column()};
    }
    const syn::TokenRef last = end ? end : start;
    if (last) {
        r.end = {last.line(), last.column() + (last.end() - last.begin())};
    }
    return r;
}
inline CharRange rangeOf(syn::TokenRef token) { return rangeOf(token, token); }

// Whether an expression reads a name as data: a `RefData` of that text, at any depth (`x * c`
// reads `x` and `c`). The left side of a rule is what binds its `const` parameters, and a read
// there is the position that matches the constant. A member name (`x.c`) and a type are not
// `RefData` nodes, so neither is read here, and neither binds anything.
inline bool readsDataName(syn::Node node, const std::string &name) {
    if (!node) {
        return false;
    }
    if (node.is(NK::RefData) && node.text() == name) {
        return true;
    }
    for (const syn::Node &child : node.nodes()) {
        if (readsDataName(child, name)) {
            return true;
        }
    }
    return false;
}

inline camel::source::origin_id_t
deriveAstAnchorOrigin(const node_ptr_t &owner, syn::TokenRef token, const std::string &label) {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId || !owner ||
        !token) {
        return camel::source::kInvalidOriginId;
    }
    auto span = g_sourceContext->createSpan(
        g_sourceFileId,
        static_cast<size_t>(token->begin()),
        static_cast<size_t>(token->end()));
    if (span == camel::source::kInvalidSpanId) {
        return camel::source::kInvalidOriginId;
    }
    auto parent = owner->load()->origin();
    if (parent != camel::source::kInvalidOriginId) {
        return g_sourceContext->deriveOrigin(
            parent,
            camel::source::OriginStage::AST,
            camel::source::OriginKind::AstNode,
            label,
            span);
    }
    return g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
}

inline camel::source::origin_id_t
deriveAstAnchorOrigin(const node_ptr_t &owner, syn::Node context, const std::string &label) {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId || !context ||
        !context->firstToken() || !context->lastToken()) {
        return camel::source::kInvalidOriginId;
    }
    auto span = g_sourceContext->createSpan(
        g_sourceFileId,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(context->lastToken()->end()));
    if (span == camel::source::kInvalidSpanId) {
        return camel::source::kInvalidOriginId;
    }
    auto parent = owner ? owner->load()->origin() : camel::source::kInvalidOriginId;
    if (parent != camel::source::kInvalidOriginId) {
        return g_sourceContext->deriveOrigin(
            parent,
            camel::source::OriginStage::AST,
            camel::source::OriginKind::AstNode,
            label,
            span);
    }
    return g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
}

inline camel::source::SemanticPart semanticPart(
    camel::source::SemanticRole role, camel::source::origin_id_t origin, int32_t slot = -1,
    const std::string &label = "") {
    return camel::source::SemanticPart{
        .role   = role,
        .origin = origin,
        .slot   = slot,
        .label  = label};
}

/// Appends one `Parameter` part per parameter written in a function type load (`with` parameters
/// first, then the ordinary ones; each with its slot in the signature and the position it is
/// written at). A port of the function's graph stands for one parameter, so that what is said
/// about a parameter -- the refusal of a function value that came in through it -- points at the
/// parameter itself. The list keeps its own `params` part as well, for what reads the parameter
/// list as one place.
inline void appendParameterParts(
    std::vector<camel::source::SemanticPart> &parts, const node_ptr_t &funcTypeNode) {
    if (!funcTypeNode || funcTypeNode->size() < 2) {
        return;
    }
    const auto add = [&](const node_ptr_t &params, size_t firstSlot) {
        if (!params) {
            return;
        }
        for (size_t i = 0; i < params->size(); ++i) {
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Parameter,
                nodeOrigin(params->at(i)),
                static_cast<int32_t>(firstSlot + i),
                "param"));
        }
    };
    add(funcTypeNode->at(0), 0);
    add(funcTypeNode->at(1), funcTypeNode->at(0)->size());
}

inline void registerAstSemanticBundle(
    const node_ptr_t &node, std::vector<camel::source::SemanticPart> parts = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    if (!g_sourceContext || !node) {
        return;
    }
    auto mainOrigin = node->load()->origin();
    if (mainOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    g_sourceContext->registerAstSemantic(
        mainOrigin,
        camel::source::SemanticBundle{
            .mainOrigin      = mainOrigin,
            .parts           = std::move(parts),
            .mergedInputs    = std::move(mergedInputs),
            .synthetic       = synthetic,
            .syntheticReason = syntheticReason});
}
} // namespace

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    if (g_arena) {
        std::shared_ptr<Load> load = std::allocate_shared<LoadType>(
            NodeArenaAllocator<LoadType>(g_arena),
            std::forward<Args>(args)...);
        return std::allocate_shared<Node>(NodeArenaAllocator<Node>(g_arena), std::move(load));
    }
    return std::make_shared<Node>(
        std::dynamic_pointer_cast<Load>(std::make_shared<LoadType>(std::forward<Args>(args)...)));
}

template <typename LoadType> std::shared_ptr<LoadType> unwrapNodeAs(std::shared_ptr<Node> &node) {
    return std::dynamic_pointer_cast<LoadType>(node->load());
}

inline TokenRange extractTokenRangeFromContext(syn::Node context) {
    if (context && context->firstToken() && context->lastToken()) {
        return {context->firstToken()->index(), context->lastToken()->index() + 1};
    } else {
        return {0, 0};
    }
}

inline void setNodeTokenRange(const node_ptr_t &node, TokenRange range) {
    node->load()->setTokenRange(range);
}
inline void setNodeTokenRange(const node_ptr_t &node, size_t start, size_t end) {
    node->load()->setTokenRange(start, end);
}

inline void setNodeTokenRangeByContext(const node_ptr_t &node, syn::Node context) {
    node->load()->setTokenRange(extractTokenRangeFromContext(context));
    if (context && context->firstToken() && context->lastToken()) {
        setNodeOriginByOffsets(
            node,
            static_cast<size_t>(context->firstToken()->begin()),
            static_cast<size_t>(context->lastToken()->end()));
    }
}

/// The range from the first of two sibling nodes to the last.
inline void setNodeTokenRangeBetween(const node_ptr_t &node, syn::Node first, syn::Node last) {
    if (!first || !last) {
        node->load()->setTokenRange(0, 0);
        return;
    }
    size_t start = first->firstToken()->index();
    size_t end   = last->lastToken()->index() + 1;
    node->load()->setTokenRange(start, end);
    setNodeOriginByOffsets(
        node,
        static_cast<size_t>(first->firstToken()->begin()),
        static_cast<size_t>(last->lastToken()->end()));
}

/// The documentation of a declaration: the `///` lines right above its first token, or above
/// the `export` in front of it.
inline std::string docOf(syn::Node decl) {
    if (!decl || decl.empty()) {
        return {};
    }
    syn::Node outer = decl;
    if (decl.parent() && decl.parent().is(NK::ExportDecl)) {
        outer = decl.parent();
    }
    return decl.tree()->tokens().docAbove(outer.firstToken().index());
}

inline node_ptr_t any2node(Value &&a) { return std::move(a.as<node_ptr_t>()); }
inline node_ptr_t any2node(Value &a) { return a.as<node_ptr_t>(); }

Reference Builder::qualifyRef(const Reference &ref) const {
    if (namespaceStack_.empty()) {
        return ref;
    }

    std::vector<std::string> paths = namespaceStack_;
    paths.insert(paths.end(), ref.paths().begin(), ref.paths().end());
    return Reference(paths, ref.ident());
}

void Builder::pushNamespace(const Reference &ref) {
    namespaceStack_.insert(namespaceStack_.end(), ref.paths().begin(), ref.paths().end());
    namespaceStack_.push_back(ref.ident());
}

void Builder::popNamespace(size_t previousSize) {
    ASSERT(previousSize <= namespaceStack_.size(), "Invalid namespace stack restore point");
    namespaceStack_.resize(previousSize);
}

/*
program : SEP? (decl SEP?)* EOF;
*/
void Builder::collectProgramPermissionGroups(cst::Program program) {
    // The names and members of the groups this program declares (`tag fast = [reassociate,
    // contract]`). Only the module's own, top-level declarations: a requirement is read while
    // the rule that carries it is parsed, before the GCT builder registers anything.
    std::map<std::string, std::vector<std::string>> groups;
    for (const syn::Node &decl : program->nodes()) {
        if (!decl.is(NK::TagDecl)) {
            continue;
        }
        const cst::TagDecl tag(decl);
        const auto name = tag->get<cst::IdentDef>();
        if (!name) {
            continue;
        }
        std::vector<std::string> members;
        if (const auto group = tag->get<cst::BracketIdents>()) {
            if (const auto list = group->get<cst::IdentList>()) {
                for (const auto ident : list->all<cst::IdentDef>()) {
                    members.emplace_back(ident->text());
                }
            }
        }
        if (!members.empty()) {
            groups.emplace(name->text(), std::move(members));
        }
    }
    std::function<camel::core::sem::PermissionMask(const std::string &, std::set<std::string> &)>
        expand = [&](const std::string &name,
                     std::set<std::string> &visiting) -> camel::core::sem::PermissionMask {
        // A core permission (including `finite_math` and its members) grants itself; a group
        // grants what its members grant. A cycle grants nothing and is not followed.
        if (const auto permission = camel::core::sem::parsePermission(name)) {
            return *permission;
        }
        const auto it = groups.find(name);
        if (it == groups.end() || !visiting.insert(name).second) {
            return 0;
        }
        camel::core::sem::PermissionMask mask = 0;
        for (const std::string &member : it->second) {
            mask |= expand(member, visiting);
        }
        return mask;
    };
    for (const auto &[name, members] : groups) {
        std::set<std::string> visiting;
        programPermissions_[name] = expand(name, visiting);
    }
}

camel::core::sem::PermissionMask Builder::programPermissionOf(std::string_view name) const {
    if (const auto it = programPermissions_.find(std::string(name)); it != programPermissions_.end()) {
        return it->second;
    }
    // A qualified requirement written for a group of this module (`m::fast`).
    if (const size_t sep = name.rfind("::"); sep != std::string_view::npos) {
        if (const auto it = programPermissions_.find(std::string(name.substr(sep + 2)));
            it != programPermissions_.end()) {
            return it->second;
        }
    }
    return 0;
}

Value Builder::visitProgram(cst::Program context) {
    ENTER("Program");
    g_sourceContext = sourceContext_;
    g_sourceFileId  = sourceFileId_;
    const ArenaScope arena;

    collectProgramPermissionGroups(context);

    root_ = std::make_shared<Node>(module_);

    node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmts");
    setNodeTokenRangeByContext(stmts, context);
    for (const syn::Node decl : context->nodes()) {
        if (decl.is(NK::Error)) {
            continue;
        }
        Value res = visitDecl(decl);
        if (res.has_value()) {
            if (res.type() == typeid(node_ptr_t)) {
                node_ptr_t node = any2node(res);
                if (node) {
                    *stmts << node;
                }
            } else if (res.type() == typeid(std::vector<node_ptr_t>)) {
                for (const auto &node : value_cast<std::vector<node_ptr_t>>(res)) {
                    if (node) {
                        *stmts << node;
                    }
                }
            }
        }
    }

    node_ptr_t importRep = createNodeAs<RepeatedLoad>("Import");
    for (auto &import_ : imports_) {
        *importRep << std::make_shared<Node>(import_);
    }
    *root_ << importRep;

    node_ptr_t exportOpt = createNodeAs<OptionalLoad>("Export");
    if (!export_->isEmpty()) {
        *exportOpt << std::make_shared<Node>(export_);
    }
    *root_ << exportOpt;

    *root_ << stmts;

    LEAVE("Program");
    return root_;
}

/*
decl : importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
     | dataDecl | funcDecl | typeDecl ;
namespaceItem : the same without the module-level declarations.
The tree has no node for either rule: the declaration itself is the child.
*/
Value Builder::visitDecl(syn::Node context) {
    switch (context.kind()) {
    case NK::ImportDecl:
        return visitImportDecl(cst::ImportDecl(context));
    case NK::ExportDecl:
        return visitExportDecl(cst::ExportDecl(context));
    case NK::NamespaceDecl:
        return visitNamespaceDecl(cst::NamespaceDecl(context));
    case NK::UsingNamespaceDecl:
        return visitUsingNamespaceDecl(cst::UsingNamespaceDecl(context));
    case NK::DataDecl:
        return visitDataDecl(cst::DataDecl(context), true);
    case NK::FuncDecl:
        return visitFuncDecl(cst::FuncDecl(context));
    case NK::TypeDecl:
        return visitTypeDecl(cst::TypeDecl(context));
    case NK::ResourceDecl:
        return visitResourceDecl(cst::ResourceDecl(context));
    case NK::TagDecl:
        return visitTagDecl(cst::TagDecl(context));
    case NK::RuleDecl:
        return visitRuleDecl(cst::RuleDecl(context));
    case NK::FactDecl:
        return visitFactDecl(cst::FactDecl(context));
    default:
        return Value();
    }
}

/*
stmt : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt | blockStmt ;
The tree has no node for the rule: the statement itself is the child.
*/
Value Builder::visitStmt(syn::Node context) {
    if (context.isExpr()) {
        node_ptr_t expr = any2node(visitExpr(context));
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context);
        *stmt << expr;
        return stmt;
    }
    switch (context.kind()) {
    case NK::DataDecl:
        return visitDataDecl(cst::DataDecl(context));
    case NK::FuncDecl:
        return visitFuncDecl(cst::FuncDecl(context));
    case NK::TypeDecl:
        return visitTypeDecl(cst::TypeDecl(context));
    case NK::UsingNamespaceDecl:
        return visitUsingNamespaceDecl(cst::UsingNamespaceDecl(context));
    case NK::RetStmt:
        return visitRetStmt(cst::RetStmt(context));
    case NK::BlockStmt:
        return visitBlockStmt(cst::BlockStmt(context));
    case NK::StmtBlock:
        return visitStmtBlock(cst::StmtBlock(context));
    default:
        throw std::runtime_error(
            "Builder: not a statement: " + std::string(syn::nodeKindName(context.kind())));
    }
}

/*
stmtList : stmt (SEP? stmt)* SEP? ;
*/
Value Builder::visitStmtList(cst::StmtList context) {
    ENTER("StmtList");
    node_ptr_t block = createNodeAs<StmtBlockLoad>();
    setNodeTokenRangeByContext(block, context);
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Stmt");
    setNodeTokenRangeByContext(rep, context);
    for (const syn::Node stmt : context->nodes()) {
        if (!stmt.is(NK::Error)) {
            *rep << any2node(visitStmt(stmt));
        }
    }
    *block << rep;
    LEAVE("StmtList");
    return block;
}

/*
importDecl : IMPORT (moduleName (AS identDef)? | (importItem | bracedImports) FROM moduleName) ;
*/
Value Builder::visitImportDecl(cst::ImportDecl context) {
    ENTER("ImportDecl");
    auto import_ = std::make_shared<ImportLoad>();
    // Where the import is: a module that is not found is reported here.
    import_->setTokenRange(extractTokenRangeFromContext(context));
    if (context->get<cst::ModuleName>()) {
        import_->setPath(context->get<cst::ModuleName>()->text());
    }
    if (context->get<cst::IdentDef>()) {
        // `import m as x`: the module under another name (design principles 10.2).
        import_->setAs(Reference(context->get<cst::IdentDef>()->text()));
    } else if (context->get<cst::BracedImports>() || context->get<cst::ImportItem>()) {
        // importItem : identDef ('as' identDef)? -- the name in the module, then the name
        // it has here. `import a from m` is `import { a } from m` (design principles 10.2).
        std::vector<Reference> refs;
        std::vector<Reference> locals;
        bool renamed     = false;
        const auto items = context->get<cst::BracedImports>()
                               ? context->get<cst::BracedImports>()->all<cst::ImportItem>()
                               : context->all<cst::ImportItem>();
        for (const auto item : items) {
            const auto name  = item->get<cst::IdentDef>(0);
            const auto local = item->get<cst::IdentDef>(1);
            if (!name) {
                continue;
            }
            refs.emplace_back(name->text());
            locals.emplace_back(local ? local->text() : name->text());
            renamed = renamed || static_cast<bool>(local);
        }
        import_->setRefs(refs);
        if (renamed) {
            import_->setLocals(locals);
        }
    }
    imports_.push_back(import_);
    LEAVE("ImportDecl");
    return Value();
}

/*
exportDecl : EXPORT (dataDecl | typeDecl | bracedIdents) ;
*/
Value Builder::visitExportDecl(cst::ExportDecl context) {
    ENTER("ExportDecl");
    node_ptr_t res = nullptr;
    if (context->get<cst::DataDecl>()) {
        res                  = any2node(visitDataDecl(context->get<cst::DataDecl>(), true));
        const auto &dataDecl = res->loadAs<DataDeclLoad>();
        for (const auto &ref : dataDecl->refs()) {
            export_->addRef(ref);
        }
    } else if (context->get<cst::TypeDecl>()) {
        res = any2node(visitTypeDecl(context->get<cst::TypeDecl>()));
        export_->addRef(res->loadAs<TypeDeclLoad>()->ref());
    } else if (context->get<cst::BracedIdents>()) {
        for (const auto ident :
             context->get<cst::BracedIdents>()->get<cst::IdentList>()->all<cst::IdentDef>()) {
            export_->addRef(qualifyRef(Reference(ident->text())));
        }
    }
    LEAVE("ExportDecl");
    if (res) {
        return res;
    } else {
        return Value();
    }
}

/*
usingNamespaceDecl : USING NAMESPACE identRef ;
*/
Value Builder::visitUsingNamespaceDecl(cst::UsingNamespaceDecl context) {
    ENTER("UsingNamespaceDecl");
    // As written: what it names is found where it is used (design principles 10.2).
    Reference ref        = value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>()));
    node_ptr_t usingNode = createNodeAs<UsingNamespaceLoad>(ref);
    setNodeTokenRangeByContext(usingNode, context);
    LEAVE("UsingNamespaceDecl");
    return usingNode;
}

/*
blockStmt  : stmtBlock ;
*/
Value Builder::visitBlockStmt(cst::BlockStmt context) {
    ENTER("BlockStmt");
    node_ptr_t res = any2node(visitStmtBlock(context->get<cst::StmtBlock>()));
    // A decorator before a block decorates the block as a function of no parameters called where
    // it is written (language reference 7.6): the decorators are the block's second child. The
    // removed `@name(args)` spelling is reported here and grants nothing.
    node_ptr_t decorators = nullptr;
    for (const auto &anno : context->all<cst::FuncAnno>()) {
        if (rejectParenthesizedName(anno) || rejectVjpAnnotation(anno)) {
            continue;
        }
        if (!decorators) {
            decorators = createNodeAs<RepeatedLoad>("FuncAnno");
        }
        *decorators << any2node(visitFuncAnno(anno));
    }
    if (decorators) {
        *res << decorators;
    }
    // The tag uses written before the block (`#tag { ... }`, design principles 6.2): a third
    // child, after the decorators.
    addTagUses(res, context);
    LEAVE("BlockStmt");
    return res;
}

/*
stmtBlock  : SYNC? '{' stmtList? '}' ;
*/
Value Builder::visitStmtBlock(cst::StmtBlock context) {
    ENTER("StmtBlock");
    node_ptr_t blockNode = nullptr;
    if (context->get<cst::StmtList>()) {
        blockNode = any2node(visitStmtList(context->get<cst::StmtList>()));
        if (context->tok(TK::KwSync)) {
            // If SYNC is present, we treat this as a synchronous block statement.
            unwrapNodeAs<StmtBlockLoad>(blockNode)->sync();
        }
    } else {
        // If no statements are present, we create an empty block.
        blockNode = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(blockNode, context);
        *blockNode << createNodeAs<RepeatedLoad>("Stmt");
    }
    LEAVE("StmtBlock");
    return blockNode;
}

/*
blockExpr  : stmtBlock | dataExpr ;
The tree has no node for the rule: the block or the expression itself is the child.
*/
Value Builder::visitBlockExpr(syn::Node context) {
    node_ptr_t res = nullptr;
    if (context.is(NK::StmtBlock)) {
        res = any2node(visitStmtBlock(cst::StmtBlock(context)));
    } else if (context) {
        node_ptr_t expr = any2node(visitExpr(context));
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context);
        res = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(res, context);
        node_ptr_t rep = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(rep, context);
        *stmt << expr;
        *rep << stmt;
        *res << rep;
    }
    return res;
}

/*
funcData   : modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr ;
*/
Value Builder::visitFuncData(cst::FuncData context) {
    ENTER("FuncData");
    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    setNodeTokenRangeByContext(funcTypeNode, context);
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
    }
    funcType->setModifiers(modSet);

    if (context->get<cst::AngledParams>()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->get<cst::AngledParams>()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->get<cst::ParentParams>()) {
        node_ptr_t normParams = any2node(visitParentParams(context->get<cst::ParentParams>()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(
        typeOptNode,
        context->firstToken()->index(),
        (context->type() ? context->type()->lastToken()->index()
                         : context->get<cst::ParentParams>()->firstToken()->index()) +
            1);
    setNodeOriginByOffsets(
        typeOptNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>((context->type() ? context->type()->lastToken()
                                             : context->get<cst::ParentParams>()->firstToken())
                                ->end()));
    if (context->type()) {
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    node_ptr_t blockNode = any2node(visitBlockExpr(context->lastChild().node()));

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>();
    setNodeTokenRangeByContext(funcNode, context);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;
    std::vector<camel::source::SemanticPart> semanticParts{
        semanticPart(
            camel::source::SemanticRole::GenericParameter,
            nodeOrigin(funcTypeNode->at(0)),
            -1,
            "withParams"),
        semanticPart(
            camel::source::SemanticRole::Parameter,
            nodeOrigin(funcTypeNode->at(1)),
            -1,
            "params"),
        semanticPart(
            camel::source::SemanticRole::ReturnType,
            typeOptNode->empty() ? camel::source::kInvalidOriginId
                                 : nodeOrigin(typeOptNode->front()),
            -1,
            "returnType"),
        semanticPart(
            camel::source::SemanticRole::ValueProducer,
            nodeOrigin(blockNode),
            -1,
            "body"),
    };
    // Each parameter as written, with its own position and its slot in the signature: a port of
    // the function's graph stands for one parameter, and what is said about a parameter points at
    // where it is written.
    appendParameterParts(semanticParts, funcTypeNode);
    registerAstSemanticBundle(funcNode, std::move(semanticParts));

    LEAVE("FuncData");
    return funcNode;
}

/*
funcAnno   : '@' identRef (angledValues | parentIdents)? ;
A decorator (`@name`, `@name<args>`). Names in parentheses are the removed `@numeric(...)`
spelling: it is reported here, where it is written, and names the tag form that replaced it.
*/
bool Builder::rejectParenthesizedName(cst::FuncAnno context) {
    if (!context->get<cst::ParentIdents>()) {
        return false;
    }
    const std::string annotation = context->get<cst::IdentRef>()->text();
    const std::string why =
        annotation == "numeric"
            ? "the numeric permissions are tags now: write '#[reassociate]' (or "
              "'#[reassociate, contract]', '#[finite_math]', ...) before the function or the "
              "block"
            : "an annotation takes '<...>' arguments, not '(...)'; a tag is written '#name', "
              "'#name(args)' or '#[a, b]'";
    diags_->of(SemanticDiag::InvalidAnnotation)
        .at(rangeOf(context->firstToken(), context->lastToken()))
        .commit(annotation, why);
    return true;
}

bool Builder::rejectVjpAnnotation(cst::FuncAnno context) {
    const std::string annotation = context->get<cst::IdentRef>()->text();
    if (annotation != "vjp" && annotation != "jvp") {
        return false;
    }
    const std::string tag    = annotation == "vjp" ? "vjp" : "jvp";
    diags_->of(SemanticDiag::InvalidAnnotation)
        .at(rangeOf(context->firstToken(), context->lastToken()))
        .commit(
            annotation,
            "a derivative rule is a declaration tag now: write '#" + tag +
                "(rule)' before the function (design principles 6.2, 8)");
    return true;
}

Value Builder::visitFuncAnno(cst::FuncAnno context) {
    ENTER("FuncAnno");

    Reference ref = value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>()));
    if (!context->get<cst::AngledValues>()) {
        // `@name`: the decorator itself.
        node_ptr_t refNode = createNodeAs<RefDataLoad>(ref);
        setNodeTokenRangeByContext(refNode, context);
        LEAVE("FuncAnno");
        return refNode;
    }
    // `@name<args>`: the decorator bound to its with-arguments.
    node_ptr_t annoNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
    setNodeTokenRangeByContext(annoNode, context);
    *annoNode << createNodeAs<RefDataLoad>(ref);
    const ArgumentValues values = value_cast<ArgumentValues>(
        visitAngledValues(context->get<cst::AngledValues>()));
    *annoNode << values.data << values.named;

    LEAVE("FuncAnno");
    return annoNode;
}

/*
funcDecl   :
        funcAnno*
        (WITH angledParams)?
        EXPORT? modifiers?
        FUNC identRef parentParams (':' typeExpr)? stmtBlock ;
*/
/*
ruleDecl : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
           parentParams whenClause? '{' ruleBody '}' ;
ruleBody : dataExpr ('=>' | '<=>') dataExpr ;

A rule is the shorthand of a pair of functions with the same parameters, the left one the pattern
and the right one what it becomes (rewrite design 2.1, graph store 5.2), and of the value that
holds them (rewrite design 2 (second version) 8.5):

    func name__lhs(params) => left
    func name__rhs(params) => right
    let name = __rule('name', twoWay, needs, consts, name__lhs, name__rhs)

`__rule` is an operator of the module `rewrite`, imported by the declaration itself. The rule is
the value `name` (type `Rule`): it is held in a variable, passed and returned like any value,
stored in a `Rule[]`, exported and imported. `consts` has bit i set when the parameter i is
`const`.

A rule that declares `with` parameters is a function of them instead (design principles 5.5):

    with <tile: int>
    rule tile_matmul(a: f32[m, k], b: f32[k, n]) when m % tile == 0 { ... }

makes the three sides functions of `tile` as well, and

    with <tile: int>
    func tile_matmul(): Rule => __rule_when('tile_matmul', ..., tile_matmul__lhs<tile>, ...)

so that `tile_matmul<64>` -- the same spelling a function's partial application has -- calls it
and gives the `Rule`, and `tile_matmul` alone does too when every parameter has a default.
*/
Value Builder::visitRuleDecl(cst::RuleDecl context) {
    ENTER("RuleDecl");
    const std::string name = std::string(context->get<cst::IdentDef>()->text());
    const Reference ref    = qualifyRef(Reference(name));
    if (context->has(TK::KwExport)) {
        export_->addRef(ref);
    }
    const auto body   = context->get<cst::RuleBody>();
    const auto sides  = body->exprs();
    // The head of the rule's left side, read here, where the left side is still syntax: at the
    // stage a module's declarations are read a rule's `__lhs` has no body, so nothing later can
    // read the head from the graphs. Only a ruleset module is indexed -- its name says so -- and
    // the module is named explicitly, by the diagnostics the module is being read with.
    if (diags_ != nullptr && sides[0]) {
        std::string shape;
        std::string head   = leftHeadOf(sides[0], shape);
        const auto range   = extractTokenRangeFromContext(context);
        collectRuleHead(
            diags_->moduleName(), std::move(head), name, std::move(shape), range.start, range.end);
    }
    const bool twoWay = body->has(TK::Equiv);
    const auto params = context->get<cst::ParentParams>();
    // The rule's `with` parameters, written before `export` as a function writes them (design
    // principles 4.4, 5.5). They are the rule's own, so the left side, the right side and the
    // condition are all functions of them and read them by name, and the value the rule is
    // written as is a function of them too.
    const auto withParams = context->get<cst::AngledParams>();
    // The rule's applicability condition is part of its signature, so it is written between the
    // parameters and the body (`rule add_zero::<T>(x: T) when is_integer::<T>() { … }`, design
    // principles 4.4). A trailing `when` is a syntax error and is
    // reported by name (`SyntaxDiag::WhenAfterBody`); its expression is still read here, so the
    // refusal that follows is about the placement and not about a missing condition.
    const auto whenClause = context->get<cst::WhenClause>();
    const auto when       = whenClause ? whenClause->expr() : context->expr();
    // A rule declares its type parameters the way a function does, after its name
    // (`rule sub_self::<T>(x: T) { ... }`): every function the rule becomes is generic over
    // them, so the sides bind and instantiate like an ordinary generic function.
    std::vector<std::string> typeParams;
    if (const auto declared = context->get<cst::TypeParams>()) {
        for (const auto ident : declared->all<cst::IdentDef>()) {
            typeParams.emplace_back(ident->text());
        }
    }
    int64_t consts    = 0;
    if (const auto paired = params ? params->get<cst::PairedParams>() : cst::PairedParams()) {
        int bit = 0;
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (pair->has(TK::KwConst)) {
                consts |= int64_t{1} << bit;
            }
            ++bit;
        }
    }

    // A `const` parameter is matched by the left side, and the left side is what binds it: the
    // parameter it is declared with is the constant's value in the condition, and the node of
    // that constant is the hidden node parameter the condition takes when the declaration has
    // one. A left side that reads no such name gives the parameter no position to match a
    // constant in, so the declaration is refused here, where it is written: the rule says
    // nothing about the program, and a condition that reaches the node through `node_of` has no
    // node to read -- which is the state the engine's own guards report as an internal error at
    // every match. `consts` is what the rule value carries (`__rule`), and it is read here the
    // same way, so the check covers exactly the parameters the engine treats as constant.
    if (params) {
        const auto paired = params->get<cst::PairedParams>();
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (!pair->has(TK::KwConst)) {
                continue;
            }
            const auto ident = pair->get<cst::IdentDef>();
            if (!ident) {
                continue;
            }
            const std::string written(ident->text());
            if (readsDataName(sides[0], written)) {
                continue;
            }
            diags_->of(SemanticDiag::RuleConstParamNotMatched)
                .at(rangeOf(ident->firstToken(), ident->lastToken()))
                .commit(written, name);
        }
    }

    // A `with` parameter of a rule is a compile-time value the rule is specialised by: every
    // function the declaration becomes -- the two sides and the condition -- takes it as a
    // `constas` parameter, so the instance an application makes reads it from a static slot and
    // both sides may write it (design principles 5.4, 5.5). The grammar writes the type of a `with`
    // parameter of a rule, and `constasParams` below reads every pair it writes: a pair without one
    // is the parser's to refuse, not this pass's.
    // `#requires(...)`: the numeric permissions the rule needs (design principles 7.2). The
    // arguments are the names of the standard permission tags, not values: a permission tag is
    // a compile-time fact, so the requirement is checked here, when the rule is compiled, and
    // an argument that names no permission is a located diagnostic rather than a silent pass.
    // The mask is handed to `__rule`, which the rewrite engines compare with the policy (a rule
    // whose requirement is not met is not applied).
    camel::core::sem::PermissionMask needs = 0;
    for (const auto use : context->all<cst::TagUse>()) {
        for (const auto tagRef : use->all<cst::TagRef>()) {
            const auto ident = tagRef->get<cst::IdentRef>();
            const std::string written(ident->text());
            if (written != "requires") {
                diags_->of(SemanticDiag::RuleTagNotRequires)
                    .at(rangeOf(ident->firstToken(), ident->lastToken()))
                    .commit(written);
                continue;
            }
            const syn::TokenBuffer &tokens = context.tree()->tokens();
            const size_t begin             = tagRef->firstToken().index();
            const size_t end               = tagRef->lastToken().index();
            size_t argCount                = 0;
            const auto resolve             = [&](size_t first, size_t last) {
                while (first <= last && syn::isTrivia(tokens[first].kind)) {
                    ++first;
                }
                while (last >= first && last != SIZE_MAX && syn::isTrivia(tokens[last].kind)) {
                    --last;
                }
                if (first > last || last == SIZE_MAX) {
                    return;
                }
                ++argCount;
                std::string name;
                for (size_t i = first; i <= last; ++i) {
                    if (!syn::isTrivia(tokens[i].kind)) {
                        name += tokens.text(i);
                    }
                }
                camel::core::sem::PermissionMask permission =
                    camel::core::tag::permissionOfName(name);
                if (permission == 0) {
                    // A group this program declares (`tag fast = [reassociate, contract]`):
                    // its declaration is not in the core's registry, so it is expanded here.
                    permission = programPermissionOf(name);
                }
                if (permission != 0) {
                    needs = static_cast<camel::core::sem::PermissionMask>(needs | permission);
                    return;
                }
                const auto at =
                    rangeOf(syn::TokenRef(context.tree(), first), syn::TokenRef(context.tree(), last));
                // `#requires` names the core's permission tags; a name that is not one (a typo,
                // or a tag of the program that grants no permission) is reported here, at the
                // argument, with the list of the permissions that can be named.
                diags_->of(SemanticDiag::TagNotAPermission).at(at).commit(name);
            };
            bool inArguments = false;
            size_t argFrom   = 0;
            for (size_t i = begin; i <= end; ++i) {
                if (syn::isTrivia(tokens[i].kind)) {
                    continue;
                }
                const std::string_view text = tokens.text(i);
                if (!inArguments) {
                    if (text == "(") {
                        inArguments = true;
                        argFrom     = i + 1;
                    }
                    continue;
                }
                if (text == ")") {
                    resolve(argFrom, i == 0 ? 0 : i - 1);
                    inArguments = false;
                } else if (text == ",") {
                    resolve(argFrom, i == 0 ? 0 : i - 1);
                    argFrom = i + 1;
                }
            }
            if (!inArguments && argCount == 0) {
                // `#requires` without any argument requires nothing, which is never
                // what was meant, so say how many arguments the form takes.
                diags_->of(SemanticDiag::TagArgumentCount)
                    .at(rangeOf(tagRef->firstToken(), tagRef->lastToken()))
                    .commit("requires", "1", "0");
            }
        }
    }

    if (!ruleImportAdded_ && typeParams.empty()) {
        ruleImportAdded_ = true;
        auto import_     = std::make_shared<ImportLoad>();
        import_->setTokenRange(extractTokenRangeFromContext(context));
        import_->setPath("rewrite");
        import_->setRefs({Reference("__rule")});
        imports_.push_back(import_);
    }

    // The rule's `with` parameters as the `constas` parameters of every function the declaration
    // becomes: same names, same declared types, in the order they are written, ahead of the rule's
    // own parameters. A `constas` parameter is a compile-time value, so it is no port of a side: the
    // ports stay the rule's own parameters, and the value an application binds is read from the
    // instance's static slot (design principles 5.4, 5.5).
    const auto constasParams = [&](node_ptr_t ownParams) {
        node_ptr_t list = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(list, context);
        if (withParams) {
            for (const auto pair : withParams->get<cst::PairedParams>()->all<cst::KeyParamPair>()) {
                const auto ident = pair->get<cst::IdentDef>();
                if (!ident || !pair->type()) {
                    continue;
                }
                node_ptr_t named = createNodeAs<NamedPairLoad>(
                    Reference(std::string(ident->text())), false, false, false, true);
                setNodeTokenRangeByContext(named, pair);
                *named << any2node(visitType(pair->type()));
                node_ptr_t noDefault = createNodeAs<OptionalLoad>("DataExpr");
                setNodeTokenRangeByContext(noDefault, pair);
                *named << noDefault;
                *list << named;
            }
        }
        // The rule's own parameters follow the `with` ones. They come from `ownParams`, a list
        // the caller builds to hand them over and keeps nothing else of, so each one is taken
        // out of it before it is attached: a node belongs to the container that lists it and to
        // no other (tree.h, `operator<<`), and the pair `visitPairedParams` attached to
        // `ownParams` still has that list as its parent here.
        while (!ownParams->empty()) {
            *list << ownParams->takeAt(0);
        }
        return list;
    };

    // One side as a function of the rule's parameters: `func name__<side>(params) => expr`. A side
    // is read as a pattern the engine matches and rebuilds with, so its ports are the rule's own
    // parameters; a `with` parameter is a `constas` parameter of it, which the instance reads from
    // a static slot, and either side may write it (design principles 5.5).
    const auto side = [&](const std::string &suffix, syn::Node expr) {
        const Reference sideRef = qualifyRef(Reference(name + suffix));
        node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(funcTypeNode, context);
        unwrapNodeAs<FuncTypeLoad>(funcTypeNode)->setTypeParamNames(typeParams);
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
        *funcTypeNode << constasParams(
            params ? any2node(visitParentParams(params)) : createNodeAs<RepeatedLoad>("NamedPair"));
        node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(typeOptNode, context);
        *funcTypeNode << typeOptNode;

        node_ptr_t funcNode = createNodeAs<FuncDataLoad>(sideRef);
        setNodeTokenRangeByContext(funcNode, context);
        *funcNode << funcTypeNode;
        *funcNode << any2node(visitBlockExpr(expr));

        node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(sideRef);
        *funcDeclNode << funcNode;
        setNodeTokenRangeByContext(funcDeclNode, context);
        return funcDeclNode;
    };
    node_ptr_t lhsFunc = side("__lhs", sides[0]);
    node_ptr_t rhsFunc = side("__rhs", sides[1]);

    // A condition is a fourth function of the same parameters: `func name__when(params) => expr`.
    // A parameter that is not `const` is a node of the draft being rewritten (`graph::Node`); a
    // `const` one is the constant's value, of the type it was declared with (rewrite design 2
    // (second version) 1), so the guard is written on the value
    // and needs neither `constant(node, sample)` nor the `match … case null` layers around it.
    node_ptr_t whenFunc;
    if (when) {
        // The condition of this rule is being built: `node_of(c)` is read here and nowhere else
        // and the name of the rule is what
        // a refusal names.
        ruleWhenName_ = name;
        const auto paired = params ? params->get<cst::PairedParams>() : cst::PairedParams();
        const auto constValueParam = [](cst::KeyParamPair pair) {
            return static_cast<bool>(pair->has(TK::KwConst)) && static_cast<bool>(pair->type());
        };
        bool nodesUsed = false;
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                nodesUsed = nodesUsed || !constValueParam(pair);
            }
        }
        if (!whenImportAdded_ && typeParams.empty()) {
            whenImportAdded_   = true;
            auto rewriteImport = std::make_shared<ImportLoad>();
            rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
            rewriteImport->setPath("rewrite");
            rewriteImport->setRefs({Reference("__rule_when")});
            imports_.push_back(rewriteImport);
        }
        if (nodesUsed && !whenNodeImportAdded_) {
            whenNodeImportAdded_ = true;
            auto graphImport     = std::make_shared<ImportLoad>();
            graphImport->setTokenRange(extractTokenRangeFromContext(context));
            graphImport->setPath("graph");
            graphImport->setRefs({Reference("Node")});
            imports_.push_back(graphImport);
        }
        const Reference whenRef = qualifyRef(Reference(name + "__when"));
        // The hidden node parameter of every `const` parameter (the rule on
        // `rule_when_const_node`: "条件函数按 `const` 形参追加隐藏的节点形参，构建期把
        // `node_of(c)` 换成它；不引入新类型"). The condition takes the rule's parameters first,
        // in the order they are written, and then one node per `const` parameter, in the same
        // order. `node_of(c)` becomes the name of that parameter, and the map is what refuses a
        // name that is not one of this rule's own `const` parameters.
        std::vector<std::string> whenNodeNames;
        // Each hidden parameter is the node of a `const` parameter, so its value type is the
        // declared type of that parameter (`node_of(c)` with `const c: int` is a `Node<int>`).
        std::vector<std::pair<std::string, syn::Node>> whenNodeTypes;
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (constValueParam(pair)) {
                    whenNodeNames.emplace_back(
                        std::string(pair->get<cst::IdentDef>()->text()) + "__node");
                    whenNodeTypes.emplace_back(whenNodeNames.back(), pair->type());
                }
            }
        }
        for (const std::string &nodeName : whenNodeNames) {
            ruleWhenNodeParams_.emplace(nodeName.substr(0, nodeName.size() - 6), nodeName);
        }
        // A rule's parameter as the condition reads it: the declaration's type is the type of the
        // value the node stands for, so the parameter is a node whose value type is that type
        // (`x: T` is `Node<T>`, `x: int` is `Node<int>`), and a parameter written without a type is
        // a node whose value type the declaration does not state (design principles 5.9). The node
        // of a value the condition is handed carries the same type, so a query in the condition
        // reads the fact of a value of the type the rule declared.
        const auto nodeParamType = [&](cst::KeyParamPair at) -> node_ptr_t {
            if (!at->type()) {
                node_ptr_t bare = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(bare, at);
                return bare;
            }
            node_ptr_t applied = createNodeAs<SpecTypeLoad>();
            setNodeTokenRangeByContext(applied, at);
            node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
            setNodeTokenRangeByContext(arguments, at);
            *arguments << any2node(visitType(at->type()));
            node_ptr_t head = createNodeAs<RefTypeLoad>(Reference("Node"));
            setNodeTokenRangeByContext(head, at);
            *applied << head << arguments;
            return applied;
        };
        node_ptr_t whenParams   = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(whenParams, context);
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                node_ptr_t named = createNodeAs<NamedPairLoad>(
                    Reference(std::string(pair->get<cst::IdentDef>()->text())),
                    false,
                    false);
                setNodeTokenRangeByContext(named, pair);
                node_ptr_t paramType;
                if (constValueParam(pair)) {
                    paramType = any2node(visitType(pair->type()));
                } else {
                    paramType = nodeParamType(pair);
                }
                *named << paramType << createNodeAs<OptionalLoad>("DataExpr");
                *whenParams << named;
            }
        }
        for (const auto &[nodeName, nodeValueType] : whenNodeTypes) {
            node_ptr_t named = createNodeAs<NamedPairLoad>(Reference(nodeName), false, false);
            setNodeTokenRangeByContext(named, context);
            node_ptr_t paramType;
            if (nodeValueType) {
                paramType = createNodeAs<SpecTypeLoad>();
                setNodeTokenRangeByContext(paramType, context);
                node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
                setNodeTokenRangeByContext(arguments, context);
                *arguments << any2node(visitType(nodeValueType));
                node_ptr_t head = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(head, context);
                *paramType << head << arguments;
            } else {
                paramType = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(paramType, context);
            }
            *named << paramType << createNodeAs<OptionalLoad>("DataExpr");
            *whenParams << named;
        }
        node_ptr_t whenType = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(whenType, when);
        unwrapNodeAs<FuncTypeLoad>(whenType)->setTypeParamNames(typeParams);
        // The condition is a function of the rule's `with` parameters as the sides are, so it
        // reads them by name (`when m % tile == 0`) and they are `constas` parameters of it too.
        *whenType << createNodeAs<RepeatedLoad>("NamedPair");
        *whenType << constasParams(whenParams);
        node_ptr_t whenResult = createNodeAs<OptionalLoad>("Type");
        node_ptr_t boolType   = createNodeAs<RefTypeLoad>(Reference("bool"));
        setNodeTokenRangeByContext(boolType, when);
        *whenResult << boolType;
        *whenType << whenResult;
        node_ptr_t whenData = createNodeAs<FuncDataLoad>(whenRef);
        setNodeTokenRangeByContext(whenData, when);
        *whenData << whenType << any2node(visitBlockExpr(when));
        // `node_of` reaches a node only here, in the condition of the rule it belongs to: the two
        // sides and every other condition of the program meet an empty map and are refused.
        ruleWhenNodeParams_.clear();
        ruleWhenName_.clear();
        whenFunc = createNodeAs<FuncDeclLoad>(whenRef);
        setNodeTokenRangeByContext(whenFunc, when);
        *whenFunc << whenData;
    }

    // let name = __rule('name', twoWay, needs, consts, name__lhs, name__rhs)
    const auto literal = [&](LiteralType type, const std::string &text) {
        node_ptr_t literalNode = createNodeAs<LiteralLoad>(Literal(type, text));
        setNodeTokenRangeByContext(literalNode, context);
        return literalNode;
    };
    const auto reference = [&](const std::string &text) {
        node_ptr_t refNode = createNodeAs<RefDataLoad>(Reference(text));
        setNodeTokenRangeByContext(refNode, context);
        return refNode;
    };
    // One side as the maker takes it: the name of the side's function; or, for the condition, which
    // is a function of the rule's `with` parameters, that name with the parameters bound
    // (`name__when<tile>`), the partial application of the condition. The condition is called by
    // the engine with the values its function value carries, so the value reaches it there, while
    // the two sides are patterns and take the rule's parameters alone.
    node_ptr_t args = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(args, context);
    *args << literal(LiteralType::String, name);
    *args << literal(LiteralType::Boolean, twoWay ? "true" : "false");
    *args << literal(LiteralType::Integer, std::to_string(needs));
    *args << literal(LiteralType::Integer, std::to_string(consts));
    *args << reference(name + "__lhs");
    *args << reference(name + "__rhs");
    std::string makerName = "__rule";
    if (whenFunc) {
        *args << reference(name + "__when");
        makerName = "__rule_when";
    }
    // A rule that is instantiated -- one whose head declares type parameters, or whose `with`
    // parameters the instance is specialised by -- has no function values to give: its functions
    // are templates (a generic function is instantiated where it is called, and a function of a
    // `constas` parameter has no value either). The value carries the id the maker registers the
    // templates under, and the values bound to the `with` parameters follow the id, so the rule
    // value is a `Rule` of the values it was bound to (design principles 5.4, 5.5).
    const bool instantiated = !typeParams.empty() || withParams;
    if (instantiated) {
        makerName = "__rule_generic";
        if (!genericRuleImportAdded_) {
            genericRuleImportAdded_  = true;
            auto rewriteImport = std::make_shared<ImportLoad>();
            rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
            rewriteImport->setPath("rewrite");
            rewriteImport->setRefs({Reference("__rule_generic")});
            imports_.push_back(rewriteImport);
        }
        // The value of each `with` parameter, in the order it is declared: the maker puts them in
        // the rule value, and the instance the engine makes reads them from there.
        if (withParams) {
            for (const auto pair : withParams->get<cst::PairedParams>()->all<cst::KeyParamPair>()) {
                *args << reference(std::string(pair->get<cst::IdentDef>()->text()));
            }
        }
    }
    node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(call, context);
    *call << reference(makerName) << args << createNodeAs<RepeatedLoad>("NamedData");

    if (withParams) {
        // A rule with `with` parameters is a function of them (design principles 5.5):
        // `with <tile: int> func name(): Rule => __rule_generic('name', ..., name__lhs, name__rhs,
        // tile)`. `name<64>`, the spelling a function's partial application has, calls it and gives
        // the `Rule`; used as it is (`name`), the call is made with the defaults, so a rule whose
        // parameters all have one is a `Rule` value by its name alone.
        if (!ruleTypeImportAdded_) {
            ruleTypeImportAdded_ = true;
            auto ruleImport      = std::make_shared<ImportLoad>();
            ruleImport->setTokenRange(extractTokenRangeFromContext(context));
            ruleImport->setPath("rewrite");
            ruleImport->setRefs({Reference("Rule")});
            imports_.push_back(ruleImport);
        }
        node_ptr_t bindType = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(bindType, context);
        unwrapNodeAs<FuncTypeLoad>(bindType)->setTypeParamNames({});
        *bindType << any2node(visitAngledParams(withParams));
        *bindType << createNodeAs<RepeatedLoad>("NamedPair");
        node_ptr_t bindResult = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(bindResult, context);
        node_ptr_t ruleType = createNodeAs<RefTypeLoad>(Reference("Rule"));
        setNodeTokenRangeByContext(ruleType, context);
        *bindResult << ruleType;
        *bindType << bindResult;

        node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(stmts, context);
        node_ptr_t valueStmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(valueStmt, context);
        *valueStmt << call;
        *stmts << valueStmt;
        node_ptr_t bindBody = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(bindBody, context);
        *bindBody << stmts;

        node_ptr_t bindData = createNodeAs<FuncDataLoad>(ref);
        setNodeTokenRangeByContext(bindData, context);
        *bindData << bindType << bindBody;
        node_ptr_t bindDecl = createNodeAs<FuncDeclLoad>(ref);
        setNodeTokenRangeByContext(bindDecl, context);
        // The function the declaration is written as, not one the program wrote: the graph
        // builder marks its graph, and a use of the name as a value makes the call that binds
        // the parameters instead of a function value (`ruleBindingValueOf`).
        unwrapNodeAs<FuncDeclLoad>(bindDecl)->setRuleBinding();
        *bindDecl << bindData;

        LEAVE("RuleDecl");
        std::vector<node_ptr_t> bound{lhsFunc, rhsFunc};
        if (whenFunc) {
            bound.push_back(whenFunc);
        }
        bound.push_back(bindDecl);
        return bound;
    }

    // A rule without `with` parameters is a module-level value, not a function:
    // `let name = __rule(...)` (rewrite design 2 (second version) 8.5). A value can be held,
    // passed, returned and put in a `Rule[]`; a function that made one could only be called.
    node_ptr_t typeList = createNodeAs<RepeatedLoad>("Type");
    setNodeTokenRangeByContext(typeList, context);
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    *dataList << call;
    node_ptr_t valueDecl =
        createNodeAs<DataDeclLoad>(false, std::vector<Reference>{ref});
    unwrapNodeAs<DataDeclLoad>(valueDecl)->setDoc(docOf(context));
    setNodeTokenRangeByContext(valueDecl, context);
    *valueDecl << typeList << dataList;
    // The AST tooling reads the binding and the initializer of a declaration; a rule declaration
    // has the same two, under the name of the rule.
    std::vector<camel::source::SemanticPart> parts;
    const auto nameToken = context->get<cst::IdentDef>();
    parts.push_back(semanticPart(
        camel::source::SemanticRole::BindingName,
        nameToken ? deriveAstAnchorOrigin(valueDecl, nameToken, "ast.data.binding")
                  : nodeOrigin(valueDecl),
        0,
        name));
    parts.push_back(semanticPart(
        camel::source::SemanticRole::ValueProducer,
        nodeOrigin(valueDecl->atAs<RepeatedLoad>(1)),
        -1,
        "initializer"));
    registerAstSemanticBundle(valueDecl, std::move(parts));

    LEAVE("RuleDecl");
    std::vector<node_ptr_t> declarations{lhsFunc, rhsFunc};
    if (whenFunc) {
        declarations.push_back(whenFunc);
    }
    declarations.push_back(valueDecl);
    return declarations;
}

/*
factDecl : ('with' angledParams)? 'export'? 'fact' identDef typeParams? parentParams? ':' typeExpr
           'match' logicalOrExpr '{' factCase* '}' factClause*
factCase : 'case' factPattern '=>' dataExpr
factPattern : 'const' identDef (':' typeExpr)? | dataExpr
factClause : IDENTIFIER dataExpr

A fact is compiled to plain functions and one call of `__fact`:

    func depth__initial() => 0
    func depth__join(a0: int, a1: int) => max(a0, a1)
    func depth__case0(c: int) => c                      (a `const` case: the matcher binds the
    func depth__case0_fact(c: int) => 1                  value, not a node)
    func depth__case1(x: int, y: int) => x + y          (a case over the construction: the
    func depth__case1_fact(x: Node, y: Node) => max(depth::<int>(x), depth::<int>(y)) + 1
    func depth(v: Node<int>): int => __fact('depth', depth__initial, depth__join, ...)     matcher reads it,
                                                          nothing runs it)

The cases are the engine's clauses: a pattern function carries the shape the matcher reads (its
parameters are the pattern variables, typed `int`; a `const` case declares the type of its
constant), and a fact function computes the fact of a value that matched. `case _`, which must be
the last case, becomes `initial`: what the fact says of a value no other case matched, and what a
value that depends on itself settles on.

Each trailing clause names one binary function, and the fact merges two facts by calling it:
`join` (required) is how the facts of two writings of one value are merged, `meet` how equivalent
writings meet, `widen` accelerates a loop and `narrow` tightens it afterwards. `meet`, `widen` and
`narrow` default to `join`; the first version computes no loop acceleration. A clause is read in a
function of its own, of the fact's type, so that a name of several overloads (or a lambda) is
resolved where the fact's type is known.

The declaration's `with` parameters are the arguments the query is written with
(`depth<8>(x)`, and one with a default may be left out). They are copied onto the query function
and marked `const` there: the compiler must compute the argument, since the argument is part of
which fact this is, and the engine computes and caches one fact per argument. Every function the
declaration becomes carries them as well, so a case or a trailing clause names one and reads the
value: on the query they are `const`, and on the functions the engine calls they are plain
`with` parameters, which the engine fills with the arguments the query was written with.

The head parameters are the parts of a value the query is written on
(`aligned(x, y)` for `fact aligned(a: int, b: int)`), and are `Node` in the generated function; a
parameter whose type the head writes is a node of that value type (`Node<T>`, `Node<int>`), so a
caller's node gives the query its type arguments and a `const` case's constant is the head's type.
A declaration with none is the one-value form (`depth(n)`).

A fact may declare type parameters (`fact range::<T>(v: T): Interval`, rewrite design 7.5): every
function the declaration becomes is generic over them, so an instance of the query is an instance
of all of them at once. The query names them, in the order the declaration writes them, as the
argument after the fact's name; the type arguments of the instance replace that text, and they are
part of which fact this is: two queries at two types are two instances, computed and kept apart.
*/
Value Builder::visitFactDecl(cst::FactDecl context) {
    ENTER("FactDecl");
    if (!factImportAdded_) {
        factImportAdded_ = true;
        auto rewriteImport = std::make_shared<ImportLoad>();
        rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
        rewriteImport->setPath("rewrite");
        rewriteImport->setRefs({Reference("__fact")});
        imports_.push_back(rewriteImport);
        auto graphImport = std::make_shared<ImportLoad>();
        graphImport->setTokenRange(extractTokenRangeFromContext(context));
        graphImport->setPath("graph");
        graphImport->setRefs({Reference("Node")});
        imports_.push_back(graphImport);
    }

    const std::string name = std::string(context->get<cst::IdentDef>()->text());
    const Reference ref    = qualifyRef(Reference(name));
    if (context->has(TK::KwExport)) {
        export_->addRef(ref);
    }
    // The type parameters, as the declaration writes them (`fact range::<T>(v: T)`): every
    // function the declaration becomes is generic over them, and the query names them to the
    // engine so that the instance it is asked for says which types its functions were made at.
    std::vector<std::string> typeParams;
    if (const auto declared = context->get<cst::TypeParams>()) {
        for (const auto ident : declared->all<cst::IdentDef>()) {
            typeParams.emplace_back(ident->text());
        }
    }
    std::string typeParamNames;
    for (const std::string &one : typeParams) {
        typeParamNames += (typeParamNames.empty() ? "" : ",") + one;
    }
    const auto typeExprs = context->types();
    if (typeExprs.size() != 1) {
        // A fact declaration writes the type of the fact after its parameters, and that is the
        // only type it writes here (`fact depth(n): int`): the cases and the `join` all give a
        // value of it (rewrite design 7.5). The declaration that writes none is refused by the
        // parser, where the `:` and its type belong, and a file the parser refused is never given
        // an AST, so no program reaches this; the guard keeps the read below off an empty list.
        ASSERT(false, "a fact declaration with no single type reached the builder");
        LEAVE("FactDecl");
        return Value();
    }
    const syn::Node factType = typeExprs[0];

    std::vector<syn::Node> cases;
    if (const auto body = context->get<cst::FactBody>()) {
        for (const auto each : body->all<cst::FactCase>()) {
            cases.push_back(each);
        }
    }
    // One case takes whatever the others do not, and it is written last: the value of `case _` is
    // also what a value that depends on itself settles on, so every fact needs it (rewrite design
    // 7.5). A body that writes no case at all is the same defect -- the parser asks for a case
    // where the body is empty, before this builder is reached -- and `cases.back()` is only read
    // once there is one.
    const syn::Node lastPattern =
        cases.empty() ? syn::Node() : cases.back()->get<cst::FactPattern>();
    if (!lastPattern || !lastPattern->has(TK::Underscore)) {
        diags_->of(SemanticDiag::FactCaseDefaultMissing)
            .at(rangeOf(context->firstToken()))
            .commit(name);
        LEAVE("FactDecl");
        return Value();
    }
    const syn::Node initialRhs = cases.back()->expr(0);

    // The trailing clauses, as written. Each names one binary function; only `join` is required.
    syn::Node joinClause, meetClause, widenClause, narrowClause;
    for (const auto clause : context->all<cst::FactClause>()) {
        const std::string keyword = clause->tok(TK::Identifier).text();
        const auto exprs          = clause->exprs();
        if (exprs.size() != 1) {
            continue;
        }
        if (keyword == "join") {
            joinClause = exprs[0];
        } else if (keyword == "meet") {
            meetClause = exprs[0];
        } else if (keyword == "widen") {
            widenClause = exprs[0];
        } else if (keyword == "narrow") {
            narrowClause = exprs[0];
        }
    }
    if (!joinClause) {
        // `join` says how the facts of two writings of one value are merged, and it is the one
        // clause that has no default: a fact without it has no value for a value the program
        // wrote two ways (rewrite design 7.5).
        diags_->of(SemanticDiag::FactJoinRequired)
            .at(rangeOf(context->firstToken()))
            .commit(name);
        LEAVE("FactDecl");
        return Value();
    }

    // The `with` parameters, as written. Every function that carries them builds its own copy:
    // an expression tree is not shared between two declarations.
    std::vector<std::string> withNames;
    if (const auto params = context->get<cst::AngledParams>()) {
        if (const auto paired = params->get<cst::PairedParams>()) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (pair->get<cst::IdentDef>()) {
                    withNames.push_back(std::string(pair->get<cst::IdentDef>()->text()));
                }
            }
        }
    }
    // The `with` parameters of the declaration, once per function that carries them: an expression
    // tree is not shared between two declarations. On the query function they are `const`, the
    // mark that says "the compiler must compute this argument"; on the functions the engine calls
    // they are plain `with` parameters, because the engine passes the query's arguments to them as
    // the `with` arguments of the call, so a case names one and reads the value.
    const auto withPairs = [&](bool asConstas) {
        std::vector<node_ptr_t> built;
        const auto params = context->get<cst::AngledParams>();
        if (!params) {
            return built;
        }
        const auto paired = params->get<cst::PairedParams>();
        if (!paired) {
            return built;
        }
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (!pair->get<cst::IdentDef>()) {
                continue;
            }
            const std::string paramName(pair->get<cst::IdentDef>()->text());
            node_ptr_t named = createNodeAs<NamedPairLoad>(
                Reference(paramName), false, /*isConst=*/asConstas, false, false);
            setNodeTokenRangeByContext(named, context);
            if (pair->type()) {
                *named << any2node(visitType(pair->type()));
            } else {
                node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
                setNodeTokenRangeByContext(inferNode, pair->get<cst::IdentDef>());
                *named << inferNode;
            }
            node_ptr_t defaultOpt = createNodeAs<OptionalLoad>("DataExpr");
            if (pair->expr()) {
                setNodeTokenRangeByContext(defaultOpt, pair->expr());
                *defaultOpt << any2node(visitExpr(pair->expr()));
            }
            *named << defaultOpt;
            built.push_back(named);
        }
        return built;
    };

    const auto literal = [&](LiteralType type, const std::string &text) {
        node_ptr_t node = createNodeAs<LiteralLoad>(Literal(type, text));
        setNodeTokenRangeByContext(node, context);
        return node;
    };
    const auto reference = [&](const std::string &text) {
        node_ptr_t node = createNodeAs<RefDataLoad>(Reference(text));
        setNodeTokenRangeByContext(node, context);
        return node;
    };
    const auto callOf = [&](node_ptr_t callee, std::vector<node_ptr_t> theArgs) {
        node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
        setNodeTokenRangeByContext(dataList, context);
        for (const auto &each : theArgs) {
            *dataList << each;
        }
        node_ptr_t namedList = createNodeAs<RepeatedLoad>("NamedData");
        setNodeTokenRangeByContext(namedList, context);
        node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        setNodeTokenRangeByContext(call, context);
        *call << callee << dataList << namedList;
        return call;
    };
    const auto blockOf = [&](node_ptr_t expr) {
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context);
        *stmt << expr;
        node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(stmts, context);
        *stmts << stmt;
        node_ptr_t block = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(block, context);
        *block << stmts;
        return block;
    };
    const auto makeFunc =
        [&](const std::string &fname, const std::vector<node_ptr_t> &withParams,
            const std::vector<std::pair<std::string, node_ptr_t>> &params, node_ptr_t retType,
            node_ptr_t body) -> node_ptr_t {
        const Reference fnRef = qualifyRef(Reference(fname));
        node_ptr_t funcType   = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(funcType, context);
        unwrapNodeAs<FuncTypeLoad>(funcType)->setTypeParamNames(typeParams);
        node_ptr_t withList = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(withList, context);
        for (const auto &each : withParams) {
            *withList << each;
        }
        *funcType << withList;
        node_ptr_t normParams = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(normParams, context);
        for (const auto &[pname, ptype] : params) {
            node_ptr_t named = createNodeAs<NamedPairLoad>(Reference(pname), false, false);
            setNodeTokenRangeByContext(named, context);
            *named << ptype << createNodeAs<OptionalLoad>("DataExpr");
            *normParams << named;
        }
        *funcType << normParams;
        node_ptr_t returnType = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(returnType, context);
        *returnType << retType;
        *funcType << returnType;
        node_ptr_t funcData = createNodeAs<FuncDataLoad>(fnRef);
        setNodeTokenRangeByContext(funcData, context);
        *funcData << funcType << body;
        node_ptr_t decl = createNodeAs<FuncDeclLoad>(fnRef);
        setNodeTokenRangeByContext(decl, context);
        *decl << funcData;
        return decl;
    };
    const auto typeOf = [&](syn::Node node) { return any2node(visitType(node)); };
    const auto refType = [&](const std::string &typeName) {
        node_ptr_t node = createNodeAs<RefTypeLoad>(Reference(typeName));
        setNodeTokenRangeByContext(node, context);
        return node;
    };

    std::vector<node_ptr_t> declarations;
    declarations.push_back(makeFunc(
        name + "__initial", withPairs(false), {}, typeOf(factType),
        any2node(visitBlockExpr(initialRhs))));
    // One function per clause: it calls the function the clause names, of the fact's type, so that
    // a name with several overloads is resolved where the type is known.
    const auto mergeFunc = [&](const std::string &suffix, syn::Node clause) -> node_ptr_t {
        std::vector<std::pair<std::string, node_ptr_t>> params = {
            {"a0", typeOf(factType)}, {"a1", typeOf(factType)}};
        node_ptr_t callee = any2node(visitExpr(clause));
        node_ptr_t body   = blockOf(callOf(callee, {reference("a0"), reference("a1")}));
        return makeFunc(name + suffix, withPairs(false), params, typeOf(factType), body);
    };
    declarations.push_back(mergeFunc("__join", joinClause));
    if (meetClause) {
        declarations.push_back(mergeFunc("__meet", meetClause));
    }
    if (widenClause) {
        declarations.push_back(mergeFunc("__widen", widenClause));
    }
    if (narrowClause) {
        declarations.push_back(mergeFunc("__narrow", narrowClause));
    }

    // The head parameters: what the query is written on. A parameter's declared type is the type
    // of the value the node stands for, so the query's parameter is `Node<T>` -- a node whose
    // value type is `T` -- and `T` is inferred from a caller's node the way any generic call infers
    // its type arguments (design principles 5.9). Parameters written without a type are nodes
    // whose value type the declaration does not state. The first one gives a `const` case the type
    // of its constant when the case writes none.
    const auto nodeOfType = [&](syn::Node valueType, cst::KeyParamPair at) -> node_ptr_t {
        node_ptr_t applied = createNodeAs<SpecTypeLoad>();
        setNodeTokenRangeByContext(applied, at);
        node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
        setNodeTokenRangeByContext(arguments, at);
        *arguments << any2node(visitType(valueType));
        *applied << refType("Node") << arguments;
        return applied;
    };
    std::vector<std::pair<std::string, node_ptr_t>> queryParams;
    std::vector<std::string> queryArgs;
    syn::Node firstHeadType;
    if (const auto params = context->get<cst::ParentParams>()) {
        if (const auto paired = params->get<cst::PairedParams>()) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (!pair->get<cst::IdentDef>()) {
                    continue;
                }
                queryArgs.push_back(std::string(pair->get<cst::IdentDef>()->text()));
                queryParams.emplace_back(
                    queryArgs.back(),
                    pair->type() ? nodeOfType(pair->type(), pair) : refType("Node"));
                if (!firstHeadType && pair->type()) {
                    firstHeadType = pair->type();
                }
            }
        }
    }
    if (queryArgs.empty()) {
        // The one-value form: the query is written on the value itself, as a node whose value type
        // the declaration does not state.
        queryArgs.push_back("n");
        queryParams.emplace_back("n", refType("Node"));
    }

    // The names bound by a case's pattern. A bare reference is a variable; the callee of a call is
    // not (`abs(x)` binds x, not abs), and a qualified name belongs to a module.
    const std::function<void(syn::Node, std::vector<std::string> &)> collectVars =
        [&](syn::Node node, std::vector<std::string> &out) {
            if (!node || node.empty()) {
                return;
            }
            if (node.kind() == NK::RefData) {
                const std::string text = std::string(node.text());
                if (text.find("::") == std::string::npos &&
                    std::find(out.begin(), out.end(), text) == out.end()) {
                    out.push_back(text);
                }
                return;
            }
            if (node.kind() == NK::AnnoExpr && node->get<cst::ParentArgues>()) {
                for (size_t i = 1; i < node->childCount(); ++i) {
                    collectVars(node->child(i).node(), out);
                }
                return;
            }
            for (size_t i = 0; i < node->childCount(); ++i) {
                collectVars(node->child(i).node(), out);
            }
        };

    struct CompiledCase {
        std::string patternFn;
        std::string factFn;
        uint64_t constMask = 0;
    };
    std::vector<CompiledCase> compiled;
    for (size_t index = 0; index + 1 < cases.size(); ++index) {
        const syn::Node one         = cases[index];
        const auto pattern          = one->get<cst::FactPattern>();
        const syn::Node patternExpr = pattern ? pattern->expr(0) : syn::Node();
        const syn::Node constDef    = pattern ? pattern->get<cst::IdentDef>() : syn::Node();
        const std::string constName = constDef ? constDef->text() : std::string();
        const syn::Node rhs         = one->expr(0);
        // `case const c => ...`: the case is about one constant, and the fact function is given
        // the value itself. The type is the one written, or the head's first parameter's.
        syn::Node constType;
        if (!constName.empty() && pattern) {
            const auto written = pattern->types();
            constType = written.size() == 1 ? written[0] : syn::Node();
        }
        if (!constName.empty()) {
            if (!constType) {
                constType = firstHeadType;
            }
            if (!constType) {
                // A `const` case matches one constant and the fact function is given the value
                // itself, so the constant needs a type: written in the case, or the head's first
                // parameter's, which is what the declaration says the value it is about is
                // (rewrite design 7.5).
                diags_->of(SemanticDiag::FactConstCaseTypeRequired)
                    .at(rangeOf(context->firstToken()))
                    .commit(constName, name);
                LEAVE("FactDecl");
                return Value();
            }
        }
        std::vector<std::string> vars;
        if (!constName.empty()) {
            vars.push_back(constName);
        } else {
            collectVars(patternExpr, vars);
        }
        if (vars.empty()) {
            // The cases read how a value was built, and the parts they read are the variables
            // their patterns name (`case x + y` binds the two operands), so a case whose pattern
            // names none leaves the fact nothing to compute from (rewrite design 7.5).
            diags_->of(SemanticDiag::FactCaseBindsNoVariable)
                .at(rangeOf(context->firstToken()))
                .commit(name);
            LEAVE("FactDecl");
            return Value();
        }

        std::vector<std::pair<std::string, node_ptr_t>> patternParams;
        std::vector<std::pair<std::string, node_ptr_t>> factParams;
        uint64_t mask = 0;
        for (size_t i = 0; i < vars.size(); ++i) {
            if (i == 0 && !constName.empty()) {
                patternParams.emplace_back(vars[i], typeOf(constType));
                factParams.emplace_back(vars[i], typeOf(constType));
                mask |= uint64_t{1} << i;
            } else {
                patternParams.emplace_back(vars[i], refType("int"));
                factParams.emplace_back(vars[i], refType("Node"));
            }
        }
        const std::string prefix = name + "__case" + std::to_string(index);
        node_ptr_t patternBody;
        if (!constName.empty()) {
            // The pattern binds the constant itself: the matcher binds the node and the fact
            // function receives the value.
            patternBody = blockOf(reference(constName));
        } else {
            patternBody = any2node(visitBlockExpr(patternExpr));
        }
        declarations.push_back(makeFunc(
            prefix, withPairs(false), patternParams,
            constName.empty() ? refType("int") : typeOf(constType), patternBody));
        declarations.push_back(makeFunc(
            prefix + "_fact", withPairs(false), factParams, typeOf(factType),
            any2node(visitBlockExpr(rhs))));
        compiled.push_back({prefix, prefix + "_fact", mask});
    }

    // The query function: `func depth(n: Node): int => __fact(...)`. Its parameters are the parts
    // of a value a rule's condition hands it; without a parameter list the query takes the value
    // itself (`depth(n)`), as it always has.
    node_ptr_t args = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(args, context);
    *args << literal(LiteralType::String, name);
    // The type parameters, named so that the maker knows which types its functions were made at;
    // the type arguments of the instance replace this text (the fact's type arguments are part of
    // what the fact is).
    *args << literal(LiteralType::String, typeParamNames);
    *args << reference(name + "__initial");
    *args << reference(name + "__join");
    *args << reference(meetClause ? name + "__meet" : name + "__join");
    *args << reference(widenClause ? name + "__widen" : name + "__join");
    *args << reference(narrowClause ? name + "__narrow" : name + "__join");
    // How many arguments the query was written with, then the arguments themselves: the engine
    // computes and caches one fact per argument.
    *args << literal(LiteralType::Integer, std::to_string(withNames.size()));
    for (const std::string &paramName : withNames) {
        *args << reference(paramName);
    }
    for (const std::string &arg : queryArgs) {
        *args << reference(arg);
    }
    for (const CompiledCase &one : compiled) {
        *args << reference(one.patternFn);
        *args << reference(one.factFn);
        *args << literal(LiteralType::Integer, std::to_string(one.constMask));
    }
    node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(call, context);
    *call << reference("__fact") << args << createNodeAs<RepeatedLoad>("NamedData");
    declarations.push_back(
        makeFunc(name, withPairs(true), queryParams, typeOf(factType), blockOf(call)));

    LEAVE("FactDecl");
    return declarations;
}

/*
        | ('export')? 'tag' identDef '=' bracketIdents ;
A tag is a name in the namespace of functions and types (design principles 6.2). The AST keeps
the positions as a bit set (TagPosition), the type parameters and the condition as written, the
parameters as written, the members of a group and the passes its declaration names; the GCT
builder registers the tag with them.
*/
Value Builder::visitTagDecl(cst::TagDecl context) {
    ENTER("TagDecl");
    // The name is written as a definition (`identDef`), not a reference: `tag name on ...`.
    const auto name = context->get<cst::IdentDef>();
    Reference ref   = qualifyRef(Reference(name ? name->text() : std::string{}));
    node_ptr_t declNode = createNodeAs<TagDeclLoad>(ref);
    setNodeTokenRangeByContext(declNode, context);
    auto decl = declNode->loadAs<TagDeclLoad>();
    decl->setDoc(docOf(context));

    if (const auto typeParams = context->get<cst::TypeParams>()) {
        for (const auto ident : typeParams->all<cst::IdentDef>()) {
            decl->addTypeParam(ident->text());
        }
    }
    if (const auto params = context->get<cst::ParentParams>()) {
        const auto paired = params->get<cst::PairedParams>();
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            TagDeclLoad::Param param;
            if (const auto name = pair->get<cst::IdentDef>()) {
                param.name = name->text();
            }
            if (const auto type = pair->type()) {
                param.type = std::string(type->sourceText());
            }
            decl->addParam(std::move(param));
        }
    }

    if (const auto group = context->get<cst::BracketIdents>()) {
        // `tag finite_math = [no_nans, no_infs]`: a group of tags, written where any of them
        // may be written. It has no facts of its own (design principles 6.2).
        if (const auto list = group->get<cst::IdentList>()) {
            for (const auto ident : list->all<cst::IdentDef>()) {
                decl->addAlias(qualifyRef(Reference(ident->text())));
            }
        }
    } else {
        if (const auto positions = context->get<cst::TagPositions>()) {
            for (const auto ident : positions->all<cst::IdentDef>()) {
                const std::string name(ident->text());
                if (const auto position = camel::core::tag::positionFromName(name)) {
                    decl->addPosition(static_cast<TagPosition>(*position));
                }
            }
        }
        if (const auto readers = context->get<cst::TagReaders>()) {
            for (const auto reader : readers->all<cst::IdentRef>()) {
                decl->addReader(value_cast<Reference>(visitIdentRef(reader)));
            }
        }
        if (const auto when = context->get<cst::WhenClause>()) {
            // The condition is kept as written, without the `when` word: a tag carries no
            // instance of its own for the type parameters to be bound in, so what the condition
            // says (`is_function::<F>()`) is read as the text it is, the same way a pass
            // declaration's condition is read.
            decl->setWhen(std::string(when->expr().sourceText()));
        }
    }

    if (context->tok(TK::KwExport)) {
        export_->addRef(ref);
    }
    LEAVE("TagDecl");
    return declNode;
}

/*
tagUse : '#' (tagRef | '[' (tagRef (',' tagRef)* ','?)? ']') ;
*/
std::vector<node_ptr_t> Builder::tagUsesOf(cst::TagUse context) {
    std::vector<node_ptr_t> uses;
    for (const auto ref : context->all<cst::TagRef>()) {
        const auto name = value_cast<Reference>(visitIdentRef(ref->get<cst::IdentRef>()));
        node_ptr_t use  = createNodeAs<TagUseLoad>(name);
        setNodeTokenRangeByContext(use, ref);
        if (const auto arguments = ref->get<cst::ParentArgues>()) {
            // A tag whose arguments are names, not values (`#reads(reassociate, contract)`):
            // each argument is the name as written, carried as a string constant. Whether the
            // name is a tag is the GIR builder's question; here it is only text.
            const auto id = camel::core::tag::find(name.toString());
            const camel::core::tag::Decl *decl =
                id.has_value() ? camel::core::tag::get(*id) : nullptr;
            if (decl != nullptr && decl->names) {
                node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
                setNodeTokenRangeByContext(dataList, arguments);
                const syn::TokenBuffer &tokens = context.tree()->tokens();
                std::string current;
                const auto flush = [&]() {
                    if (!current.empty()) {
                        node_ptr_t literalNode =
                            createNodeAs<LiteralLoad>(Literal(LiteralType::String, current));
                        setNodeTokenRangeByContext(literalNode, arguments);
                        *dataList << literalNode;
                        current.clear();
                    }
                };
                for (size_t i = arguments->firstToken().index();
                     i <= arguments->lastToken().index() && i < tokens.size();
                     ++i) {
                    if (syn::isTrivia(tokens[i].kind)) {
                        continue;
                    }
                    const std::string_view text = tokens.text(i);
                    if (text == "(" || text == ")") {
                        flush();
                    } else if (text == ",") {
                        flush();
                    } else {
                        current += text;
                    }
                }
                flush();
                *use << dataList;
                uses.push_back(use);
                continue;
            }
            const ArgumentValues args = value_cast<ArgumentValues>(visitParentArgues(arguments));
            if (args.named && args.named->size() > 0) {
                // A tag takes positional arguments only: it names its parameters where it is
                // declared, and the pass that reads the tag matches them by position.
                diags_->of(SemanticDiag::TagArgumentNamed)
                    .at(rangeOf(arguments->firstToken(), arguments->lastToken()))
                    .commit(args.named->at(0)->load()->toString());
            }
            *use << args.data;
        }
        uses.push_back(use);
    }
    return uses;
}

void Builder::addTagUses(const node_ptr_t &owner, const syn::Node &context) {
    if (node_ptr_t list = tagUseListOf(context)) {
        *owner << list;
    }
}

/// The tag uses written in `context`, as one repeated node, or null when there are none. Kept
/// apart from addTagUses because a module's tags belong to a node (the module's) that is made
/// after the declaration has been visited.
node_ptr_t Builder::tagUseListOf(const syn::Node &context) {
    node_ptr_t list = nullptr;
    for (const auto tagUse : context->all<cst::TagUse>()) {
        for (const auto &use : tagUsesOf(tagUse)) {
            if (!list) {
                list = createNodeAs<RepeatedLoad>("TagUse");
                setNodeTokenRangeByContext(list, tagUse);
            }
            *list << use;
        }
    }
    return list;
}

Value Builder::visitFuncDecl(cst::FuncDecl context) {
    ENTER("FuncDecl");

    // Decorators are applied to the function. The removed `@name(args)` spelling and the removed
    // `@vjp<rule>` decorator are reported and add no decorator (numeric permissions and custom
    // derivative rules are tags now, design principles 7.2, 8).
    node_ptr_t annoListNode = nullptr;
    for (const auto &funcAnno : context->all<cst::FuncAnno>()) {
        if (rejectParenthesizedName(funcAnno) || rejectVjpAnnotation(funcAnno)) {
            continue;
        }
        if (!annoListNode) {
            annoListNode = createNodeAs<RepeatedLoad>("FuncAnno");
        }
        *annoListNode << any2node(visitFuncAnno(funcAnno));
    }

    // A function written without a body is a declaration of an operator of a C++ module (design
    // principles 6.4): the signature is the whole of it. Its token range and origin end with the
    // signature, not with a block that is not there.
    const auto blockContext = context->get<cst::StmtBlock>();
    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    TokenRange typeRange    = {
        context->firstToken()->index(),
        blockContext ? blockContext->firstToken()->index() + 1
                     : context->lastToken()->index() + 1};
    setNodeTokenRange(funcTypeNode, typeRange);
    setNodeOriginByOffsets(
        funcTypeNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(
            blockContext ? blockContext->firstToken()->begin() : context->lastToken()->end()));
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
    }
    funcType->setModifiers(modSet);

    // The type parameters declared at the head (`func first::<T>(...)`, design principles 5.5).
    // They are the names the declaration's type variables are; the GCT builder puts them in
    // scope before it visits a parameter type, so one of them wins over a type of the same name.
    // One written with `..` is a pack and not one type (`func zip::<..A>`, 5.6).
    if (const auto typeParams = context->get<cst::TypeParams>()) {
        std::vector<TypeParam> params;
        for (const auto ident : typeParams->all<cst::IdentDef>()) {
            params.emplace_back(ident->text(), ident->writtenAsPack());
        }
        funcType->setTypeParams(std::move(params));
    }

    if (context->get<cst::AngledParams>()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->get<cst::AngledParams>()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->get<cst::ParentParams>()) {
        node_ptr_t normParams = any2node(visitParentParams(context->get<cst::ParentParams>()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(typeOptNode, typeRange);
    setNodeOriginByOffsets(
        typeOptNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(
            blockContext ? blockContext->firstToken()->begin() : context->lastToken()->end()));
    if (context->type()) {
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;
    if (context->get<cst::ResourceClause>()) {
        *funcTypeNode << any2node(visitResourceClause(context->get<cst::ResourceClause>()));
    }

    // The name: an identifier, or the operator of an overload (`func operator +(a, b)`, design
    // principles 4.3). An overload is a function under the reserved name of its operator
    // (`__add__`), chosen by the symbol and the number of parameters (`-` with one is the
    // negation). It is not qualified by an enclosing namespace: an operator has no name a
    // program could qualify, and every overload of it is one set (10.2). A symbol that cannot
    // be overloaded with that many parameters gets a name no program can write; the GCT
    // builder reports it with the reason.
    Reference ref;
    std::string operatorSymbol;
    syn::Node nameNode = context->get<cst::IdentRef>();
    if (const auto operatorName = context->get<cst::OperatorName>()) {
        nameNode           = operatorName;
        operatorSymbol     = operatorName->text().substr(std::string_view("operator").size());
        const size_t arity = funcTypeNode->at(1)->size();
        const auto group   = camel::core::overloadableOperatorGroup(operatorSymbol, arity);
        ref                = Reference(group ? std::string(*group) : "operator " + operatorSymbol);
    } else {
        ref = qualifyRef(value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>())));
    }
    if (context->tok(TK::KwExport)) {
        export_->addRef(ref);
    }

    // The body, when the declaration has one. A `FuncDataLoad` without a `StmtBlock` child is a
    // body-less declaration: the GCT builder makes a FUNC of it that no graph is built from.
    node_ptr_t blockNode = nullptr;
    if (blockContext) {
        blockNode = any2node(visitStmtBlock(blockContext));
    }

    node_ptr_t whenNode = nullptr;
    std::string whenText;
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        whenNode = any2node(visitExpr(whenClause->expr()));
        whenText = std::string(whenClause->expr().sourceText());
    }

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>(ref);
    setNodeTokenRangeByContext(funcNode, context);
    unwrapNodeAs<FuncDataLoad>(funcNode)->setWhen(whenNode);
    unwrapNodeAs<FuncDataLoad>(funcNode)->setWhenText(std::move(whenText));
    *funcNode << funcTypeNode;
    *funcNode << blockNode;
    // The signature as the node the function is built from knows it: the same parts the body of a
    // lambda registers (`visitFuncData`), so that the graph of a declared function reads its
    // parameters from where its body is built, whichever of the two nodes it is entered from.
    {
        std::vector<camel::source::SemanticPart> funcParts{
            semanticPart(
                camel::source::SemanticRole::GenericParameter,
                nodeOrigin(funcTypeNode->at(0)),
                -1,
                "withParams"),
            semanticPart(
                camel::source::SemanticRole::Parameter,
                nodeOrigin(funcTypeNode->at(1)),
                -1,
                "params"),
            semanticPart(
                camel::source::SemanticRole::ReturnType,
                typeOptNode->empty() ? camel::source::kInvalidOriginId
                                     : nodeOrigin(typeOptNode->front()),
                -1,
                "returnType"),
            semanticPart(
                camel::source::SemanticRole::ValueProducer,
                blockNode ? nodeOrigin(blockNode) : camel::source::kInvalidOriginId,
                -1,
                "body"),
        };
        appendParameterParts(funcParts, funcTypeNode);
        registerAstSemanticBundle(funcNode, std::move(funcParts));
    }

    node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(ref);
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDoc(docOf(context));
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setOperatorSymbol(operatorSymbol);
    if (!operatorSymbol.empty()) {
        unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDeclaredIn(namespaceStack_);
    }
    if (annoListNode) {
        *funcDeclNode << annoListNode;
    }
    // The tag uses written before the function, after the decorators. They are read when the
    // function is compiled: a tag declared `on func` belongs to the declaration, one declared
    // `on nodes` to the nodes of its body (design principles 6.2).
    addTagUses(funcDeclNode, context);
    *funcDeclNode << funcNode;
    setNodeTokenRangeByContext(funcDeclNode, context);
    std::vector<camel::source::SemanticPart> declSemanticParts{
        semanticPart(
            camel::source::SemanticRole::FuncName,
            deriveAstAnchorOrigin(funcDeclNode, nameNode, "ast.func.name"),
            -1,
            ref.toString()),
        semanticPart(
            camel::source::SemanticRole::GenericParameter,
            nodeOrigin(funcTypeNode->at(0)),
            -1,
            "withParams"),
        semanticPart(
            camel::source::SemanticRole::Parameter,
            nodeOrigin(funcTypeNode->at(1)),
            -1,
            "params"),
        semanticPart(
            camel::source::SemanticRole::ReturnType,
            typeOptNode->empty() ? camel::source::kInvalidOriginId
                                 : nodeOrigin(typeOptNode->front()),
            -1,
            "returnType"),
        semanticPart(
            camel::source::SemanticRole::ValueProducer,
            blockNode ? nodeOrigin(blockNode) : camel::source::kInvalidOriginId,
            -1,
            "body"),
    };
    // Each parameter as written, with its own position and its slot in the signature: the
    // declaration's own node is where the parameter list is read as one place, and where the
    // parameters of a declaration without a body are the whole of it.
    appendParameterParts(declSemanticParts, funcTypeNode);
    registerAstSemanticBundle(funcDeclNode, std::move(declSemanticParts));

    LEAVE("FuncDecl");
    return funcDeclNode;
}

/*
namespaceDecl : NAMESPACE identRef '{' SEP? (namespaceItem SEP?)* '}' ;
*/
Value Builder::visitNamespaceDecl(cst::NamespaceDecl context) {
    ENTER("NamespaceDecl");
    const Reference nsRef     = value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>()));
    const size_t previousSize = namespaceStack_.size();
    pushNamespace(nsRef);

    // The `using namespace` of the block apply to the declarations of the block (design
    // principles 10.2), wherever in the block they are written: each declaration carries them,
    // and they are not declarations of the module.
    std::vector<Reference> usings;
    for (const syn::Node item : context->nodes()) {
        if (item.is(NK::UsingNamespaceDecl)) {
            const auto using_ = cst::UsingNamespaceDecl(item);
            usings.push_back(value_cast<Reference>(visitIdentRef(using_->get<cst::IdentRef>())));
        }
    }
    std::vector<node_ptr_t> nodes;
    for (const syn::Node item : context->nodes()) {
        if (item.is(NK::Error) || item.is(NK::IdentRef) || item.is(NK::UsingNamespaceDecl)) {
            continue;
        }
        Value res = visitDecl(item);
        if (!res.has_value()) {
            continue;
        }
        if (res.type() == typeid(node_ptr_t)) {
            node_ptr_t node = any2node(res);
            if (node) {
                nodes.push_back(node);
            }
        } else if (res.type() == typeid(std::vector<node_ptr_t>)) {
            auto nested = value_cast<std::vector<node_ptr_t>>(res);
            nodes.insert(nodes.end(), nested.begin(), nested.end());
        }
    }

    if (!usings.empty()) {
        for (const node_ptr_t &node : nodes) {
            if (node->load()->type() == LoadType::Stmt) {
                node->loadAs<StmtLoad>()->addScopeUsings(usings);
            }
        }
    }
    popNamespace(previousSize);
    LEAVE("NamespaceDecl");
    return nodes;
}

/*
dataDecl   : (LET | VAR) carrier (':' typeList)? '=' dataList ;
*/
Value Builder::visitDataDecl(cst::DataDecl context, bool topLevel) {
    ENTER("DataDecl");
    bool isVar = false;
    if (context->tok(TK::KwVar)) {
        isVar = true;
    }
    // carrier : identList | parentIdents
    // A parenthesized carrier destructures a tuple; the parser refuses `{` and `[` here, so a
    // declaration never binds the fields of a struct or the elements of an array.
    cst::IdentList identList = context->get<cst::IdentList>();
    if (auto parent = context->get<cst::ParentIdents>()) {
        identList = parent->get<cst::IdentList>();
    }
    vector<Reference> refs;
    std::vector<cst::IdentDef> bindingIdents;
    for (const auto ident : identList->all<cst::IdentDef>()) {
        refs.push_back(topLevel ? qualifyRef(Reference(ident->text())) : Reference(ident->text()));
        bindingIdents.push_back(ident);
    }
    node_ptr_t dataDeclNode = createNodeAs<DataDeclLoad>(isVar, refs);
    setNodeTokenRangeByContext(dataDeclNode, context);
    unwrapNodeAs<DataDeclLoad>(dataDeclNode)->setDoc(docOf(context));
    if (context->get<cst::TypeList>()) {
        *dataDeclNode << any2node(visitTypeList(context->get<cst::TypeList>()));
    } else {
        node_ptr_t typeListNode = createNodeAs<RepeatedLoad>("Type");
        *dataDeclNode << typeListNode;
    }

    if (context->get<cst::DataList>()) {
        *dataDeclNode << any2node(visitDataList(context->get<cst::DataList>()));
    } else {
        // `export let inf: float`: no right side, the value the C++ module supplies.
        node_ptr_t none = createNodeAs<RepeatedLoad>("Data");
        setNodeTokenRangeByContext(none, context);
        *dataDeclNode << none;
    }
    std::vector<camel::source::SemanticPart> parts;
    if (context->tok(TK::KwLet)) {
        parts.push_back(semanticPart(
            camel::source::SemanticRole::Keyword,
            deriveAstAnchorOrigin(dataDeclNode, context->tok(TK::KwLet), "ast.data.let"),
            -1,
            "let"));
    } else if (context->tok(TK::KwVar)) {
        parts.push_back(semanticPart(
            camel::source::SemanticRole::Keyword,
            deriveAstAnchorOrigin(dataDeclNode, context->tok(TK::KwVar), "ast.data.var"),
            -1,
            "var"));
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        parts.push_back(semanticPart(
            camel::source::SemanticRole::BindingName,
            (i < bindingIdents.size()
                 ? deriveAstAnchorOrigin(dataDeclNode, bindingIdents[i], "ast.data.binding")
                 : nodeOrigin(dataDeclNode)),
            static_cast<int32_t>(i),
            refs[i].ident()));
    }
    parts.push_back(semanticPart(
        camel::source::SemanticRole::ValueProducer,
        nodeOrigin(dataDeclNode->atAs<RepeatedLoad>(1)),
        -1,
        "initializer"));
    registerAstSemanticBundle(dataDeclNode, std::move(parts));
    LEAVE("DataDecl");
    return dataDeclNode;
}

/*
typeDecl
    : TYPE identRef ('<' identDef (',' identDef)* ','? ('>' | '>='))? ('=' typeExpr)?
    ;
*/
/*
resourceDecl : 'export'? 'resource' identDef ('=' bracketIdents | 'unordered')?
*/
Value Builder::visitResourceDecl(cst::ResourceDecl context) {
    ENTER("ResourceDecl");
    const auto name  = context->get<cst::IdentDef>();
    node_ptr_t declNode = createNodeAs<ResourceDeclLoad>(
        qualifyRef(Reference(name ? name->text() : std::string{})));
    setNodeTokenRangeByContext(declNode, context);
    auto decl = declNode->loadAs<ResourceDeclLoad>();
    decl->setDoc(docOf(context));
    if (const auto group = context->get<cst::BracketIdents>()) {
        decl->setGroup(true);
        if (const auto list = group->get<cst::IdentList>()) {
            for (const auto member : list->all<cst::IdentDef>()) {
                // A member is a name as written: `stdout` is the resource every program has,
                // and `m::r` one of another module's. An enclosing namespace is not implied
                // (a group may name a standard resource from inside a namespace block).
                decl->addMember(Reference(member->text()));
            }
        }
    } else if (context->lastToken() && context->lastToken()->text() == "unordered") {
        // `resource net unordered`: the word after the name is the contextual `unordered`.
        decl->setUnordered(true);
    }
    if (context->tok(TK::KwExport)) {
        // `export resource` is this declaration, not an `ExportDecl` around it (the word
        // `resource` follows `export`): it is exported like a type or a data declaration.
        export_->addRef(decl->ref());
    }
    LEAVE("ResourceDecl");
    return declNode;
}

/*
resourceClause : 'uses' identRef (',' identRef)* | 'pure'
*/
Value Builder::visitResourceClause(cst::ResourceClause context) {
    ENTER("ResourceClause");
    node_ptr_t clauseNode = createNodeAs<ResourceClauseLoad>();
    setNodeTokenRangeByContext(clauseNode, context);
    auto clause = unwrapNodeAs<ResourceClauseLoad>(clauseNode);
    // The clause's tokens run together without white space or comments: a leading `pure` is the
    // word, while `uses pure` (a parameter of that name) starts with `uses`. The parser reads
    // both words so that `pure uses fs` is reported where it is written (PureAndUses) rather
    // than as a syntax error.
    if (context->text().starts_with("pure")) {
        clause->setPure(true);
    }
    for (const auto use : context->all<cst::IdentRef>()) {
        clause->addUse(value_cast<Reference>(visitIdentRef(use)));
    }
    LEAVE("ResourceClause");
    return clauseNode;
}

Value Builder::visitTypeDecl(cst::TypeDecl context) {
    ENTER("TypeDecl");
    Reference ref = qualifyRef(value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>())));

    node_ptr_t typeDeclNode = createNodeAs<TypeDeclLoad>(ref);
    setNodeTokenRangeByContext(typeDeclNode, context);
    unwrapNodeAs<TypeDeclLoad>(typeDeclNode)->setDoc(docOf(context));
    if (!context->all<cst::IdentDef>().empty()) {
        // `type Pair<T> = (T, T)`: a type function, its parameters are type names in the
        // right-hand side.
        std::vector<std::string> params;
        for (const auto param : context->all<cst::IdentDef>()) {
            params.push_back(param->text());
        }
        unwrapNodeAs<TypeDeclLoad>(typeDeclNode)->setParams(std::move(params));
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    if (context->type()) {
        setNodeTokenRangeByContext(typeOptNode, context->type());
        *typeOptNode << any2node(visitType(context->type()));
    }

    *typeDeclNode << typeOptNode;
    // The tag uses written before the type declaration (design principles 6.2): read when the
    // type is compiled, `on type` for the declaration and `on field` for its fields.
    addTagUses(typeDeclNode, context);
    LEAVE("TypeDecl");
    return typeDeclNode;
}

/*
retStmt    : (RETURN | YIELD) dataList ;
*/
Value Builder::visitRetStmt(cst::RetStmt context) {
    ENTER("RetStmt");
    // `yield e` is the second exit the production reads: it gives one element of the stream the
    // function's head declares (design principles 5.4). An error value is returned like any
    // other value; the function's declared `T!` makes it the failed arm (the graph builder
    // checks the declaration).
    ExitType exitType   = context->tok(TK::KwYield) ? ExitType::Yield : ExitType::Return;
    node_ptr_t exitNode = createNodeAs<ExitStmtLoad>(exitType);
    setNodeTokenRangeByContext(exitNode, context);
    if (context->get<cst::DataList>()) {
        *exitNode << any2node(visitDataList(context->get<cst::DataList>()));
    }
    LEAVE("RetStmt");
    return exitNode;
}

/*
restValue : '..' dataExpr ;
The argument is a compile-time tuple whose elements are the arguments (design principles 5.5);
GCT keeps the marker and GIR expands it at the call.
*/
Value Builder::visitRestValue(cst::RestValue context) {
    ENTER("RestValue");
    node_ptr_t res = createNodeAs<DataLoad>(DataType::SpreadArg);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitExpr(context->expr()));
    LEAVE("RestValue");
    return res;
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
Value Builder::visitKeyTypePair(cst::KeyTypePair context) {
    ENTER("KeyTypePair");
    Reference ref(context->get<cst::IdentDef>()->text());
    node_ptr_t namedTypeNode = createNodeAs<NamedTypeLoad>(ref);
    setNodeTokenRangeByContext(namedTypeNode, context);
    if (context->type()) {
        *namedTypeNode << any2node(visitType(context->type()));
    }
    // The tags of a field (design principles 6.2): kept with the named type, so that the type
    // declaration that owns the field can record them (`#tag x: int`).
    addTagUses(namedTypeNode, context);
    LEAVE("KeyTypePair");
    return namedTypeNode;
}

/*
keyValuePair : identDef ':' dataExpr ;
*/
Value Builder::visitKeyValuePair(cst::KeyValuePair context) {
    ENTER("KeyValuePair");
    // A key is always a name; only a text that ends inside the pair leaves the node without one,
    // and a placeholder key keeps the rest of the file checked.
    Reference ref(
        context->get<cst::IdentDef>() ? context->get<cst::IdentDef>()->text() : std::string("_"));
    node_ptr_t namedDataNode = createNodeAs<NamedDataLoad>(ref);
    setNodeTokenRangeByContext(namedDataNode, context);
    if (context->expr()) {
        *namedDataNode << any2node(visitExpr(context->expr()));
    }
    LEAVE("KeyValuePair");
    return namedDataNode;
}

/*
keyParamPair : ('var' | 'const' | 'constas')? identDef (':' typeExpr ('=' dataExpr)?)?
    | '..' identDef (':' typeExpr)? ;
A type left out is a lambda's parameter (design principles 5.9): it stands as an InferTypeLoad,
which GCT lowers to a hole and GIR fills from the function type the position expects. A `constas`
parameter writes its type; one left out stays a pair with no type child, so GCT can report it
(design principles 5.4).
*/
Value Builder::visitKeyParamPair(cst::KeyParamPair context) {
    ENTER("KeyParamPair");
    const bool isRest   = static_cast<bool>(context->tok(TK::DotDot));
    const bool isConstas = context->has(TK::KwConstas);
    bool isVar        = false;
    if (context->tok(TK::KwVar)) {
        isVar = true;
    }
    const bool isConst = context->has(TK::KwConst);
    // `..ParamsOf<F>` in a function type's parameter list has no name: the pack is its type.
    Reference ref(
        context->get<cst::IdentDef>() ? std::string(context->get<cst::IdentDef>()->text())
                                      : std::string("__pack"));
    node_ptr_t namedPairNode =
        createNodeAs<NamedPairLoad>(ref, isVar, isConst, isRest, isConstas);
    setNodeTokenRangeByContext(namedPairNode, context);
    if (isRest || isConstas) {
        // `..ts` is untyped: the tuple of the arguments left over is made at the call. A type
        // written after either is still read, so that GCT can report it (the rest parameter
        // refuses one; a `constas` needs one).
        if (context->type()) {
            *namedPairNode << any2node(visitType(context->type()));
        }
        // The default of a `constas` parameter of the `with` list.
        if (isConstas && context->type() && context->expr()) {
            node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
            setNodeTokenRangeByContext(dataExprOptNode, context->expr());
            *dataExprOptNode << any2node(visitExpr(context->expr()));
            *namedPairNode << dataExprOptNode;
        }
    } else if (context->type()) {
        *namedPairNode << any2node(visitType(context->type()));
    } else {
        node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
        setNodeTokenRangeByContext(inferNode, context->get<cst::IdentDef>());
        *namedPairNode << inferNode;
    }
    if (!isRest && !isConstas) {
        node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
        if (context->expr()) {
            setNodeTokenRangeByContext(dataExprOptNode, context->expr());
            *dataExprOptNode << any2node(visitExpr(context->expr()));
        }
        *namedPairNode << dataExprOptNode;
    }
    LEAVE("KeyParamPair");
    return namedPairNode;
}

/*
tupleParam : '(' identDef (',' identDef)* ','? ')' ;
A lambda parameter destructured as a tuple: the parameter has a name the program does not write,
and the names take the elements of the tuple it is passed (the rule of `let (a, b) = p`).
*/
Value Builder::visitTupleParam(cst::TupleParam context) {
    ENTER("TupleParam");
    std::vector<std::string> names;
    for (const auto &identDef : context->all<cst::IdentDef>()) {
        names.push_back(identDef->text());
    }
    // The name is internal: no program writes it, and the count of such parameters before it in
    // the file makes it unique. It does not depend on where the parameter is written, so a file
    // formatted differently has the same tree.
    Reference ref("_tuple_" + std::to_string(tupleParams_++));
    node_ptr_t namedPairNode = createNodeAs<NamedPairLoad>(ref);
    namedPairNode->loadAs<NamedPairLoad>()->setTupleNames(std::move(names));
    setNodeTokenRangeByContext(namedPairNode, context);
    node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
    setNodeTokenRangeByContext(inferNode, context);
    *namedPairNode << inferNode;
    node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
    *namedPairNode << dataExprOptNode;
    LEAVE("TupleParam");
    return namedPairNode;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
Value Builder::visitDataList(cst::DataList context) {
    ENTER("DataList");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &dataExpr : context->exprs()) {
        *rep << any2node(visitExpr(dataExpr));
    }
    LEAVE("DataList");
    return rep;
}

/*
identList    : identDef (',' identDef)* ;
*/
Value Builder::visitIdentList(cst::IdentList context) {
    ENTER("IdentList");
    std::vector<Reference> refs;
    for (const auto &identDef : context->all<cst::IdentDef>()) {
        refs.emplace_back(identDef->text());
    }
    LEAVE("IdentList");
    return refs;
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
Value Builder::visitIndexValues(cst::IndexValues context) {
    ENTER("IndexValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &indexValue : context->exprs()) {
        *rep << any2node(visitExpr(indexValue));
    }
    LEAVE("IndexValues");
    return rep;
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
In a call's parentheses the list may also hold a positional argument written after a named one:
it is a StrayPositional, and the call refuses it. The named arguments are all
this function returns.
*/
Value Builder::visitPairedValues(cst::PairedValues context) {
    ENTER("PairedValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &keyValuePair : context->all<cst::KeyValuePair>()) {
        *rep << any2node(visitKeyValuePair(keyValuePair));
    }
    LEAVE("PairedValues");
    return rep;
}

/*
strayPositional : indexValue ;
*/
Value Builder::visitStrayPositional(cst::StrayPositional context) {
    ENTER("StrayPositional");
    node_ptr_t value = nullptr;
    if (context->expr()) {
        value = any2node(visitExpr(context->expr()));
    }
    LEAVE("StrayPositional");
    return value;
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
Value Builder::visitPairedParams(cst::PairedParams context) {
    ENTER("PairedParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    for (const syn::Node child : context->nodes()) {
        if (child.is(NK::KeyParamPair)) {
            *rep << any2node(visitKeyParamPair(cst::KeyParamPair(child)));
        } else if (child.is(NK::TupleParam)) {
            *rep << any2node(visitTupleParam(cst::TupleParam(child)));
        }
    }
    LEAVE("PairedParams");
    return rep;
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
Value Builder::visitArgumentList(cst::ArgumentList context) {
    ENTER("ArgumentList");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    if (context->get<cst::IndexValues>()) {
        setNodeTokenRangeByContext(dataList, context->get<cst::IndexValues>());
        dataList = any2node(visitIndexValues(context->get<cst::IndexValues>()));
    }
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::PairedValues>()) {
        const auto &paired = context->get<cst::PairedValues>();
        setNodeTokenRangeByContext(namedDataList, paired);
        namedDataList = any2node(visitPairedValues(paired));
        for (const auto &stray : paired->all<cst::StrayPositional>()) {
            // The first one is where the report points; more than one is the same refusal.
            if (strayPositional == nullptr) {
                strayPositional = any2node(visitStrayPositional(stray));
            }
        }
    }
    LEAVE("ArgumentList");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
indices : '[' (sliceIndices | dataExpr) (',' (sliceIndices | dataExpr))* ']' ;
sliceIndices : dataExpr? ':' dataExpr? (':' dataExpr?)? ;
*/
Value Builder::visitIndices(cst::Indices context) {
    ENTER("Indices");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    for (const syn::Node entry : context->nodes()) {
        if (entry.is(NK::SliceIndices)) {
            // Three slots, one per part, an unwritten part a null load: the graph builder tells
            // a slice from an element subscript by the node, not by counting children. Which
            // slot a part takes follows from the colons the tree kept: a part before the first
            // colon is the start, one between the colons the end, one after the second the step.
            const cst::SliceIndices slice(entry);
            node_ptr_t sliceNode = createNodeAs<SliceLoad>();
            setNodeTokenRangeByContext(sliceNode, slice);
            node_ptr_t slots[3] = {nullptr, nullptr, nullptr};
            size_t colons       = 0;
            for (size_t i = 0; i < slice->childCount(); ++i) {
                const auto child = slice->child(i);
                if (child.isToken()) {
                    if (child.token().kind() == TK::Colon) {
                        ++colons;
                    } else if (child.token().kind() == TK::ColonColon) {
                        // `xs[:: -1]`: the two colons of an omitted start and end are one token.
                        colons += 2;
                    }
                    continue;
                }
                if (colons < 3) {
                    slots[colons] = any2node(visitExpr(child.node()));
                }
            }
            for (node_ptr_t &slot : slots) {
                if (slot == nullptr) {
                    slot = createNodeAs<NullLoad>();
                    setNodeTokenRangeByContext(slot, slice);
                }
                *sliceNode << slot;
            }
            *dataList << sliceNode;
        } else {
            *dataList << any2node(visitExpr(entry));
        }
    }
    LEAVE("Indices");
    return dataList;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
Value Builder::visitParentParams(cst::ParentParams context) {
    ENTER("ParentParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    const auto &pairedParams = context->get<cst::PairedParams>();
    if (pairedParams) {
        rep = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("ParentParams");
    return rep;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
Value Builder::visitParentArgues(cst::ParentArgues context) {
    ENTER("ParentArgues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::ArgumentList>()) {
        ArgumentValues values = value_cast<ArgumentValues>(
            visitArgumentList(context->get<cst::ArgumentList>()));
        dataList          = values.data;
        namedDataList     = values.named;
        strayPositional   = values.positionalAfterNamed;
    }
    LEAVE("ParentArgues");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
Value Builder::visitAngledParams(cst::AngledParams context) {
    ENTER("AngledParams");
    node_ptr_t res = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(res, context);
    const auto &pairedParams = context->get<cst::PairedParams>();
    if (pairedParams) {
        res = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
Value Builder::visitAngledValues(cst::AngledValues context) {
    ENTER("AngledValues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::ArgumentList>()) {
        ArgumentValues values = value_cast<ArgumentValues>(
            visitArgumentList(context->get<cst::ArgumentList>()));
        dataList          = values.data;
        namedDataList     = values.named;
        strayPositional   = values.positionalAfterNamed;
    }
    LEAVE("AngledValues");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
pattern
    : 'error' identDef
    | identRef (enumTuplePattern | enumStructPattern)?
    | literal
    | '_' // wildcard
    ;
fieldPattern
    : identRef | '_'
    ;
*/
Value Builder::visitPattern(cst::Pattern context) {
    ENTER("Pattern");
    node_ptr_t res;
    if (context->get<cst::Literal>()) {
        Literal literal = value_cast<Literal>(visitLiteral(context->get<cst::Literal>()));
        res             = createNodeAs<LiteralLoad>(literal);
        setNodeTokenRangeByContext(res, context->get<cst::Literal>());
    } else if (context->text() == "_") {
        res = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(res, context);
    } else if (context->get<cst::IdentRef>()) {
        // The two fixed patterns over a tagged value (design principles 3.8): `v` binds the
        // value taken out of a `T?` / `T!`, `error e` binds the error of a failed `T!`. Both
        // are a reference node; `error e` carries the bound name as its child. Whether the
        // scrutinee has such a type is known to the graph builder only, which reports an
        // identifier pattern over any other type by the rule itself: only the two marks `T?` and
        // `T!` can be taken apart (design principles 3.8). The parser reads the second
        // word of a pattern only after `error` (`parsePattern`), so the name here is the error's.
        if (context->get<cst::EnumTuplePattern>() || context->get<cst::EnumStructPattern>() ||
            context->get<cst::IdentRef>()->text().find("::") != std::string::npos) {
            // A variant without fields is written by its qualified name (`case Shape::Empty`):
            // a plain name would be a binding, and what an identifier pattern binds over a
            // value that is not `T?` / `T!` has not been decided.
            res = any2node(visitEnumPattern(context, context->get<cst::IdentRef>()));
            LEAVE("Pattern");
            return res;
        }
        res = createNodeAs<RefDataLoad>(Reference(context->get<cst::IdentRef>()->text()));
        setNodeTokenRangeByContext(res, context);
        if (context->get<cst::IdentDef>()) {
            node_ptr_t bound =
                createNodeAs<RefDataLoad>(Reference(context->get<cst::IdentDef>()->text()));
            setNodeTokenRangeByContext(bound, context->get<cst::IdentDef>());
            *res << bound;
        }
    } else {
        // A pattern is an identifier, a variant path, a literal or `_`. What the fields of a
        // variant bind is read by the enum patterns above; the parser builds no other shape.
        ASSERT(false, "a pattern of another shape reached the builder");
        res = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(res, context);
    }
    LEAVE("Pattern");
    return res;
}

/*
matchCase
    : CASE pattern ('|' pattern)* whenClause? '=>' blockExpr
    ;
*/
Value Builder::visitMatchCase(cst::MatchCase context) {
    ENTER("MatchCase");
    // `case a | b => body` enters the body when any of its patterns matches. A branch has one
    // value per arm, so the case becomes one arm per pattern, each with its own copy of the
    // body (only the first pattern used to be read). An alternative that is the wildcard
    // matches everything: the whole case is then the wildcard arm.
    std::vector<node_ptr_t> patterns;
    for (const auto patternContext : context->all<cst::Pattern>()) {
        node_ptr_t pattern = any2node(visitPattern(patternContext));
        if (pattern->type() == LoadType::Null) {
            patterns.assign(1, pattern);
            break;
        }
        patterns.push_back(pattern);
    }
    // The guard of the case, read once and shared by its arms as the body is: each arm builds
    // its own copy of it, in the scope of that arm's pattern, so the names the pattern binds
    // are the ones it reads (and one arm's guard never sees another arm's bindings).
    node_ptr_t guardNode = nullptr;
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        guardNode = any2node(visitExpr(whenClause->expr()));
    }
    std::vector<node_ptr_t> caseNodes;
    for (const node_ptr_t &pattern : patterns) {
        node_ptr_t caseNode = createNodeAs<MatchCaseLoad>();
        setNodeTokenRangeByContext(caseNode, context);
        node_ptr_t exprNode = any2node(visitBlockExpr(context->lastChild().node()));
        *caseNode << pattern << exprNode;
        if (guardNode) {
            *caseNode << guardNode;
        }
        registerAstSemanticBundle(
            caseNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(caseNode, context->tok(TK::KwCase), "ast.match.case"),
                    -1,
                    "case"),
                semanticPart(
                    camel::source::SemanticRole::CaseValue,
                    nodeOrigin(pattern),
                    0,
                    "pattern"),
                semanticPart(
                    camel::source::SemanticRole::CaseBody,
                    nodeOrigin(exprNode),
                    -1,
                    "body"),
            });
        caseNodes.push_back(caseNode);
    }
    LEAVE("MatchCase");
    return caseNodes;
}

/*
matchTypeExpr
    : MATCH TYPE typeExpr '{' typeCase* '}'
    ;
*/
Value Builder::visitMatchTypeExpr(cst::MatchTypeExpr context) {
    ENTER("MatchTypeExpr");
    node_ptr_t matchNode = createNodeAs<TypeMatchLoad>();
    setNodeTokenRangeByContext(matchNode, context);
    node_ptr_t typeNode = any2node(visitType(context->type()));
    *matchNode << typeNode;
    node_ptr_t casesNode = createNodeAs<RepeatedLoad>("TypeCase");
    setNodeTokenRangeByContext(casesNode, context);
    for (const auto typeCase : context->all<cst::TypeCase>()) {
        *casesNode << any2node(visitTypeCase(typeCase));
    }
    *matchNode << casesNode;
    registerAstSemanticBundle(
        matchNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(matchNode, context->tok(TK::KwMatch), "ast.match.type"),
                -1,
                "match"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(matchNode, context->tok(TK::KwType), "ast.match.type.type"),
                -1,
                "type"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(typeNode),
                -1,
                "type"),
        });
    LEAVE("MatchTypeExpr");
    return matchNode;
}

/*
staticForExpr
    : STATIC staticForHeader '=>' blockExpr
    ;
staticForHeader
    : FOR '(' identDef (',' identDef)* ')' IN ('fields' typeExpr | 'indices' dataExpr)
    ;
`indices` walks the positions of a tuple value; its operand is child 0 as an
ordinary expression, where `fields` has a type there. The head may name up to three things --
`fields`'s element is the triple (name, type, tags), every other element is one thing -- and
whether the count fits is read from the element (`SemanticDiag::StaticHeadNameCount`).
*/
Value Builder::visitStaticForExpr(cst::StaticForExpr context) {
    ENTER("StaticForExpr");
    const auto bindings = context->all<cst::IdentDef>();
    const std::string valueName =
        bindings.size() > 0 ? std::string(bindings[0]->text()) : std::string("_");
    const std::string typeName =
        bindings.size() > 1 ? std::string(bindings[1]->text()) : std::string("_");
    const std::string tagsName =
        bindings.size() > 2 ? std::string(bindings[2]->text()) : std::string("_");
    const auto mode      = context->tok(TK::Identifier);
    const bool byIndices = mode && mode.text() == "indices";
    node_ptr_t forNode =
        createNodeAs<StaticForLoad>(valueName, typeName, tagsName, byIndices, bindings.size());
    setNodeTokenRangeByContext(forNode, context);
    node_ptr_t shapeNode = byIndices ? any2node(visitExpr(context->expr()))
                                     : any2node(visitType(context->type()));
    *forNode << shapeNode;
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *forNode << bodyNode;
    registerAstSemanticBundle(
        forNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(forNode, context->tok(TK::KwStatic), "ast.static.for.strong"),
                -1,
                "static"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(forNode, context->tok(TK::KwFor), "ast.static.for"),
                -1,
                "for"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(shapeNode),
                -1,
                byIndices ? "indices" : "type"),
        });
    LEAVE("StaticForExpr");
    return forNode;
}

/*
staticLoopExpr
    : STATIC LOOP loopState (',' loopState)* staticLoopHeader DO blockExpr
    ;
The children of the node are described at StaticLoopLoad: a Repeated Data of the first values,
the walked shape (a type for `fields`, a value for `indices` and for a sequence), and the body.
The head's own production is `staticLoopHeader`: `for i in <序列>`, without the
parentheses `static for` still has.
*/
Value Builder::visitStaticLoopExpr(cst::StaticLoopExpr context) {
    ENTER("StaticLoopExpr");
    std::vector<Reference> state;
    node_ptr_t inits = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(inits, context);
    for (const auto entry : context->all<cst::LoopState>()) {
        state.emplace_back(entry->get<cst::IdentDef>()->text());
        *inits << any2node(visitExpr(entry->expr()));
    }
    const auto bindings = context->all<cst::IdentDef>();
    const std::string valueName =
        bindings.size() > 0 ? std::string(bindings[0]->text()) : std::string("_");
    const std::string typeName =
        bindings.size() > 1 ? std::string(bindings[1]->text()) : std::string("_");
    const std::string tagsName =
        bindings.size() > 2 ? std::string(bindings[2]->text()) : std::string("_");
    // The subject word of the head is the only direct identifier token of the node (`acc`,
    // `i` and the names of the states are inside their own nodes): `fields`, `indices`, or
    // nothing at all when what follows `in` is a sequence.
    const auto mode       = context->tok(TK::Identifier);
    const bool byIndices  = mode && mode.text() == "indices";
    const bool byFields   = mode && mode.text() == "fields";
    const bool bySequence = !byIndices && !byFields;
    node_ptr_t loopNode = createNodeAs<StaticLoopLoad>(
        state, valueName, typeName, tagsName, byIndices, bySequence, bindings.size());
    setNodeTokenRangeByContext(loopNode, context);
    *loopNode << inits;
    node_ptr_t shapeNode = byIndices || bySequence ? any2node(visitExpr(context->expr()))
                                                   : any2node(visitType(context->type()));
    *loopNode << shapeNode;
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *loopNode << bodyNode;
    registerAstSemanticBundle(
        loopNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(loopNode, context->tok(TK::KwStatic), "ast.static.loop.strong"),
                -1,
                "static"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(loopNode, context->tok(TK::KwLoop), "ast.static.loop"),
                -1,
                "loop"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(shapeNode),
                -1,
                byIndices ? "indices" : (bySequence ? "sequence" : "type")),
        });
    LEAVE("StaticLoopExpr");
    return loopNode;
}

TypeCaseKind Builder::typeCaseKindOf(cst::TypeCasePattern pattern) {
    const std::string text = pattern.text();
    if (const auto kind = typeCaseKindOfWord(text)) {
        return *kind;
    }
    // A word that names no shape is told, where it is written, instead of being read as `_`.
    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
        .at(rangeOf(pattern->firstToken(), pattern->lastToken()))
        .commit(text, "it is not a shape word (struct, tuple, array, func, nullable, fallible, map, set, #leaf, _)");
    return TypeCaseKind::Wildcard;
}

template <class List> static std::vector<cst::TypeCasePattern> patternsOf(const List &list) {
    std::vector<cst::TypeCasePattern> out;
    for (const auto pattern : list) {
        out.push_back(pattern);
    }
    return out;
}

std::vector<TypeCasePattern>
Builder::typeCasePatternsOf(const std::vector<cst::TypeCasePattern> &written) {
    std::vector<TypeCasePattern> patterns;
    for (const auto pattern : written) {
        TypeCasePattern entry;
        // A pattern with a type child is an identity pattern (`case int`, `case T`); the
        // shape words carry no type.
        if (const auto typeNode = pattern.type()) {
            entry.kind = TypeCaseKind::Identity;
            entry.type = any2node(visitType(typeNode));
        } else {
            entry.kind = typeCaseKindOf(pattern);
        }
        patterns.push_back(std::move(entry));
    }
    return patterns;
}

/*
typeCase
    : CASE typeCasePattern ('|' typeCasePattern)* '=>' blockExpr
    ;
*/
Value Builder::visitTypeCase(cst::TypeCase context) {
    ENTER("TypeCase");
    node_ptr_t caseNode = createNodeAs<TypeCaseLoad>(
        typeCasePatternsOf(patternsOf(context->all<cst::TypeCasePattern>())));
    setNodeTokenRangeByContext(caseNode, context);
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *caseNode << bodyNode;
    registerAstSemanticBundle(
        caseNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(caseNode, context->tok(TK::KwCase), "ast.match.type.case"),
                -1,
                "case"),
            semanticPart(
                camel::source::SemanticRole::CaseBody,
                nodeOrigin(bodyNode),
                -1,
                "body"),
        });
    LEAVE("TypeCase");
    return caseNode;
}

/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
Value Builder::visitCtrlExpr(syn::Node context) {
    ENTER("CtrlExpr");
    node_ptr_t ctrlNode = nullptr;
    switch (context.kind()) {
    case NK::IfExpr: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        ctrlNode = createNodeAs<IfExprLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
        node_ptr_t condNode = any2node(visitExpr(context->expr()));
        node_ptr_t thenNode = any2node(visitBlockExpr(context->child(3).node()));
        *ctrlNode << condNode;
        *ctrlNode << thenNode;
        node_ptr_t elseBlockNode = createNodeAs<OptionalLoad>("StmtBlock");
        *ctrlNode << elseBlockNode;
        if (context->child(5).node()) {
            setNodeTokenRangeByContext(elseBlockNode, context->child(5).node());
            *elseBlockNode << any2node(visitBlockExpr(context->child(5).node()));
        } else {
            node_ptr_t stmts = createNodeAs<StmtBlockLoad>();
            *stmts << createNodeAs<RepeatedLoad>("Stmt");
            *elseBlockNode << stmts;
        }
        registerAstSemanticBundle(
            ctrlNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwIf), "ast.if.keyword"),
                    -1,
                    "if"),
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwThen), "ast.if.then"),
                    -1,
                    "then"),
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    context->tok(TK::KwElse)
                        ? deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwElse), "ast.if.else")
                        : camel::source::kInvalidOriginId,
                    -1,
                    "else"),
                semanticPart(
                    camel::source::SemanticRole::BranchCondition,
                    nodeOrigin(condNode),
                    -1,
                    "condition"),
                semanticPart(
                    camel::source::SemanticRole::BranchTarget,
                    nodeOrigin(thenNode),
                    0,
                    "then"),
                semanticPart(
                    camel::source::SemanticRole::BranchTarget,
                    elseBlockNode->empty() ? camel::source::kInvalidOriginId
                                           : nodeOrigin(elseBlockNode->front()),
                    1,
                    "else"),
            });
    } break;
    case NK::MatchTypeExpr: // MATCH TYPE typeExpr '{' typeCase* '}'
    {
        ctrlNode = any2node(visitMatchTypeExpr(cst::MatchTypeExpr(context)));
    } break;
    case NK::StaticForExpr:
    {
        ctrlNode = any2node(visitStaticForExpr(cst::StaticForExpr(context)));
    } break;
    case NK::MatchExpr: // MATCH logicalOrExpr '{' matchCase+ '}'
    {
        ctrlNode = createNodeAs<MatchExprLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
        node_ptr_t subjectNode = any2node(visitExpr(context->expr()));
        // The subject is evaluated once, before the cases; the graph builder tests its value.
        *ctrlNode << subjectNode;
        node_ptr_t casesNode = createNodeAs<RepeatedLoad>("MatchCase");
        setNodeTokenRangeByContext(casesNode, context);
        cst::MatchCase wildcardCase;
        bool wildcardSeen = false;
        for (const auto matchCase : context->all<cst::MatchCase>()) {
            if (wildcardSeen) {
                // The branch takes its last arm when no value matches, and `case _` always
                // matches: a case written after it is never taken (it used to be entered in
                // the wildcard's place).
                diags_->of(SemanticDiag::MatchCaseAfterWildcard)
                    .at(rangeOf(matchCase->firstToken(), matchCase->lastToken()))
                    .commit();
                continue;
            }
            // One node per pattern of the case (see visitMatchCase).
            for (const node_ptr_t &caseNode :
                 value_cast<std::vector<node_ptr_t>>(visitMatchCase(matchCase))) {
                *casesNode << caseNode;
            }
            // The first case that matches is taken, so nothing after the wildcard can be. A
            // guarded wildcard is not a wildcard: `case _ when c` is not taken when `c` is
            // false, so the cases written after it are still reached (and the guarded case
            // does not cover the match by itself).
            if (casesNode->size() > 0) {
                const node_ptr_t &lastCase = casesNode->at(casesNode->size() - 1);
                if (lastCase->front()->type() == LoadType::Null && lastCase->size() <= 2) {
                    wildcardCase = matchCase;
                    wildcardSeen = true;
                }
            }
        }
        if (wildcardCase && casesNode->size() == 1 &&
            subjectNode->load()->type() == LoadType::Data &&
            std::dynamic_pointer_cast<RefDataLoad>(subjectNode->load()) != nullptr) {
            // Only the wildcard is left: the match is its body, whatever the value. A branch
            // without a value to compare is read as an `if` on a boolean, so this is built as
            // one whose condition is true. Reading the name has no effect, so dropping the
            // read is not observable; a subject that is a call is kept in the general form
            // below, which evaluates it.
            ctrlNode = createNodeAs<IfExprLoad>();
            setNodeTokenRangeByContext(ctrlNode, context);
            node_ptr_t condNode = createNodeAs<LiteralLoad>(Literal(LiteralType::Boolean, "true"));
            setNodeTokenRangeByContext(condNode, context);
            node_ptr_t thenNode      = casesNode->at(0)->at(1);
            node_ptr_t elseBlockNode = createNodeAs<OptionalLoad>("StmtBlock");
            setNodeTokenRangeByContext(elseBlockNode, wildcardCase->lastChild().node());
            *elseBlockNode << any2node(visitBlockExpr(wildcardCase->lastChild().node()));
            *ctrlNode << condNode << thenNode << elseBlockNode;
            registerAstSemanticBundle(
                ctrlNode,
                {
                    semanticPart(
                        camel::source::SemanticRole::Keyword,
                        deriveAstAnchorOrigin(
                            ctrlNode,
                            context->tok(TK::KwMatch),
                            "ast.match.keyword"),
                        -1,
                        "match"),
                    semanticPart(
                        camel::source::SemanticRole::BranchCondition,
                        nodeOrigin(condNode),
                        -1,
                        "condition"),
                    semanticPart(
                        camel::source::SemanticRole::BranchTarget,
                        nodeOrigin(thenNode),
                        0,
                        "then"),
                    semanticPart(
                        camel::source::SemanticRole::BranchTarget,
                        nodeOrigin(elseBlockNode->front()),
                        1,
                        "else"),
                });
            break;
        }
        *ctrlNode << casesNode;
        std::string subjectText                        = syn::Node(context->expr())->text();
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwMatch), "ast.match.keyword"),
                -1,
                "match"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(subjectNode),
                -1,
                subjectText),
        };
        for (size_t i = 0; i < casesNode->size(); ++i) {
            auto caseOrigin = nodeOrigin(casesNode->at(i));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::BranchTarget,
                caseOrigin,
                static_cast<int32_t>(i),
                "case"));
        }
        registerAstSemanticBundle(ctrlNode, std::move(parts));
    } break;

    default:
        // Every kind `visitExpr` sends here has a case above. `try`/`catch`/`finally` are not
        // part of the language (design principles 3.9) and are not tokens any more, so the
        // parser never builds a node for them.
        throw std::runtime_error("Invalid control expression");
    }
    LEAVE("CtrlExpr");
    return ctrlNode;
}

/*
assignExpr
    : logicalOrExpr (
        ('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=')
        logicalOrExpr
    )?
    ;
*/
Value Builder::visitAssignExpr(cst::AssignExpr context) {
    ENTER("AssignExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "=") {
            op = BinaryDataOp::Assign;
        } else if (strOp == "+=") {
            op = BinaryDataOp::AssignAdd;
        } else if (strOp == "-=") {
            op = BinaryDataOp::AssignSub;
        } else if (strOp == "*=") {
            op = BinaryDataOp::AssignMul;
        } else if (strOp == "/=") {
            op = BinaryDataOp::AssignDiv;
        } else if (strOp == "%=") {
            op = BinaryDataOp::AssignMod;
        } else if (strOp == "^=") {
            op = BinaryDataOp::AssignExp;
        } else if (strOp == "@=") {
            op = BinaryDataOp::AssignMat;
        } else if (strOp == "&=") {
            op = BinaryDataOp::AssignAnd;
        } else if (strOp == "|=") {
            op = BinaryDataOp::AssignOr;
        } else {
            throw std::runtime_error("Invalid assignment operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.assign.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("AssignExpr");
    return lhsNode;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
Value Builder::visitLogicalOrExpr(cst::LogicalOrExpr context) {
    ENTER("LogicalOrExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::LogicalOr);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.logic.or"),
                    -1,
                    "||"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalOrExpr");
    return lhsNode;
}

/*
orExpr  : andExpr ('|' andExpr)* ;
andExpr : equalityExpr ('&' equalityExpr)* ;
Ordinary binary operators that evaluate both sides: BinaryDataOp::Or and And. `||` and `&&`
are LogicalOr and LogicalAnd, which a later stage turns into branches.
*/
Value Builder::visitEagerLogicExpr(syn::Node context) {
    const bool isOr    = context.is(NK::OrExpr);
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    const size_t count = context->exprs().size();
    for (size_t i = 1; i < count; ++i) {
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode =
            createNodeAs<BinaryExprLoad>(isOr ? BinaryDataOp::Or : BinaryDataOp::And);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        isOr ? "ast.or" : "ast.and"),
                    -1,
                    isOr ? "|" : "&"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    return lhsNode;
}

/*
logicalAndExpr
    : orExpr ('&&' orExpr)*
    ;
*/
Value Builder::visitLogicalAndExpr(cst::LogicalAndExpr context) {
    ENTER("LogicalAndExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::LogicalAnd);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.logic.and"),
                    -1,
                    "&&"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalAndExpr");
    return lhsNode;
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
Value Builder::visitEqualityExpr(cst::EqualityExpr context) {
    ENTER("EqualityExpr");
    node_ptr_t lhsNode =
        any2node(visitExpr(context->expr(0))); // get the left hand side of the equality expr
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "===") {
            op = BinaryDataOp::StrictEq;
        } else if (strOp == "!==") {
            op = BinaryDataOp::StrictNeq;
        } else if (strOp == "==") {
            op = BinaryDataOp::Eq;
        } else if (strOp == "!=") {
            op = BinaryDataOp::Neq;
        } else {
            throw std::runtime_error("Invalid equality operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.equality.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("EqualityExpr");
    return lhsNode;
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
Value Builder::visitRelationalExpr(cst::RelationalExpr context) {
    ENTER("RelationalExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "<") {
            op = BinaryDataOp::Less;
        } else if (strOp == ">") {
            op = BinaryDataOp::Greater;
        } else if (strOp == "<=") {
            op = BinaryDataOp::LessEq;
        } else if (strOp == ">=") {
            op = BinaryDataOp::GreaterEq;
        } else {
            throw std::runtime_error("Invalid relational operator: " + strOp);
        }
        if (strOp == ">" && context->exprs().size() > 2) {
            // `f<a, 0>` at the end of a line, followed by a line that goes on to contain a
            // `<...>` of its own (`'total = {}'->format<total>->println`): the first `>` is
            // read as a comparison between `0` and the next line, that line's `<` as another,
            // and its `>` closes the arguments. The errors that follow (an undefined
            // operator, a name used before its declaration) do not point there. A chain of
            // comparisons (`a > b < c`) is never what a program means, so a `>` in one that
            // ends its line is reported.
            syn::TokenRef opToken   = tokenOf(context->child(i * 2 - 1));
            syn::TokenRef nextToken = context->expr(i)->firstToken();
            if (opToken && nextToken && nextToken->line() > opToken->line()) {
                diags_->of(SyntaxDiag::ComparisonAcrossLines)
                    .at(rangeOf(opToken))
                    .commit(quoteForDiagnostic(nextToken->text()));
            }
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.relational.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("RelationalExpr");
    return lhsNode;
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
Value Builder::visitAdditiveExpr(cst::AdditiveExpr context) {
    ENTER("AdditiveExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "+") {
            op = BinaryDataOp::Add;
        } else if (strOp == "-") {
            op = BinaryDataOp::Sub;
        } else {
            throw std::runtime_error("Invalid additive operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.additive.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("AdditiveExpr");
    return lhsNode;
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
Value Builder::visitMultiplicativeExpr(cst::MultiplicativeExpr context) {
    ENTER("MultiplicativeExpr");
    BinaryDataOp op;
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "*") {
            op = BinaryDataOp::Mul;
        } else if (strOp == "/") {
            op = BinaryDataOp::Div;
        } else if (strOp == "^") {
            op = BinaryDataOp::Pow;
        } else if (strOp == "@") {
            op = BinaryDataOp::Mat;
        } else if (strOp == "%") {
            op = BinaryDataOp::Mod;
        } else {
            throw std::runtime_error("Invalid multiplicative operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.multiplicative.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("MultiplicativeExpr");
    return lhsNode;
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
Value Builder::visitNullableExpr(cst::NullableExpr context) {
    ENTER("NullableExpr");
    node_ptr_t res = any2node(visitExpr(context->expr()));

    if (context->childCount() > 1) {
        string strOp = context->child(1)->text();
        ReservedDataOp op;
        if (strOp == "??") {
            op = ReservedDataOp::NullThen;
        } else if (strOp == "!!") {
            op = ReservedDataOp::ErrorThen;
        } else {
            throw std::runtime_error("Invalid nullable operator: " + strOp);
        }
        // `a ?? b` / `a !! b`: a branch on the tag of `a` (design principles 3.9).
        if (context->expr(1)) {
            node_ptr_t dataExprNode = createNodeAs<ReservedExprLoad>(op);
            // Located at the operator: that is where its diagnostics point.
            syn::TokenRef opToken = tokenOf(context->child(1));
            setNodeTokenRange(dataExprNode, opToken->index(), opToken->index() + 1);
            setNodeOriginByOffsets(
                dataExprNode,
                static_cast<size_t>(opToken->begin()),
                static_cast<size_t>(opToken->end()));
            node_ptr_t rhsNode = any2node(visitExpr(context->expr(1)));
            *dataExprNode << res << rhsNode;
            registerAstSemanticBundle(
                dataExprNode,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(
                            dataExprNode,
                            tokenOf(context->child(1)),
                            "ast.nullable.operator"),
                        -1,
                        strOp),
                    semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(res), -1, "lhs"),
                    semanticPart(
                        camel::source::SemanticRole::Argument,
                        nodeOrigin(rhsNode),
                        0,
                        "rhs"),
                });
            res = dataExprNode;
        }
    }

    LEAVE("NullableExpr");
    return res;
}

/*
unaryExpr
    : linkExpr (AS typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
Value Builder::visitUnaryExpr(syn::Node context) {
    ENTER("UnaryExpr");
    node_ptr_t res = nullptr;

    switch (context.is(NK::CastExpr) ? 1 : 2) {
    case 1: {
        {
            if (!context->tok(TK::KwAs)) {
                throw std::runtime_error("Invalid unary operator: " + context->child(0)->text());
            }
            node_ptr_t dataNode = any2node(visitExpr(context->expr()));
            node_ptr_t typeNode = any2node(visitType(context->type()));
            res                 = createNodeAs<ReservedExprLoad>(ReservedDataOp::As);
            setNodeTokenRangeByContext(res, context);
            *res << dataNode << typeNode;
            registerAstSemanticBundle(
                res,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(
                            res, context->tok(TK::KwAs), "ast.unary.keyword"),
                        -1,
                        "as"),
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(dataNode),
                        -1,
                        "value"),
                    semanticPart(
                        camel::source::SemanticRole::ReturnType,
                        nodeOrigin(typeNode),
                        -1,
                        "type"),
                });
        }
        break;
    }
    case 2: {
        UnaryDataOp op;
        string strOp = context->child(0)->text();
        if (strOp == "!") {
            op = UnaryDataOp::Not;
        } else if (strOp == "-") {
            op = UnaryDataOp::Neg;
        } else if (strOp == "~") {
            op = UnaryDataOp::Inv;
        } else {
            throw std::runtime_error("Invalid unary operator: " + strOp);
        }
        if (op == UnaryDataOp::Neg && context->expr().is(NK::Literal)) {
            // A minus applied directly to a number is the negative number: the literal -2, the
            // smallest integer -9223372036854775808 (whose digits alone do not fit), a typed
            // `let b: i32 = -2147483648`. Only the minus right before the literal: `-2 ^ 2`
            // negates the power and `-(2)` the parenthesis.
            const auto literal   = cst::Literal(context->expr());
            const bool integer   = static_cast<bool>(literal->tok(TK::Integer));
            const bool imaginary = static_cast<bool>(literal->tok(TK::Imaginary));
            if ((integer || literal->tok(TK::Real) || imaginary) && literal->childCount() == 1) {
                res = createNodeAs<LiteralLoad>(Literal(
                    imaginary ? LiteralType::Imaginary
                              : (integer ? LiteralType::Integer : LiteralType::Real),
                    "-" + literal->text()));
                setNodeTokenRangeByContext(res, context);
                break;
            }
        }
        node_ptr_t dataNode = any2node(visitExpr(context->expr()));
        res                 = createNodeAs<UnaryExprLoad>(op);
        setNodeTokenRangeByContext(res, context);
        *res << dataNode;
        registerAstSemanticBundle(
            res,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(res, tokenOf(context->child(0)), "ast.unary.operator"),
                    -1,
                    strOp),
                semanticPart(
                    camel::source::SemanticRole::Argument,
                    nodeOrigin(dataNode),
                    0,
                    "operand"),
            });
        break;
    }
    default:
        throw std::runtime_error("Invalid alternative in UnaryExpr");
    }

    LEAVE("UnaryExpr");
    return res;
}

/*
linkExpr
    : annoExpr ('->' annoExpr)*
    ;
*/
Value Builder::visitLinkExpr(cst::LinkExpr context) {
    ENTER("LinkExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp       = context->child(i * 2 - 1)->text();
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        node_ptr_t linkNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        setNodeTokenRangeByContext(linkNode, context);
        linkNode->loadAs<ReservedExprLoad>()->setPipe(true);
        // What the arrow hands on is the receiver of the call, wherever the call is written.
        node_ptr_t dataList      = nullptr;
        node_ptr_t calleeNode    = rhsNode;
        node_ptr_t namedDataList = nullptr;
        if (rhsNode->type() == LoadType::Data &&
            rhsNode->loadAs<DataLoad>()->dataType() == DataType::ReservedExpr &&
            rhsNode->loadAs<ReservedExprLoad>()->op() == ReservedDataOp::Call) {
            // `x->f(a, b)` is `f(x, a, b)` (design principles 4.4): the receiver is the first
            // ordinary argument of the call written on the right, not the value its result is
            // applied to. The call keeps its callee and its named arguments.
            calleeNode    = rhsNode->at(0);
            namedDataList = rhsNode->at(2);
            // The merged call stands for the one on the right: a positional argument written
            // after a name there is still the same refusal.
            linkNode->loadAs<ReservedExprLoad>()->setPositionalAfterNamed(
                rhsNode->loadAs<ReservedExprLoad>()->positionalAfterNamed());
            calleeNode->setParent(nullptr);
            namedDataList->setParent(nullptr);
            dataList = createNodeAs<RepeatedLoad>("Data");
            setNodeTokenRangeBetween(dataList, context->expr(i - 1), context->expr(i));
            *dataList << lhsNode;
            // The written arguments move from the call on the right into the merged list; a
            // node is in one place, so they are detached from it first.
            node_ptr_t written = rhsNode->at(1);
            for (const auto &argument : *written) {
                argument->setParent(nullptr);
                *dataList << argument;
            }
        } else {
            dataList = createNodeAs<RepeatedLoad>("Data");
            setNodeTokenRangeByContext(dataList, context->expr(i));
            namedDataList = createNodeAs<RepeatedLoad>("NamedData");
            setNodeTokenRangeByContext(namedDataList, context->expr(i));
            *dataList << lhsNode;
        }
        *linkNode << calleeNode << dataList << namedDataList;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(
                    linkNode,
                    tokenOf(context->child(i * 2 - 1)),
                    "ast.link.operator"),
                -1,
                strOp),
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
            semanticPart(
                camel::source::SemanticRole::Receiver,
                nodeOrigin(lhsNode),
                -1,
                "receiver"),
            semanticPart(camel::source::SemanticRole::ArgList, nodeOrigin(dataList), -1, "args"),
        };
        // The receiver is argument 0; the arguments written after the arrow follow it.
        int32_t slot = 0;
        for (const auto &argument : *dataList) {
            const char *label = slot == 0 ? "receiver" : "arg";
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(argument),
                slot++,
                label));
        }
        registerAstSemanticBundle(linkNode, std::move(parts));
        lhsNode = linkNode;
    }
    LEAVE("LinkExpr");
    return lhsNode;
}

/*
rangeExpr
    : additiveExpr ('..' additiveExpr)*
    ;
*/
Value Builder::visitRangeExpr(cst::RangeExpr context) {
    ENTER("RangeExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp        = context->child(i * 2 - 1)->text();
        node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Comp);
        setNodeTokenRangeByContext(dataNode, context);
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        *dataNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.comp.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataNode;
    }
    LEAVE("RangeExpr");
    return lhsNode;
}

/*
annoExpr
    : accessExpr (({isAdjacent()}? (indices | parentArgues | angledValues | '!')) | memberAccess)*
    ;
*/
Value Builder::visitAnnoExpr(cst::AnnoExpr context) {
    ENTER("AnnoExpr");
    // `node_of(c)` in a rule's condition :
    // "条件函数按 `const` 形参追加隐藏的节点形参，构建期把 `node_of(c)` 换成它；不引入新类型"
    // and "名字是 `node_of(c)`，只接受该规则（或 `fact` 的 `case`）自己的形参名". The call is
    // read here, before the callee is built, and becomes the hidden node parameter the rule
    // declaration appended for that `const` parameter. A name that is not a `const` parameter of
    // this rule is refused here, by name, and `node_of` written where no such parameter exists is
    // refused too, because the map is empty outside a rule condition.
    if (context->expr().is(NK::RefData) && context->expr().text() == "node_of") {
        // The argument is read from the tree, not built: `node_of(c)` is not a call of a
        // function, and building `c` would put a node in the tree that nothing reads. It is one
        // written name and nothing else: `node_of` names a parameter, so anything else (a call,
        // an arithmetic expression) has no parameter to name and is refused below, by name.
        std::string argument;
        if (const cst::ParentArgues argues = context->get<cst::ParentArgues>()) {
            if (const cst::ArgumentList args = argues->get<cst::ArgumentList>()) {
                if (const cst::IndexValues values = args->get<cst::IndexValues>()) {
                    const auto exprs = values->exprs();
                    if (exprs.size() == 1 && exprs[0].kind() == NK::RefData) {
                        argument = std::string(exprs[0].text());
                    }
                }
            }
        }
        const bool writtenArgument = !argument.empty();
        const auto found = writtenArgument ? ruleWhenNodeParams_.find(argument)
                                           : ruleWhenNodeParams_.end();
        if (writtenArgument && found != ruleWhenNodeParams_.end()) {
            node_ptr_t nodeRef = createNodeAs<RefDataLoad>(Reference(found->second));
            setNodeTokenRangeByContext(nodeRef, context);
            LEAVE("AnnoExpr");
            return nodeRef;
        }
        diags_->of(SemanticDiag::NodeOfNotAParameter)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit(argument, ruleWhenName_);
        // The diagnostic is the report; the condition still needs a value, and `false` says
        // "this rule does not apply here", which is what a name that holds no node means.
        node_ptr_t noNode = createNodeAs<LiteralLoad>(Literal(LiteralType::Boolean, "false"));
        setNodeTokenRangeByContext(noNode, context);
        LEAVE("AnnoExpr");
        return noNode;
    }
    node_ptr_t lhsNode = any2node(visitExpr(context->expr()));
    for (size_t i = 1; i < context->childCount(); i++) {
        auto child = context->child(i);
        if (child.isNode()) {
            node_ptr_t exprNode = nullptr;
            if (child.node().is(NK::Indices)) {
                exprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::Index);
                setNodeTokenRangeByContext(exprNode, cst::Indices(child.node()));
                node_ptr_t rhsNode = any2node(visitIndices(cst::Indices(child.node())));
                *exprNode << lhsNode << rhsNode;
                registerAstSemanticBundle(
                    exprNode,
                    {
                        // The operator is the bracket: a fault of the index (out of range) is
                        // reported there, as a division by zero is reported at its `/`.
                        semanticPart(
                            camel::source::SemanticRole::Operator,
                            deriveAstAnchorOrigin(
                                exprNode,
                                cst::Indices(child.node())->firstToken(),
                                "ast.index.operator"),
                            -1,
                            "[]"),
                        semanticPart(
                            camel::source::SemanticRole::IndexExpr,
                            nodeOrigin(rhsNode),
                            -1,
                            "indices"),
                        semanticPart(
                            camel::source::SemanticRole::Receiver,
                            nodeOrigin(lhsNode),
                            -1,
                            "target"),
                    });
            } else if (child.node().is(NK::ParentArgues)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
                setNodeTokenRangeByContext(exprNode, cst::ParentArgues(child.node()));
                const ArgumentValues values = value_cast<ArgumentValues>(
                    visitParentArgues(cst::ParentArgues(child.node())));
                node_ptr_t dataList      = values.data;
                node_ptr_t namedDataList = values.named;
                if (values.positionalAfterNamed != nullptr) {
                    // `f(a: 1, 2)`: the call refuses it, at the argument out of place
                    // (design principles 4.4).
                    exprNode->loadAs<ReservedExprLoad>()->setPositionalAfterNamed(
                        nodeOrigin(values.positionalAfterNamed));
                }
                *exprNode << lhsNode << dataList << namedDataList;
                std::vector<camel::source::SemanticPart> parts = {
                    semanticPart(
                        camel::source::SemanticRole::Callee,
                        nodeOrigin(lhsNode),
                        -1,
                        "callee"),
                    semanticPart(
                        camel::source::SemanticRole::ArgList,
                        nodeOrigin(dataList),
                        -1,
                        "args"),
                };
                for (size_t argIndex = 0; argIndex < dataList->size(); ++argIndex) {
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::Argument,
                        nodeOrigin(dataList->at(argIndex)),
                        static_cast<int32_t>(argIndex),
                        "arg"));
                }
                registerAstSemanticBundle(exprNode, std::move(parts));
            } else if (child.node().is(NK::MemberAccess)) {
                auto access = cst::MemberAccess(child.node());
                if (access->child(1).isToken() && access->child(1).text() == "[") {
                    exprNode = makeStaticAccess(
                        lhsNode, access->child(0), access->child(2).node(), access);
                } else {
                    exprNode = makeMemberAccess(lhsNode, access->child(0), access->child(1), access);
                }
            } else if (child.node().is(NK::StructData)) {
                // `Shape::Rect { w: 2 }`: a struct-shape variant is built from a struct literal,
                // which is one argument. The graph builder decides by the shape of the variant
                // whether the literal is its payload or its only field.
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
                setNodeTokenRangeByContext(exprNode, cst::StructData(child.node()));
                node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
                *dataList << any2node(visitStructData(cst::StructData(child.node())));
                *exprNode << lhsNode << dataList << createNodeAs<RepeatedLoad>("NamedData");
                registerAstSemanticBundle(
                    exprNode,
                    {
                        semanticPart(
                            camel::source::SemanticRole::Callee,
                            nodeOrigin(lhsNode),
                            -1,
                            "callee"),
                        semanticPart(
                            camel::source::SemanticRole::ArgList,
                            nodeOrigin(dataList),
                            -1,
                            "args"),
                        semanticPart(
                            camel::source::SemanticRole::Argument,
                            nodeOrigin(dataList->front()),
                            0,
                            "payload"),
                    });
            } else if (child.node().is(NK::TypeArguments)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Instantiate);
                setNodeTokenRangeByContext(exprNode, cst::TypeArguments(child.node()));
                // Positional (`typeList`) or named (`A = int`): a named argument is a
                // NamedTypeLoad that holds its type.
                node_ptr_t typeArgs;
                if (child.node().get<cst::TypeList>()) {
                    typeArgs = any2node(visitTypeList(child.node().get<cst::TypeList>()));
                } else {
                    typeArgs = createNodeAs<RepeatedLoad>("Type");
                    setNodeTokenRangeByContext(typeArgs, cst::TypeArguments(child.node()));
                    for (const auto named : child.node().all<cst::NamedTypeArgument>()) {
                        node_ptr_t namedNode = createNodeAs<NamedTypeLoad>(
                            Reference(named->tok(TK::Identifier)->text()));
                        setNodeTokenRangeByContext(namedNode, named);
                        *namedNode << any2node(visitType(named->type()));
                        *typeArgs << namedNode;
                    }
                }
                *exprNode << lhsNode << typeArgs;
                registerAstSemanticBundle(
                    exprNode,
                    {
                        semanticPart(
                            camel::source::SemanticRole::Callee,
                            nodeOrigin(lhsNode),
                            -1,
                            "callee"),
                        semanticPart(
                            camel::source::SemanticRole::GenericArgument,
                            nodeOrigin(exprNode->at(1)),
                            -1,
                            "typeArgs"),
                    });
            } else if (child.node().is(NK::AngledValues)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
                setNodeTokenRangeByContext(exprNode, cst::AngledValues(child.node()));
                const ArgumentValues values = value_cast<ArgumentValues>(
                    visitAngledValues(cst::AngledValues(child.node())));
                node_ptr_t dataList      = values.data;
                node_ptr_t namedDataList = values.named;
                *exprNode << lhsNode << dataList << namedDataList;
                std::vector<camel::source::SemanticPart> parts = {
                    semanticPart(
                        camel::source::SemanticRole::Callee,
                        nodeOrigin(lhsNode),
                        -1,
                        "callee"),
                    semanticPart(
                        camel::source::SemanticRole::GenericArgument,
                        nodeOrigin(dataList),
                        -1,
                        "typeArgs"),
                };
                for (size_t argIndex = 0; argIndex < dataList->size(); ++argIndex) {
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::GenericArgument,
                        nodeOrigin(dataList->at(argIndex)),
                        static_cast<int32_t>(argIndex),
                        "typeArg"));
                }
                registerAstSemanticBundle(exprNode, std::move(parts));
            }
            lhsNode = exprNode;
        } else {
            // Every shape of an `annoExpr` is read above; the postfix `!` is not one of them any
            // more (the parser refuses it: the mark belongs to a `T!`, not to a value).
            ASSERT(false, "an annoExpr child of another shape reached the builder");
        }
    }
    LEAVE("AnnoExpr");
    return lhsNode;
}

node_ptr_t
Builder::makeMemberAccess(node_ptr_t lhs, syn::Child dot, syn::Child member, syn::Node range) {
    syn::TokenRef memberToken = tokenOf(member);
    std::string memberText    = member->text();
    size_t memberBegin        = memberToken ? memberToken->begin() : 0;
    size_t memberEnd          = memberToken ? memberToken->end() : 0;
    if (memberToken && memberToken.kind() == TK::Real) {
        // `t.0.1`: the lexer reads `0.1` as one REAL. It stands for two indices, the first
        // applied here and the second to its result.
        const size_t split = memberText.find('.');
        const bool plain   = split != std::string::npos &&
                             memberText.find_first_not_of("0123456789.") == std::string::npos;
        if (!plain) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(rangeOf(memberToken))
                .commit("member index", memberText);
        } else {
            node_ptr_t first = createNodeAs<ReservedExprLoad>(ReservedDataOp::Access);
            setNodeTokenRangeByContext(first, range);
            Reference firstRef(memberText.substr(0, split));
            node_ptr_t firstMember = createNodeAs<RefDataLoad>(firstRef);
            setNodeTokenRange(
                firstMember,
                static_cast<size_t>(memberToken->index()),
                static_cast<size_t>(memberToken->index() + 1));
            setNodeOriginByOffsets(
                firstMember,
                memberBegin,
                memberBegin + split,
                "ast.access.member");
            *first << lhs << firstMember;
            registerAstSemanticBundle(
                first,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(first, tokenOf(dot), "ast.access.dot"),
                        -1,
                        "."),
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(lhs),
                        -1,
                        "target"),
                    semanticPart(
                        camel::source::SemanticRole::MemberName,
                        nodeOrigin(firstMember),
                        -1,
                        firstRef.ident()),
                });
            lhs         = first;
            memberBegin = memberBegin + split + 1;
            memberText  = memberText.substr(split + 1);
        }
    }
    node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Access);
    setNodeTokenRangeByContext(dataNode, range);
    Reference ref(memberText);
    node_ptr_t rhsNode = createNodeAs<RefDataLoad>(ref);
    if (memberToken) {
        setNodeTokenRange(
            rhsNode,
            static_cast<size_t>(memberToken->index()),
            static_cast<size_t>(memberToken->index() + 1));
        setNodeOriginByOffsets(rhsNode, memberBegin, memberEnd, "ast.access.member");
    }
    *dataNode << lhs << rhsNode;
    registerAstSemanticBundle(
        dataNode,
        {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(dataNode, tokenOf(dot), "ast.access.dot"),
                -1,
                "."),
            semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(lhs), -1, "target"),
            semanticPart(
                camel::source::SemanticRole::MemberName,
                nodeOrigin(rhsNode),
                -1,
                ref.ident()),
        });
    return dataNode;
}

/*
accessExpr
    : primaryData ('.' (IDENTIFIER | INTEGER | REAL | '[' dataExpr ']'))*
    ;
*/
Value Builder::visitAccessExpr(cst::AccessExpr context) {
    ENTER("AccessExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr()));
    size_t i           = 1;
    while (i < context->childCount()) {
        std::string strOp = context->child(i)->text();
        if (strOp != ".") {
            throw std::runtime_error("Invalid access operator: " + strOp);
        }
        if (context->child(i + 1).isToken() && context->child(i + 1).text() == "[") {
            // `.` '[' dataExpr ']': a field or element named by a compile-time key.
            const auto &keyChild = context->child(i + 2);
            lhsNode = makeStaticAccess(lhsNode, context->child(i), keyChild.node(), context);
            i += 4;
            continue;
        }
        lhsNode = makeMemberAccess(lhsNode, context->child(i), context->child(i + 1), context);
        i += 2;
    }
    LEAVE("AccessExpr");
    return lhsNode;
}

node_ptr_t Builder::makeStaticAccess(
    node_ptr_t lhs, syn::Child dot, syn::Node key, syn::Node range) {
    node_ptr_t dataNode = createNodeAs<StaticAccessLoad>();
    setNodeTokenRangeByContext(dataNode, range);
    node_ptr_t keyNode = any2node(visitExpr(key));
    *dataNode << lhs << keyNode;
    registerAstSemanticBundle(
        dataNode,
        {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(dataNode, tokenOf(dot), "ast.access.static"),
                -1,
                ".[]"),
            semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(lhs), -1, "target"),
            semanticPart(
                camel::source::SemanticRole::IndexExpr,
                nodeOrigin(keyNode),
                -1,
                "key"),
        });
    return dataNode;
}

/*
structData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct is immutable
    | '{' staticForExpr '}'        // a struct comprehension
    ;
*/
Value Builder::visitStructData(cst::StructData context) {
    ENTER("StructData");
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        // `{ static for n, F in fields T => e }`: the value has the fields of `T`, each from one
        // round; the generator is built once and the rounds are packed in the graph builder.
        node_ptr_t compNode = createNodeAs<StructCompLoad>();
        setNodeTokenRangeByContext(compNode, context);
        *compNode << any2node(visitStaticForExpr(generator));
        LEAVE("StructData");
        return compNode;
    }
    node_ptr_t dataNode = createNodeAs<StructDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::PairedValues>()) {
        *dataNode << any2node(visitPairedValues(context->get<cst::PairedValues>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("NamedData");
    }
    LEAVE("StructData");
    return dataNode;
}

/*
arrayData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
Value Builder::visitArrayData(cst::ArrayData context) {
    ENTER("ArrayData");
    node_ptr_t dataNode = createNodeAs<ArrayDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::IndexValues>()) {
        *dataNode << any2node(visitIndexValues(context->get<cst::IndexValues>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
        if (context->tok(TK::KwFor)) {
            // `[e for x in xs if c]` (design principles 4.2): `filter` then `map`. The parts
            // are kept as they are written, after the (empty) element list: the element
            // expression, the name, the source and the optional condition. The GCT builder
            // lowers them to the two calls (visitComprehension).
            std::vector<syn::Node> exprs;
            for (const auto &expr : context->exprs()) {
                exprs.push_back(expr);
            }
            // The name after `for` is an expression node of its own when a bare name is one.
            const size_t written  = 2 + (context->has(TK::KwIf) ? 1 : 0);
            const bool nameIsExpr = exprs.size() == written + 1;
            const syn::Node nameSyntax =
                nameIsExpr ? exprs[1] : syn::Node(context->get<cst::IdentRef>());
            if (nameIsExpr) {
                exprs.erase(exprs.begin() + 1);
            }
            *dataNode << any2node(visitExpr(exprs[0]));
            node_ptr_t nameNode = createNodeAs<RefDataLoad>(Reference(nameSyntax->text()));
            setNodeTokenRangeByContext(nameNode, nameSyntax);
            *dataNode << nameNode;
            *dataNode << any2node(visitExpr(exprs[1]));
            node_ptr_t condNode = createNodeAs<OptionalLoad>("Data");
            if (exprs.size() > 2) {
                setNodeTokenRangeByContext(condNode, exprs[2]);
                *condNode << any2node(visitExpr(exprs[2]));
            }
            *dataNode << condNode;
        }
    }
    LEAVE("ArrayData");
    return dataNode;
}

/*
tupleData
    : '(' dataList? ','? ')'
    | '(' staticForExpr ')'        // a tuple comprehension
    ;
*/
Value Builder::visitTupleData(cst::TupleData context) {
    ENTER("TupleData");
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        node_ptr_t compNode = createNodeAs<TupleCompLoad>();
        setNodeTokenRangeByContext(compNode, context);
        *compNode << any2node(visitStaticForExpr(generator));
        LEAVE("TupleData");
        return compNode;
    }
    // `(e)`: a parenthesized expression, not a tuple (three children: the brackets and the
    // list; a trailing comma would be a fourth).
    if (context->get<cst::DataList>() && context->get<cst::DataList>()->exprs().size() == 1 &&
        context->childCount() == 3) {
        Value inner = visitExpr(context->get<cst::DataList>()->expr(0));
        LEAVE("TupleData");
        return inner;
    }
    node_ptr_t dataNode = createNodeAs<TupleDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::DataList>()) {
        *dataNode << any2node(visitDataList(context->get<cst::DataList>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
    }
    LEAVE("TupleData");
    return dataNode;
}

/*
shapeComp : typeExpr '{' staticForExpr '}' ;
The shape is child 0, the generator child 1. The tree keeps both: the graph builder reads the
generator's rounds and takes the field names and the kind from the shape.
*/
Value Builder::visitShapeComp(cst::ShapeComp context) {
    ENTER("ShapeComp");
    node_ptr_t compNode = createNodeAs<ShapeCompLoad>();
    setNodeTokenRangeByContext(compNode, context);
    *compNode << any2node(visitType(context->type()));
    *compNode << any2node(visitStaticForExpr(context->get<cst::StaticForExpr>()));
    LEAVE("ShapeComp");
    return compNode;
}

/*
primaryData : identRef | literal | arrayData | structData | tupleData | funcData ;
The tree has no node for the rule. A name as an operand is a RefData node, a literal a Literal.
*/
Value Builder::visitRefData(cst::RefData context) {
    Reference ref(context->text());
    node_ptr_t res = createNodeAs<RefDataLoad>(ref);
    setNodeTokenRangeByContext(res, context);
    return res;
}

Value Builder::visitConstasData(cst::ConstasData context) {
    Reference ref(context->get<cst::IdentDef>()->text());
    node_ptr_t res = createNodeAs<ConstAsLoad>(ref);
    setNodeTokenRangeByContext(res, context);
    return res;
}

Value Builder::visitLiteralData(cst::Literal context) {
    if (context->tok(TK::FString)) {
        return visitFStringData(context);
    }
    Literal literal = value_cast<Literal>(visitLiteral(context));
    node_ptr_t res  = createNodeAs<LiteralLoad>(literal);
    setNodeTokenRangeByContext(res, context);
    return res;
}

namespace {

/// The text of a formatted string that is not an interpolation: only `\${` is decoded here,
/// because it is what keeps `${` from starting an interpolation. Every character escape, the
/// backtick and the bare `\$` (not followed by `{`) included, is left for decodeEscapes, the one
/// table of every string literal.
std::string decodeFStringText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            if (text[i + 1] == '$' && i + 2 < text.size() && text[i + 2] == '{') {
                out += "${"; // `\${` is a literal `${`
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

/// An interpolation is empty when it holds no token that is neither white space nor a comment
/// (the author's criterion). White space is never a token, so this is: every token
/// is Eof or a comment. `Command` (the ignored `#!` line) and `Error` are not comments, and either
/// leaves the interpolation non-empty.
bool isEmptyInterpolation(const syn::TokenBuffer &tokens) {
    for (const syn::Token &token : tokens.tokens()) {
        switch (token.kind) {
        case syn::TokenKind::Eof:
        case syn::TokenKind::LineComment:
        case syn::TokenKind::DocComment:
        case syn::TokenKind::BlockComment:
            break;
        default:
            return false;
        }
    }
    return true;
}

/// Shown when the text taken as a format specification contains `=>`: the ':' of a lambda's
/// return type at the top level of an interpolation was read as the start of the specification.
/// The same sentence is used where the format check fails (gir/builder_operators.cpp).
constexpr std::string_view kLambdaNeedsParenthesesHint =
    "a lambda with a return type needs its own parentheses at the top level of an "
    "interpolation: the ':' of the return type is read as the start of the format "
    "specification. Write it as ${((x: int): int => x)}";

/// The end (just past the closing `}`) of a valid `\u{H…}` escape that starts at `i`, or npos.
/// Valid means what decodeEscapes decodes: 1 to 6 hex digits and a Unicode scalar value.
size_t unicodeEscapeEnd(std::string_view text, size_t i) {
    if (i + 2 >= text.size() || text[i] != '\\' || text[i + 1] != 'u' || text[i + 2] != '{') {
        return std::string_view::npos;
    }
    size_t j       = i + 3;
    size_t digits  = 0;
    uint32_t value = 0;
    while (j < text.size() && digits < 6 && std::isxdigit(static_cast<unsigned char>(text[j]))) {
        const char d = text[j];
        value        = value * 16 + (d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
        ++digits;
        ++j;
    }
    if (digits == 0 || j >= text.size() || text[j] != '}' || !camel::utf8::isScalarValue(value)) {
        return std::string_view::npos;
    }
    return j + 1;
}

/// The pattern text for `format`: the literal braces of a formatted string are doubled, so
/// `format` prints them. A valid `\u{H…}` is a character escape, not format syntax: its braces are
/// left alone for decodeEscapes, which doubles a brace the escape produces. An invalid one
/// is kept as written, so its braces are format syntax and are doubled here.
std::string escapeFormatBraces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\\') {
            // An even run of backslashes is `\\` pairs; the last backslash of an odd run opens
            // an escape.
            size_t run = 0;
            while (i + run < text.size() && text[i + run] == '\\') {
                ++run;
            }
            out.append(text.substr(i, run - (run % 2)));
            i += run - (run % 2);
            if (run % 2 == 0) {
                continue;
            }
            const size_t end = unicodeEscapeEnd(text, i);
            if (end != std::string_view::npos) {
                out.append(text.substr(i, end - i));
                i = end;
                continue;
            }
            out += text[i];
            ++i;
            continue;
        }
        if (c == '{' || c == '}') {
            out += c;
        }
        out += c;
        ++i;
    }
    return out;
}

/// A sub-tree of an interpolation has the token indices of its own buffer; a token range is
/// only a hint for a diagnostic, so it is moved to the formatted-string token, which is what
/// the file shows. Origins, which are exact, are untouched.
void setTokenRangeDeep(const node_ptr_t &node, size_t begin, size_t end) {
    if (!node) {
        return;
    }
    node->load()->setTokenRange(begin, end);
    for (const auto &child : *node) {
        setTokenRangeDeep(child, begin, end);
    }
}

} // namespace

/*
 * A formatted string is the same tree as `'...'->format<...>`: `format` applied to the
 * interpolated expressions, then to the pattern. Building that tree here keeps every later
 * stage (types, passes, schedulers, the C++ translator) unaware of the shorthand.
 *
 * The pattern is the text of the string with each `${EXPR}` replaced by `{}` (or `{:SPEC}`),
 * the braces of the literal text doubled and `\${` unescaped. The pieces of text between the
 * interpolations keep their escapes for decodeEscapes.
 */
Value Builder::visitFStringData(cst::Literal context) {
    ENTER("FStringData");
    const syn::TokenRef token = context->firstToken();
    const std::string &source = context->tree()->source();
    const std::string_view whole(source);
    const syn::TokenBuffer &buffer = context->tree()->tokens();
    const auto interpolations      = buffer.interpolationsOf(token->index());

    // Where an interpolation lies in the file: from its `$` to just past its `}`. A diagnostic
    // about the interpolation itself (not about the expression inside it) points here.
    const auto interpolationRange = [&buffer](const syn::FStringInterpolation &part) {
        return CharRange{
            {buffer.lineOf(part.open), buffer.columnOf(part.open)},
            {buffer.lineOf(part.close + 1), buffer.columnOf(part.close + 1)}};
    };

    std::vector<node_ptr_t> args;
    std::string pattern;
    size_t cursor = token->begin() + 1; // just past the opening backtick
    bool failed   = false;

    for (const auto &part : interpolations) {
        pattern += escapeFormatBraces(
            decodeFStringText(whole.substr(cursor, static_cast<size_t>(part.open) - cursor)));

        if (part.hasSpec) {
            pattern += "{:" +
                       std::string(whole.substr(
                           part.specBegin,
                           static_cast<size_t>(part.specEnd) - part.specBegin)) +
                       "}";
        } else {
            pattern += "{}";
        }
        cursor = static_cast<size_t>(part.close) + 1;

        syn::TokenBuffer tokens = syn::Lexer::tokenizeRange(source, part.exprBegin, part.exprEnd);
        // No token that is neither white space nor a comment is `${}`, `${ }`, `${:spec}` or
        // `${/*c*/}`: an interpolation without an expression is its own mistake, not a parse
        // error inside an expression (item 1). `${/**/ 1}` has a token and is
        // an expression.
        if (isEmptyInterpolation(tokens)) {
            diags_->of(SyntaxDiag::EmptyInterpolation).at(interpolationRange(part)).commit();
            failed = true;
            continue;
        }
        const size_t beforeExpression = diags_->count();
        syn::Tree sub                 = syn::Parser::parseExpression(std::move(tokens));
        syn::reportLexicalErrors(sub.tokens(), *diags_);
        if (sub.tokens().errors().empty()) {
            syn::reportSyntaxErrors(sub, *diags_);
        }
        if (!sub.root() || sub.hasErrors()) {
            // The first ':' outside every bracket ends the expression. A lambda with a return
            // type has such a ':' at its top level, so its `=>` ended up in the specification
            // and the expression is incomplete: say what the fix is (item 1).
            if (part.hasSpec) {
                const std::string_view spec = whole.substr(
                    part.specBegin, static_cast<size_t>(part.specEnd) - part.specBegin);
                if (spec.find("=>") != std::string_view::npos) {
                    diags_->addRelatedToErrorsFrom(
                        beforeExpression,
                        DiagLabel{
                            interpolationRange(part),
                            std::string(kLambdaNeedsParenthesesHint)});
                }
            }
            failed = true;
            continue;
        }
        node_ptr_t exprNode = any2node(visitExpr(sub.root()));
        setTokenRangeDeep(exprNode, token->index(), token->index() + 1);
        args.push_back(exprNode);
    }
    pattern += escapeFormatBraces(
        decodeFStringText(whole.substr(cursor, static_cast<size_t>(token->end()) - 1 - cursor)));

    if (failed) {
        // The errors are already reported; a placeholder keeps the tree well formed.
        node_ptr_t res = createNodeAs<LiteralLoad>(Literal(LiteralType::String, ""));
        setNodeTokenRangeByContext(res, context);
        LEAVE("FStringData");
        return res;
    }

    node_ptr_t literalNode = createNodeAs<LiteralLoad>(Literal(LiteralType::String, pattern));
    setNodeTokenRangeByContext(literalNode, context);
    unwrapNodeAs<LiteralLoad>(literalNode)->setFromFormattedString();

    node_ptr_t refNode = createNodeAs<RefDataLoad>(qualifyRef(Reference("format")));
    setNodeTokenRangeByContext(refNode, context);

    node_ptr_t withArgs = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(withArgs, context);
    for (const auto &arg : args) {
        *withArgs << arg;
    }
    node_ptr_t withNamed = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(withNamed, context);

    node_ptr_t bindNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
    setNodeTokenRangeByContext(bindNode, context);
    *bindNode << refNode << withArgs << withNamed;

    node_ptr_t normArgs = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(normArgs, context);
    *normArgs << literalNode;
    node_ptr_t normNamed = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(normNamed, context);

    node_ptr_t callNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(callNode, context);
    *callNode << bindNode << normArgs << normNamed;
    LEAVE("FStringData");
    return callNode;
}

/*
literal
    : ('+' | '-')? INTEGER
    | ('+' | '-')? REAL
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
Value Builder::visitLiteral(cst::Literal context) {
    LiteralType type;
    string data = context->text();
    if (context->tok(TK::Integer)) {
        type = LiteralType::Integer;
    } else if (context->tok(TK::Real)) {
        type = LiteralType::Real;
    } else if (context->tok(TK::Imaginary)) {
        // Keep the trailing `i`: the literal's text is what the GCT reads the value from.
        type = LiteralType::Imaginary;
    } else if (context->tok(TK::String) || context->tok(TK::FString)) {
        // The text of a string constant, as a pattern takes it (`case 'x'`). The formatted
        // strings of an expression go to visitFStringData (visitLiteralData), and the pattern
        // production no longer takes one, so what arrives here is a plain string.
        data = data.substr(1, data.size() - 2); // Remove quotes
        type = LiteralType::String;
    } else if (context->tok(TK::MultiStr)) {
        data = data.substr(3, data.size() - 6); // Remove triple quotes
        type = LiteralType::String;
    } else if (context->tok(TK::KwTrue) || context->tok(TK::KwFalse)) {
        type = LiteralType::Boolean;
    } else if (context->tok(TK::KwNull)) {
        type = LiteralType::Null;
    } else {
        throw std::runtime_error("Unknown literal type in visitLiteral");
    }
    Literal literal(type, data);
    return literal;
}

/*
pattern
    : identRef ('(' (pattern (',' pattern)*)? ','? ')' | '{' (IDENTIFIER (':' pattern)? ','?)* '}'
      | identDef)?
    | literal
    | '_'
    ;
*/
Value Builder::visitEnumPattern(cst::Pattern context, cst::IdentRef ref) {
    ENTER("EnumPattern");
    const auto structPattern = context->get<cst::EnumStructPattern>();
    node_ptr_t node =
        createNodeAs<EnumPatternLoad>(Reference(ref->text()), static_cast<bool>(structPattern));
    setNodeTokenRangeByContext(node, context);
    auto &fields    = node->loadAs<EnumPatternLoad>()->fields();
    const auto bind = [&](cst::Pattern pattern, const std::string &name) {
        std::string binding;
        if (pattern->text() != "_") {
            // A field is a `fieldPattern`: a name or `_`. The parser refuses a nested pattern and
            // a literal where it is written (`parsePattern`), so a name is all that arrives.
            ASSERT(
                pattern->get<cst::IdentRef>() && !pattern->get<cst::EnumTuplePattern>() &&
                    !pattern->get<cst::EnumStructPattern>(),
                "a field pattern of another shape reached the builder");
            binding = pattern->get<cst::IdentRef>()->text();
        }
        fields.push_back(EnumPatternField{name, binding});
    };
    if (structPattern) {
        for (size_t i = 0; i < structPattern->childCount(); ++i) {
            const auto child = structPattern->child(i);
            if (!child.isToken() || child.token()->kind() != TK::Identifier) {
                continue;
            }
            std::string name = child.text();
            const auto next  = structPattern->child(i + 1);
            if (next.isToken() && next.token()->kind() == TK::Colon) {
                const auto value = structPattern->child(i + 2);
                if (value.isNode()) {
                    bind(cst::Pattern(value.node()), name);
                }
            } else {
                // `{ w, h }` binds the field of that name.
                fields.push_back(EnumPatternField{name, name});
            }
        }
    } else if (const auto tuplePattern = context->get<cst::EnumTuplePattern>()) {
        for (const auto pattern : tuplePattern->all<cst::Pattern>()) {
            bind(pattern, "");
        }
    }
    LEAVE("EnumPattern");
    return node;
}

/*
enumType
    : 'enum' '{' (enumVariant (',' enumVariant)*)? ','? '}'
    ;
enumVariant
    : identDef ('(' typeList? ','? ')' | '{' (enumField (',' enumField)*)? ','? '}')?
    ;
enumField
    : identDef ':' typeExpr
    ;
*/
Value Builder::visitEnumType(cst::EnumType context) {
    ENTER("EnumType");
    node_ptr_t typeNode = createNodeAs<EnumTypeLoad>();
    setNodeTokenRangeByContext(typeNode, context);
    auto &variants = typeNode->loadAs<EnumTypeLoad>()->variants();
    for (const auto variantContext : context->all<cst::EnumVariant>()) {
        EnumVariantDecl variant;
        variant.name = variantContext->get<cst::IdentDef>()->text();
        // The field types are the child nodes of the declaration, one list per variant in the
        // order the variants are written; GCT reads them back in that order.
        node_ptr_t fieldTypes = createNodeAs<RepeatedLoad>("EnumField");
        setNodeTokenRangeByContext(fieldTypes, variantContext);
        if (const auto tuple = variantContext->get<cst::TypeList>()) {
            variant.shape = EnumVariantShape::Tuple;
            for (const auto type : tuple->types()) {
                *fieldTypes << any2node(visitType(type));
            }
        } else if (variantContext->get<cst::EnumField>()) {
            variant.shape = EnumVariantShape::Struct;
            for (const auto field : variantContext->all<cst::EnumField>()) {
                variant.fieldNames.push_back(field->get<cst::IdentDef>()->text());
                *fieldTypes << any2node(visitType(field->type()));
            }
        }
        variants.push_back(std::move(variant));
        *typeNode << fieldTypes;
    }
    LEAVE("EnumType");
    return typeNode;
}

/*
typeExpr
    : arrayType ('?' | '!' | '..')*
    ;
*/
Value Builder::visitTypeExpr(cst::TypeExpr context) {
    ENTER("TypeExpr");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    // The marks, in the order they were read, innermost first: `T?!` is a fallible nullable,
    // `T?..` is the stream of a nullable `T`, and `T..?` is the possibly absent stream. `..` is
    // the stream (design principles 5.4), `?`/`!` the nullable and fallible marks.
    for (size_t i = 1; i < context->childCount(); ++i) {
        const std::string mark = context->child(i)->text();
        node_ptr_t markedTypeNode =
            mark == ".." ? createNodeAs<StreamTypeLoad>()
                         : createNodeAs<NullableTypeLoad>(mark == "!");
        setNodeTokenRangeByContext(markedTypeNode, context);
        *markedTypeNode << lhsNode;
        lhsNode = markedTypeNode;
    }
    LEAVE("TypeExpr");
    return lhsNode;
}

/*
arrayType
    : specType arrayDim+
    ;
*/
Value Builder::visitArrayType(cst::ArrayType context) {
    ENTER("ArrayType");
    node_ptr_t res = any2node(visitType(context->type()));
    std::vector<DimGroup> groups;
    for (const auto dim : context->all<cst::ArrayDim>()) {
        groups.push_back(dimGroupOf(dim));
    }
    if (!groups.empty()) {
        node_ptr_t arrayTypeNode = createNodeAs<ArrayTypeLoad>(std::move(groups));
        setNodeTokenRangeByContext(arrayTypeNode, context);
        *arrayTypeNode << res;
        // The extent expressions stay children of the node as well: everything written is in
        // the tree, and a reader that walks it sees `?` as the one item that has no node.
        for (const DimGroup &group : arrayTypeNode->loadAs<ArrayTypeLoad>()->dims()) {
            for (const auto &extent : group.extents) {
                if (extent) {
                    *arrayTypeNode << extent;
                }
            }
        }
        res = arrayTypeNode;
    }
    LEAVE("ArrayType");
    return res;
}

/*
arrayDim
    : '[' (']' | arrayDimItem (',' arrayDimItem)* ','?)? ']'
    ;
arrayDimItem : '?' | '..' identDef? | dataExpr
*/
Value Builder::visitArrayDim(cst::ArrayDim context) {
    node_ptr_t res = createNodeAs<ArrayDimLoad>(dimGroupOf(context));
    setNodeTokenRangeByContext(res, context);
    for (const auto &extent : res->loadAs<ArrayDimLoad>()->dim().extents) {
        if (extent) {
            *res << extent;
        }
    }
    return res;
}

DimGroup Builder::dimGroupOf(cst::ArrayDim context) {
    DimGroup group;
    bool packNameNext = false;
    for (size_t i = 0; i < context->childCount(); ++i) {
        const auto child = context->child(i);
        if (child.isToken()) {
            if (child.token().is(TK::Question)) {
                group.extents.push_back(nullptr);
                ++group.itemCount;
                packNameNext = false;
            } else if (child.token().is(TK::DotDot)) {
                if (group.pack != DimPack::None) {
                    group.packRepeated = true;
                } else {
                    group.pack     = DimPack::Anonymous;
                    group.packItem = group.itemCount;
                }
                ++group.itemCount;
                packNameNext = true;
            }
            continue;
        }
        const auto node = child.node();
        if (packNameNext && node.is(NK::IdentDef)) {
            group.pack     = DimPack::Named;
            group.packName = node.text();
            packNameNext   = false;
            continue;
        }
        packNameNext = false;
        if (node.isExpr()) {
            group.extents.push_back(any2node(visitExpr(node)));
            ++group.itemCount;
        }
    }
    return group;
}

/*
typeOrData : typeExpr | arrayDim | CONST dataExpr ;
The tree has a node for the second alternative only.
*/
Value Builder::visitConstData(cst::ConstData context) { return visitExpr(context->expr()); }

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
The node exists only with arguments: its first type child is the type that is specialized.
*/
Value Builder::visitSpecType(cst::SpecType context) {
    const auto types        = context->types();
    const size_t count      = types.size();
    node_ptr_t res          = any2node(visitType(types[0]));
    node_ptr_t specTypeNode = createNodeAs<SpecTypeLoad>();
    setNodeTokenRangeByContext(specTypeNode, context);
    if (count > 1) {
        node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("TypeOrData");
        setNodeTokenRangeBetween(repeatNode, types[1], types[count - 1]);
        for (size_t i = 1; i < count; ++i) {
            if (types[i].is(NK::ConstData) &&
                types[i].get<cst::ConstData>()->expr().is(NK::ArrayData)) {
                // A shape is the constant it is made of; `const` was how the shape of a module
                // type was written before array types took it over (plan P3, 4.1).
                diags_->of(SemanticDiag::InvalidTypeArgument)
                    .at(rangeOf(types[i].firstToken(), types[i].lastToken()))
                    .commit(
                        types[0].text(),
                        std::format(
                            "a shape is written without 'const': '{}<f32, [3]>'",
                            types[0].text()));
            }
            *repeatNode << any2node(visitType(types[i]));
        }
        *specTypeNode << res << repeatNode;
        res = specTypeNode;
    }
    return res;
}

/*
primaryType
    : structType | identRef | '(' typeExpr ')' | tupleType | funcType
    | TYPEOF dataExpr | TYPEAS identDef
    ;
The tree has no node for the rule, one kind per alternative instead.
*/
Value Builder::visitRefType(cst::RefType context) {
    node_ptr_t res = createNodeAs<RefTypeLoad>(Reference(context->text()));
    setNodeTokenRangeByContext(res, context);
    return res;
}

/*
matchTypeType : 'match' 'type' typeExpr '{' typeArm* '}'
typeArm       : 'case' typeCasePattern ('|' typeCasePattern)* '=>' typeExpr
The cases are the cases of `match type` over values, and each holds the type it gives.
*/
Value Builder::visitMatchTypeType(cst::MatchTypeType context) {
    node_ptr_t res = createNodeAs<MatchTypeTypeLoad>();
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitType(context->type()));
    node_ptr_t arms = createNodeAs<RepeatedLoad>("TypeCase");
    setNodeTokenRangeByContext(arms, context);
    for (const auto arm : context->all<cst::TypeArm>()) {
        *arms << any2node(visitTypeArm(arm));
    }
    *res << arms;
    return res;
}

Value Builder::visitTypeArm(cst::TypeArm context) {
    node_ptr_t arm = createNodeAs<TypeCaseLoad>(
        typeCasePatternsOf(patternsOf(context->all<cst::TypeCasePattern>())));
    setNodeTokenRangeByContext(arm, context);
    *arm << any2node(visitType(context->type()));
    return arm;
}

Value Builder::visitPackType(cst::PackType context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::Pack);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitType(context->type()));
    return res;
}

Value Builder::visitTypeOfType(cst::TypeOfType context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::TypeOf);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitExpr(context->expr()));
    return res;
}

/*
structType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
Value Builder::visitStructType(cst::StructType context) {
    ENTER("StructType");
    node_ptr_t res        = createNodeAs<StructTypeLoad>();
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("NamedType");
    setNodeTokenRangeByContext(repeatNode, context);
    for (const auto pair : context->all<cst::KeyTypePair>()) {
        node_ptr_t pairNode = any2node(visitKeyTypePair(pair));
        *repeatNode << pairNode;
    }
    *res << repeatNode;
    LEAVE("StructType");
    return res;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
Value Builder::visitTypeList(cst::TypeList context) {
    ENTER("TypeList");
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("Type");
    setNodeTokenRangeByContext(repeatNode, context);
    for (const auto typeExpr : context->types()) {
        node_ptr_t typeExprNode = any2node(visitType(typeExpr));
        *repeatNode << typeExprNode;
    }
    LEAVE("TypeList");
    return repeatNode;
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
Value Builder::visitTupleType(cst::TupleType context) {
    ENTER("TupleType");
    node_ptr_t res = createNodeAs<TupleTypeLoad>();
    setNodeTokenRangeByContext(res, context);
    if (context->get<cst::TypeList>()) {
        node_ptr_t repeatNode = any2node(visitTypeList(context->get<cst::TypeList>()));
        *res << repeatNode;
    } else {
        *res << createNodeAs<RepeatedLoad>("Type");
    }
    LEAVE("TupleType");
    return res;
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
Value Builder::visitFuncType(cst::FuncType context) {
    ENTER("FuncType");

    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    setNodeTokenRangeByContext(funcTypeNode, context);
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
    }
    funcType->setModifiers(modSet);

    if (context->get<cst::AngledParams>()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->get<cst::AngledParams>()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->get<cst::ParentParams>()) {
        node_ptr_t normParams = any2node(visitParentParams(context->get<cst::ParentParams>()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    if (context->type()) {
        setNodeTokenRangeByContext(typeOptNode, context->type());
        setNodeTokenRangeByContext(typeOptNode, context->type());
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    if (context->get<cst::ResourceClause>()) {
        *funcTypeNode << any2node(visitResourceClause(context->get<cst::ResourceClause>()));
    }

    LEAVE("FuncType");
    return funcTypeNode;
}

/*
identDef : IDENTIFIER ;
*/
Value Builder::visitIdentDef(cst::IdentDef context) {
    ENTER("IdentDef");
    Reference ref(context->tok(TK::Identifier)->text());
    LEAVE("IdentDef");
    return ref;
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
Value Builder::visitIdentRef(cst::IdentRef context) {
    ENTER("IdentRef");
    Reference ref(context->text());
    LEAVE("IdentRef");
    return ref;
}

/*
loopExpr : 'loop' loopState (',' loopState)* loopFor? loopWhile? 'do' blockExpr ;
loopState : identDef '=' logicalOrExpr ;
The children of the node are described at LoopExprLoad.
*/
Value Builder::visitLoopExpr(cst::LoopExpr context) {
    std::vector<Reference> state;
    node_ptr_t inits = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(inits, context);
    for (const auto entry : context->all<cst::LoopState>()) {
        state.emplace_back(entry->get<cst::IdentDef>()->text());
        *inits << any2node(visitExpr(entry->expr()));
    }
    const auto forClause   = context->get<cst::LoopFor>();
    const auto whileClause = context->get<cst::LoopWhile>();
    Reference binder;
    if (forClause) {
        binder = Reference(forClause->get<cst::IdentDef>()->text());
    }
    node_ptr_t loopNode = createNodeAs<LoopExprLoad>(state, binder);
    setNodeTokenRangeByContext(loopNode, context);
    *loopNode << inits;
    node_ptr_t sequence = createNodeAs<OptionalLoad>("Data");
    if (forClause) {
        setNodeTokenRangeByContext(sequence, forClause);
        *sequence << any2node(visitExpr(forClause->expr()));
    }
    node_ptr_t condition = createNodeAs<OptionalLoad>("Data");
    if (whileClause) {
        setNodeTokenRangeByContext(condition, whileClause);
        *condition << any2node(visitExpr(whileClause->expr()));
    }
    *loopNode << sequence << condition;
    // The expression after `do`, read as the arm of an `if` is: always a block on the AST.
    *loopNode << any2node(visitBlockExpr(context->lastChild().node()));
    return loopNode;
}

// ---- Dispatch on the kind of a node ----

/// Any expression (what `dataExpr` derives).
Value Builder::visitExpr(syn::Node context) {
    switch (context.kind()) {
    case NK::RestValue:
        return visitRestValue(cst::RestValue(context));
    case NK::IfExpr:
    case NK::MatchExpr:
    case NK::MatchTypeExpr:
    case NK::StaticForExpr:
        return visitCtrlExpr(context);
    case NK::LoopExpr:
        return visitLoopExpr(cst::LoopExpr(context));
    case NK::StaticLoopExpr:
        return visitStaticLoopExpr(cst::StaticLoopExpr(context));
    case NK::AssignExpr:
        return visitAssignExpr(cst::AssignExpr(context));
    case NK::LogicalOrExpr:
        return visitLogicalOrExpr(cst::LogicalOrExpr(context));
    case NK::LogicalAndExpr:
        return visitLogicalAndExpr(cst::LogicalAndExpr(context));
    case NK::EqualityExpr:
        return visitEqualityExpr(cst::EqualityExpr(context));
    case NK::RelationalExpr:
        return visitRelationalExpr(cst::RelationalExpr(context));
    case NK::AdditiveExpr:
        return visitAdditiveExpr(cst::AdditiveExpr(context));
    case NK::MultiplicativeExpr:
    case NK::PowerExpr:
        // operand '^' operand: built as the multiplicative operators are.
        return visitMultiplicativeExpr(cst::MultiplicativeExpr(context));
    case NK::NullableExpr:
        return visitNullableExpr(cst::NullableExpr(context));
    case NK::OrExpr:
    case NK::AndExpr:
        return visitEagerLogicExpr(context);
    case NK::CastExpr:
    case NK::UnaryExpr:
        return visitUnaryExpr(context);
    case NK::LinkExpr:
        return visitLinkExpr(cst::LinkExpr(context));
    case NK::RangeExpr:
        return visitRangeExpr(cst::RangeExpr(context));
    case NK::AnnoExpr:
        return visitAnnoExpr(cst::AnnoExpr(context));
    case NK::AccessExpr:
        return visitAccessExpr(cst::AccessExpr(context));
    case NK::RefData:
        return visitRefData(cst::RefData(context));
    case NK::ConstasData:
        return visitConstasData(cst::ConstasData(context));
    case NK::Literal:
        return visitLiteralData(cst::Literal(context));
    case NK::ArrayData:
        return visitArrayData(cst::ArrayData(context));
    case NK::StructData:
        return visitStructData(cst::StructData(context));
    case NK::TupleData:
        return visitTupleData(cst::TupleData(context));
    case NK::ShapeComp:
        return visitShapeComp(cst::ShapeComp(context));
    case NK::FuncData:
        return visitFuncData(cst::FuncData(context));
    default:
        throw std::runtime_error(
            "Builder: not an expression: " + std::string(syn::nodeKindName(context.kind())));
    }
}

/// Any type expression (what `typeExpr` and `typeOrData` derive).
Value Builder::visitType(syn::Node context) {
    switch (context.kind()) {
    case NK::TypeExpr:
        return visitTypeExpr(cst::TypeExpr(context));
    case NK::ArrayType:
        return visitArrayType(cst::ArrayType(context));
    case NK::ArrayDim:
        return visitArrayDim(cst::ArrayDim(context));
    case NK::SpecType:
        return visitSpecType(cst::SpecType(context));
    case NK::ConstData:
        return visitConstData(cst::ConstData(context));
    case NK::StructType:
        return visitStructType(cst::StructType(context));
    case NK::RefType:
        return visitRefType(cst::RefType(context));
    case NK::PackType:
        return visitPackType(cst::PackType(context));
    case NK::ParenType:
        return visitType(context.type());
    case NK::TupleType:
        return visitTupleType(cst::TupleType(context));
    case NK::FuncType:
        return visitFuncType(cst::FuncType(context));
    case NK::TypeOfType:
        return visitTypeOfType(cst::TypeOfType(context));
    case NK::MatchTypeType:
        return visitMatchTypeType(cst::MatchTypeType(context));
    case NK::EnumType:
        return visitEnumType(cst::EnumType(context));
    default:
        throw std::runtime_error(
            "Builder: not a type: " + std::string(syn::nodeKindName(context.kind())));
    }
}
