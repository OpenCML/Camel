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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 26, 2024
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/parse/ast/builder.h"
#include "camel/core/operator_names.h"
#include "camel/core/operator_semantics.h"

#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"
#include "camel/parse/syntax/report.h"

#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"

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
Value Builder::visitProgram(cst::Program context) {
    ENTER("Program");
    g_sourceContext = sourceContext_;
    g_sourceFileId  = sourceFileId_;
    const ArenaScope arena;

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
decl : moduleDecl | importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
     | dataDecl | funcDecl | typeDecl ;
namespaceItem : the same without moduleDecl.
The tree has no node for either rule: the declaration itself is the child.
*/
Value Builder::visitDecl(syn::Node context) {
    switch (context.kind()) {
    case NK::ModuleDecl:
        return visitModuleDecl(cst::ModuleDecl(context));
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
    case NK::RuleDecl:
        return visitRuleDecl(cst::RuleDecl(context));
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
moduleDecl : MODULE identDef ;
*/
Value Builder::visitModuleDecl(cst::ModuleDecl context) {
    ENTER("ModuleDecl");
    Reference ref(context->get<cst::IdentDef>()->text());
    module_->setRef(ref);
    LEAVE("ModuleDecl");
    return Value();
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
blockStmt  : WAIT? stmtBlock ;
*/
Value Builder::visitBlockStmt(cst::BlockStmt context) {
    ENTER("BlockStmt");
    node_ptr_t res = any2node(visitStmtBlock(context->get<cst::StmtBlock>()));
    if (context->tok(TK::KwWait)) {
        unwrapNodeAs<StmtBlockLoad>(res)->wait();
    }
    // `@numeric(...)` before a block grants its permissions to the block (design principles
    // 7.5). A decorator before a block decorates the block as a function of no parameters
    // called where it is written (language reference 7.6): the decorators are the block's second
    // child.
    node_ptr_t decorators = nullptr;
    for (const auto &anno : context->all<cst::FuncAnno>()) {
        if (const auto permissions = numericAnnotationOf(anno)) {
            unwrapNodeAs<StmtBlockLoad>(res)->grantNumeric(*permissions);
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
        if (modifier->tokCount(TK::KwAtomic) > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->tokCount(TK::KwShared) > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->tokCount(TK::KwMacro) > 0) {
            modSet.insert(Modifier::Macro);
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
    registerAstSemanticBundle(
        funcNode,
        {
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
        });

    LEAVE("FuncData");
    return funcNode;
}

/*
funcAnno   : '@' identRef (angledValues | parentIdents)? ;
`@numeric(name, ...)`: the numeric permissions the program grants (design principles 7.5).
*/
std::optional<uint8_t> Builder::numericAnnotationOf(cst::FuncAnno context) {
    const auto names = context->get<cst::ParentIdents>();
    if (!names) {
        return std::nullopt;
    }
    const std::string annotation = context->get<cst::IdentRef>()->text();
    auto reject                  = [&](const std::string &why) {
        diags_->of(SemanticDiag::InvalidAnnotation)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit(annotation, why);
    };
    if (annotation != "numeric") {
        reject(
            "the only annotation that takes names in parentheses is '@numeric(...)'; a "
            "decorator takes its arguments as '@name<args>'");
        return uint8_t{0};
    }
    uint8_t granted = 0;
    const auto list = names->get<cst::IdentList>();
    if (!list) {
        reject(
            "it names no permission; write one or more of reassociate, contract, "
            "approx_math, change_precision, finite_math, no_nans, no_infs, "
            "no_signed_zeros, flush_subnormals");
        return uint8_t{0};
    }
    for (const auto &ident : list->all<cst::IdentDef>()) {
        const std::string name(ident->text());
        if (const auto permission = camel::core::sem::parsePermission(name)) {
            granted |= *permission;
        } else {
            reject(
                "'" + name +
                "' is not a permission; the permissions are reassociate, contract, "
                "approx_math, change_precision, finite_math (no_nans, no_infs, "
                "no_signed_zeros, flush_subnormals)");
        }
    }
    return granted;
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
    auto [dataList, namedDataList] = value_cast<std::pair<node_ptr_t, node_ptr_t>>(
        visitAngledValues(context->get<cst::AngledValues>()));
    *annoNode << dataList << namedDataList;

    LEAVE("FuncAnno");
    return annoNode;
}

/*
funcDecl   :
        funcAnno*
        (WITH angledParams)?
        EXPORT? implMark? modifiers?
        FUNC identRef parentParams (':' typeExpr)? stmtBlock ;
*/
/*
ruleDecl : 'export'? IDENTIFIER identDef ruleContext? parentParams '{' ruleBody '}'
           (IDENTIFIER dataExpr)? ;
ruleBody : dataExpr ('=>' | '<=>') dataExpr ;
*/
Value Builder::visitRuleDecl(cst::RuleDecl context) {
    ENTER("RuleDecl");
    Reference ref = qualifyRef(Reference(context->get<cst::IdentDef>()->text()));
    const bool exported = context->has(TK::KwExport);
    if (exported) {
        export_->addRef(ref);
    }
    std::vector<std::string> contextNames;
    if (const auto ruleContext = context->get<cst::RuleContext>()) {
        for (const auto &name : ruleContext->all<cst::IdentDef>()) {
            contextNames.emplace_back(name->text());
        }
    }
    const auto body   = context->get<cst::RuleBody>();
    const auto sides  = body->exprs();
    const bool twoWay = body->has(TK::Equiv);

    node_ptr_t ruleNode = createNodeAs<RuleDeclLoad>(ref, twoWay, exported, contextNames);
    unwrapNodeAs<RuleDeclLoad>(ruleNode)->setDoc(docOf(context));
    if (context->get<cst::ParentParams>()) {
        *ruleNode << any2node(visitParentParams(context->get<cst::ParentParams>()));
    } else {
        *ruleNode << createNodeAs<RepeatedLoad>("NamedPair");
    }
    *ruleNode << any2node(visitExpr(sides[0]));
    *ruleNode << any2node(visitExpr(sides[1]));
    node_ptr_t whenNode = createNodeAs<OptionalLoad>("DataExpr");
    if (context->expr()) {
        setNodeTokenRangeByContext(whenNode, context->expr());
        *whenNode << any2node(visitExpr(context->expr()));
    }
    *ruleNode << whenNode;
    setNodeTokenRangeByContext(ruleNode, context);
    LEAVE("RuleDecl");
    return ruleNode;
}

Value Builder::visitFuncDecl(cst::FuncDecl context) {
    ENTER("FuncDecl");

    // Decorators are applied to the function; `@numeric(...)` grants its permissions to the
    // function's body (design principles 7.5), which is where the GCT builder reads them.
    node_ptr_t annoListNode    = nullptr;
    uint8_t numericPermissions = 0;
    for (const auto &funcAnno : context->all<cst::FuncAnno>()) {
        if (const auto permissions = numericAnnotationOf(funcAnno)) {
            numericPermissions |= *permissions;
            continue;
        }
        if (!annoListNode) {
            annoListNode = createNodeAs<RepeatedLoad>("FuncAnno");
        }
        *annoListNode << any2node(visitFuncAnno(funcAnno));
    }

    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    TokenRange typeRange    = {
        context->firstToken()->index(),
        context->get<cst::StmtBlock>()->firstToken()->index() + 1};
    setNodeTokenRange(funcTypeNode, typeRange);
    setNodeOriginByOffsets(
        funcTypeNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(context->get<cst::StmtBlock>()->firstToken()->begin()));
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto implMark = context->get<cst::ImplMark>();
    if (implMark) {
        if (implMark->text() == "inner") {
            funcType->setImplMark(ImplMark::Inner);
        } else {
            funcType->setImplMark(ImplMark::Outer);
        }
    }

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwAtomic) > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->tokCount(TK::KwShared) > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->tokCount(TK::KwMacro) > 0) {
            modSet.insert(Modifier::Macro);
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
    setNodeTokenRange(typeOptNode, typeRange);
    setNodeOriginByOffsets(
        typeOptNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(context->get<cst::StmtBlock>()->firstToken()->begin()));
    if (context->type()) {
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

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

    node_ptr_t blockNode = any2node(visitStmtBlock(context->get<cst::StmtBlock>()));
    unwrapNodeAs<StmtBlockLoad>(blockNode)->grantNumeric(numericPermissions);

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>(ref);
    setNodeTokenRangeByContext(funcNode, context);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;

    node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(ref);
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDoc(docOf(context));
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setOperatorSymbol(operatorSymbol);
    if (!operatorSymbol.empty()) {
        unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDeclaredIn(namespaceStack_);
    }
    if (annoListNode) {
        *funcDeclNode << annoListNode;
    }
    *funcDeclNode << funcNode;
    setNodeTokenRangeByContext(funcDeclNode, context);
    registerAstSemanticBundle(
        funcDeclNode,
        {
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
                nodeOrigin(blockNode),
                -1,
                "body"),
        });

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
    // carrier : identList | parentIdents | bracedIdents | bracketIdents
    UnpackType type          = UnpackType::Tuple;
    cst::IdentList identList = context->get<cst::IdentList>();
    if (auto parent = context->get<cst::ParentIdents>()) {
        identList = parent->get<cst::IdentList>();
    } else if (auto braced = context->get<cst::BracedIdents>()) {
        type      = UnpackType::Struct;
        identList = braced->get<cst::IdentList>();
    } else if (auto bracket = context->get<cst::BracketIdents>()) {
        type      = UnpackType::Array;
        identList = bracket->get<cst::IdentList>();
    }
    vector<Reference> refs;
    std::vector<cst::IdentDef> bindingIdents;
    for (const auto ident : identList->all<cst::IdentDef>()) {
        refs.push_back(topLevel ? qualifyRef(Reference(ident->text())) : Reference(ident->text()));
        bindingIdents.push_back(ident);
    }
    node_ptr_t dataDeclNode = createNodeAs<DataDeclLoad>(isVar, type, refs);
    setNodeTokenRangeByContext(dataDeclNode, context);
    unwrapNodeAs<DataDeclLoad>(dataDeclNode)->setDoc(docOf(context));
    if (context->get<cst::TypeList>()) {
        *dataDeclNode << any2node(visitTypeList(context->get<cst::TypeList>()));
    } else {
        node_ptr_t typeListNode = createNodeAs<RepeatedLoad>("Type");
        *dataDeclNode << typeListNode;
    }

    *dataDeclNode << any2node(visitDataList(context->get<cst::DataList>()));
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
    : implMark? TYPE identRef ('=' | '<' identDef (',' identDef)* ','? ('>' '=' | '>='))
      (typeExpr | STRING)
    ;
*/
Value Builder::visitTypeDecl(cst::TypeDecl context) {
    ENTER("TypeDecl");
    string uri;
    ImplMark implMark = ImplMark::Graph;
    Reference ref = qualifyRef(value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>())));
    if (context->get<cst::ImplMark>()) {
        if (context->get<cst::ImplMark>()->tok(TK::KwInner)) {
            implMark = ImplMark::Inner;
        } else {
            implMark = ImplMark::Outer;
        }
    }
    if (context->tok(TK::String)) {
        uri = context->tok(TK::String)->text();
        // Remove quotes from the string
        uri = uri.substr(1, uri.size() - 2);
    }

    node_ptr_t typeDeclNode = createNodeAs<TypeDeclLoad>(ref, implMark, uri);
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
    LEAVE("TypeDecl");
    return typeDeclNode;
}

/*
retStmt    : (RETURN | THROW) dataList ;
*/
Value Builder::visitRetStmt(cst::RetStmt context) {
    ENTER("RetStmt");
    // `throw e` returns an error value: an exit like `return`, whose value is the failed form
    // of the function's declared `T!` (design principles 3.9; the graph builder checks the
    // declaration).
    ExitType exitType   = context->tok(TK::KwThrow) ? ExitType::Throw : ExitType::Return;
    node_ptr_t exitNode = createNodeAs<ExitStmtLoad>(exitType);
    setNodeTokenRangeByContext(exitNode, context);
    if (context->get<cst::DataList>()) {
        *exitNode << any2node(visitDataList(context->get<cst::DataList>()));
    }
    LEAVE("RetStmt");
    return exitNode;
}

/*
indexValue   : '...'? dataExpr ;
*/
Value Builder::visitSpreadValue(cst::SpreadValue context) {
    ENTER("IndexValue");
    if (context->childCount() > 1) {
        // `...xs` is in the grammar but spreading is not implemented. The marker used to be
        // dropped, so that `[...xs, 4]` was built as the array of `xs` and 4.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(rangeOf(context->firstToken()))
            .commit("...", "spreading is not implemented");
    }
    node_ptr_t res = any2node(visitExpr(context->expr()));
    LEAVE("IndexValue");
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
    LEAVE("KeyTypePair");
    return namedTypeNode;
}

/*
keyValuePair : identDef ':' dataExpr | '...' dataExpr ;
*/
Value Builder::visitKeyValuePair(cst::KeyValuePair context) {
    ENTER("KeyValuePair");
    if (!context->get<cst::IdentDef>()) {
        // `...s` in a struct literal or an argument list: in the grammar, not implemented. It
        // used to dereference the missing key and crash the compiler.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(rangeOf(context->firstToken()))
            .commit("...", "spreading is not implemented");
    }
    // The error above stops compilation; a placeholder key keeps the rest of the file checked.
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
keyParamPair : VAR? identDef ':' typeExpr ('=' dataExpr)? ;
*/
Value Builder::visitKeyParamPair(cst::KeyParamPair context) {
    ENTER("KeyParamPair");
    bool isVar = false;
    if (context->tok(TK::KwVar)) {
        isVar = true;
    }
    const bool isConst = context->has(TK::KwConst);
    Reference ref(context->get<cst::IdentDef>()->text());
    node_ptr_t namedPairNode = createNodeAs<NamedPairLoad>(ref, isVar, isConst);
    setNodeTokenRangeByContext(namedPairNode, context);
    *namedPairNode << any2node(visitType(context->type()));
    node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
    if (context->expr()) {
        setNodeTokenRangeByContext(dataExprOptNode, context->expr());
        *dataExprOptNode << any2node(visitExpr(context->expr()));
    }
    *namedPairNode << dataExprOptNode;
    LEAVE("KeyParamPair");
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
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
Value Builder::visitPairedParams(cst::PairedParams context) {
    ENTER("PairedParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &keyParamPair : context->all<cst::KeyParamPair>()) {
        *rep << any2node(visitKeyParamPair(keyParamPair));
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
    if (context->get<cst::PairedValues>()) {
        setNodeTokenRangeByContext(namedDataList, context->get<cst::PairedValues>());
        namedDataList = any2node(visitPairedValues(context->get<cst::PairedValues>()));
    }
    LEAVE("ArgumentList");
    return make_pair(dataList, namedDataList);
}

/*
indices : '[' dataExpr (':' dataExpr (':' dataExpr)?)? ']' ;
*/
Value Builder::visitIndices(cst::Indices context) {
    ENTER("Indices");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    if (context->expr(0)) {
        *dataList << any2node(visitExpr(context->expr(0)));
    }
    if (context->expr(1)) {
        *dataList << any2node(visitExpr(context->expr(1)));
    }
    if (context->expr(2)) {
        *dataList << any2node(visitExpr(context->expr(2)));
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
    if (context->get<cst::ArgumentList>()) {
        auto res = value_cast<std::pair<node_ptr_t, node_ptr_t>>(
            visitArgumentList(context->get<cst::ArgumentList>()));
        dataList      = res.first;
        namedDataList = res.second;
    }
    LEAVE("ParentArgues");
    return make_pair(dataList, namedDataList);
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
    if (context->get<cst::ArgumentList>()) {
        auto res = value_cast<std::pair<node_ptr_t, node_ptr_t>>(
            visitArgumentList(context->get<cst::ArgumentList>()));
        dataList      = res.first;
        namedDataList = res.second;
    }
    LEAVE("AngledValues");
    return make_pair(dataList, namedDataList);
}

/*
pattern
    : identRef
    | literal
    | '(' (dataList | identList)? ','? ')'
    | '{' (pairedValues | identList)? ','? '}'
    | '_' // wildcard
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
        // identifier pattern over any other type as not implemented.
        if (context->get<cst::IdentDef>() && context->get<cst::IdentRef>()->text() != "error") {
            diags_->of(SemanticDiag::ConstructNotSupported)
                .at(rangeOf(context->firstToken(), context->lastToken()))
                .commit(
                    context->get<cst::IdentRef>()->text() + " " +
                        context->get<cst::IdentDef>()->text(),
                    "the only pattern of two words is 'error e'");
        }
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
        // Tuple and struct patterns are in the grammar, but what they destructure and bind
        // has not been decided. They used to end in an internal error without a position.
        const char *kind = context->get<cst::IdentRef>()          ? "an identifier pattern"
                           : context->firstToken()->text() == "(" ? "a tuple pattern"
                                                                  : "a struct pattern";
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit(
                context->text(),
                std::string(kind) + " is not implemented, only literal patterns and '_' are");
        // The error above stops compilation; a wildcard keeps the rest of the file checked.
        res = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(res, context);
    }
    LEAVE("Pattern");
    return res;
}

/*
matchCase
    : CASE pattern ('|' pattern)* '=>' blockExpr
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
    std::vector<node_ptr_t> caseNodes;
    for (const node_ptr_t &pattern : patterns) {
        node_ptr_t caseNode = createNodeAs<MatchCaseLoad>();
        setNodeTokenRangeByContext(caseNode, context);
        node_ptr_t exprNode = any2node(visitBlockExpr(context->lastChild().node()));
        *caseNode << pattern << exprNode;
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
            // The first case that matches is taken, so nothing after the wildcard can be.
            if (casesNode->size() > 0 &&
                casesNode->at(casesNode->size() - 1)->front()->type() == LoadType::Null) {
                wildcardCase = matchCase;
                wildcardSeen = true;
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
        // TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
        // Not part of the language (design principles 1): its shape on the graph would
        // depend on what the block contains. The diagnostic names what replaces it.
        diags_->of(SemanticDiag::TryCatchNotInLanguage)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit();
        // The error above stops compilation; a placeholder keeps the rest of the file checked.
        ctrlNode = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
    }
    LEAVE("CtrlExpr");
    return ctrlNode;
}

/*
waitExpr : WAIT? assignExpr ;
*/
Value Builder::visitWaitExpr(cst::WaitExpr context) {
    ENTER("WaitExpr");
    node_ptr_t dataNode = any2node(visitExpr(context->expr()));
    if (context->tok(TK::KwWait)) {
        unwrapNodeAs<DataLoad>(dataNode)->wait();
    }
    LEAVE("WaitExpr");
    return dataNode;
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
    : linkExpr ((AS | IS) typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
Value Builder::visitUnaryExpr(syn::Node context) {
    ENTER("UnaryExpr");
    node_ptr_t res = nullptr;

    switch (context.is(NK::CastExpr) ? 1 : 2) {
    case 1: {
        {
            ReservedDataOp op;
            if (context->tok(TK::KwAs)) {
                op = ReservedDataOp::As;
            } else if (context->tok(TK::KwIs)) {
                op = ReservedDataOp::Is;
            } else {
                throw std::runtime_error("Invalid unary operator: " + context->child(0)->text());
            }
            if (context->tok(TK::KwIs)) {
                // There is no run-time type test. `x is T` used to be accepted with a warning
                // and compiled as `x`.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .at(rangeOf(context->tok(TK::KwIs)))
                    .commit("is", "the operator is not implemented");
            }
            node_ptr_t dataNode = any2node(visitExpr(context->expr()));
            node_ptr_t typeNode = any2node(visitType(context->type()));
            res                 = createNodeAs<ReservedExprLoad>(op);
            setNodeTokenRangeByContext(res, context);
            *res << dataNode << typeNode;
            registerAstSemanticBundle(
                res,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(
                            res,
                            context->tok(TK::KwAs) ? context->tok(TK::KwAs)
                                                   : context->tok(TK::KwIs),
                            "ast.unary.keyword"),
                        -1,
                        context->tok(TK::KwAs) ? "as" : "is"),
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
            const auto literal = cst::Literal(context->expr());
            const bool integer = static_cast<bool>(literal->tok(TK::Integer));
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
    : compExpr (('->' | '?->') compExpr)*
    ;
*/
Value Builder::visitLinkExpr(cst::LinkExpr context) {
    ENTER("LinkExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp       = context->child(i * 2 - 1)->text();
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        if (strOp == "?->") {
            // `x ?-> f`: a branch on the tag of `x`; the call is the arm taken when `x` has
            // not failed (design principles 3.9). The marked receiver tells the GCT builder.
            node_ptr_t notNullNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::NotNullThen);
            syn::TokenRef opToken  = tokenOf(context->child(i * 2 - 1));
            setNodeTokenRange(notNullNode, opToken->index(), opToken->index() + 1);
            setNodeOriginByOffsets(
                notNullNode,
                static_cast<size_t>(opToken->begin()),
                static_cast<size_t>(opToken->end()));
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
        node_ptr_t linkNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        setNodeTokenRangeByContext(linkNode, context);
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
compExpr
    : annoExpr (('..' | '?..') annoExpr)*
    ;
*/
Value Builder::visitCompExpr(cst::CompExpr context) {
    ENTER("CompExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp        = context->child(i * 2 - 1)->text();
        node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Comp);
        setNodeTokenRangeByContext(dataNode, context);
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        if (strOp == "..") {
            // `f..g` is in the grammar, but what it builds is not a function: its result
            // could not be called and printed as 0. Its meaning is not specified (the order
            // of the two functions, functions of several parameters).
            diags_->of(SemanticDiag::ConstructNotSupported)
                .at(rangeOf(tokenOf(context->child(i * 2 - 1))))
                .commit(
                    strOp,
                    "function composition is not implemented; write a lambda, e.g. "
                    "'(x: int): int => g(f(x))'");
        }
        if (strOp == "?..") {
            // Used to be accepted with a warning and compiled as `..`.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .at(rangeOf(tokenOf(context->child(i * 2 - 1))))
                .commit(strOp, "the operator is not implemented");
            node_ptr_t notNullNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::NotNullThen);
            setNodeTokenRangeByContext(notNullNode, context->expr(i));
            *notNullNode << lhsNode;
            lhsNode = notNullNode;
        }
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
    LEAVE("CompExpr");
    return lhsNode;
}

/*
annoExpr
    : accessExpr (({isAdjacent()}? (indices | parentArgues | angledValues | '!')) | memberAccess)*
    ;
*/
Value Builder::visitAnnoExpr(cst::AnnoExpr context) {
    ENTER("AnnoExpr");
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
                auto [dataList, namedDataList] = value_cast<std::pair<node_ptr_t, node_ptr_t>>(
                    visitParentArgues(cst::ParentArgues(child.node())));
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
                exprNode    = makeMemberAccess(lhsNode, access->child(0), access->child(1), access);
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
                auto [dataList, namedDataList] = value_cast<std::pair<node_ptr_t, node_ptr_t>>(
                    visitAngledValues(cst::AngledValues(child.node())));
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
            // The postfix `!` (non-null assertion) used to be recorded and never read.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .at(rangeOf(tokenOf(child)))
                .commit("!", "the postfix non-null assertion is not implemented");
            unwrapNodeAs<DataLoad>(lhsNode)->setNonNull(true);
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
    : primaryData ('.' (IDENTIFIER | INTEGER | REAL))*
    ;
*/
Value Builder::visitAccessExpr(cst::AccessExpr context) {
    ENTER("AccessExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr()));
    for (size_t i = 1; i < context->childCount(); i += 2) {
        string strOp = context->child(i)->text();
        if (strOp != ".") {
            throw std::runtime_error("Invalid access operator: " + strOp);
        }
        lhsNode = makeMemberAccess(lhsNode, context->child(i), context->child(i + 1), context);
    }
    LEAVE("AccessExpr");
    return lhsNode;
}

/*
structData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct is immutable
    ;
*/
Value Builder::visitStructData(cst::StructData context) {
    ENTER("StructData");
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
    ;
*/
Value Builder::visitTupleData(cst::TupleData context) {
    ENTER("TupleData");
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

/// The text of a formatted string that is not an interpolation: `\${` is the literal `${` and
/// every other escape is left for decodeEscapes to decode (a brace has no escape: it is
/// literal, and is doubled for `format` afterwards).
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
            if (text[i + 1] == '`') {
                out += '`'; // `\`` is a literal backtick (it also does not close the string)
                ++i;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

/// The pattern text for `format`: the literal braces of a formatted string are doubled, so
/// `format` prints them.
std::string escapeFormatBraces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '{' || c == '}') {
            out += c;
        }
        out += c;
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
    const auto interpolations = context->tree()->tokens().interpolationsOf(token->index());

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
        syn::Tree sub           = syn::Parser::parseExpression(std::move(tokens));
        syn::reportLexicalErrors(sub.tokens(), *diags_);
        if (sub.tokens().errors().empty()) {
            syn::reportSyntaxErrors(sub, *diags_);
        }
        if (!sub.root() || sub.hasErrors()) {
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
        if (context->tok(TK::FString)) {
            // An expression builds a formatted string (visitFStringData); a pattern needs a
            // constant, and an interpolation is not one.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .at(rangeOf(context->firstToken()))
                .commit("`...`", "a formatted string is not a constant, so it cannot be a pattern");
        }
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
            if (pattern->get<cst::IdentRef>() && !pattern->get<cst::EnumTuplePattern>() &&
                !pattern->get<cst::EnumStructPattern>()) {
                binding = pattern->get<cst::IdentRef>()->text();
            } else {
                // Nested patterns are read so that the message is about the language rather
                // than about the grammar; what they would bind has not been decided.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .at(rangeOf(pattern->firstToken(), pattern->lastToken()))
                    .commit(
                        pattern->text(),
                        "a nested pattern is not supported; write a name or '_' for each field and "
                        "match inside the arm");
            }
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
    : interType ('?' | '!')*
    ;
*/
Value Builder::visitTypeExpr(cst::TypeExpr context) {
    ENTER("TypeExpr");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    // `T?` and `T!`, innermost mark first: `T?!` is a fallible nullable.
    for (size_t i = 1; i < context->childCount(); ++i) {
        node_ptr_t markedTypeNode =
            createNodeAs<NullableTypeLoad>(context->child(i)->text() == "!");
        setNodeTokenRangeByContext(markedTypeNode, context);
        *markedTypeNode << lhsNode;
        lhsNode = markedTypeNode;
    }
    LEAVE("TypeExpr");
    return lhsNode;
}

/*
interType
    : diffType ('&' diffType)*
    ;
*/
Value Builder::visitInterType(cst::InterType context) {
    ENTER("InterType");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    for (size_t i = 1; i < context->types().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitType(context->type(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::Inter);
        setNodeTokenRangeByContext(typeExprNode, context->type(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("InterType");
    return lhsNode;
}

/*
diffType
    : keyUnionDiffType ('\\' keyUnionDiffType)*
    ;
*/
Value Builder::visitDiffType(cst::DiffType context) {
    ENTER("DiffType");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    for (size_t i = 1; i < context->types().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitType(context->type(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::Diff);
        setNodeTokenRangeByContext(typeExprNode, context->type(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("DiffType");
    return lhsNode;
}

/*
keyUnionDiffType
    : keyInterType (('+' | '-') keyInterType)*
    ;
*/
Value Builder::visitKeyUnionDiffType(cst::KeyUnionDiffType context) {
    ENTER("KeyUnionDiffType");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    for (size_t i = 1; i < context->types().size(); ++i) {
        TypeOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "+") {
            op = TypeOp::KeyUnion;
        } else if (strOp == "-") {
            op = TypeOp::KeyDiff;
        } else {
            throw std::runtime_error("Invalid key union diff operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitType(context->type(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(op);
        setNodeTokenRangeByContext(typeExprNode, context->type(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("KeyUnionDiffType");
    return lhsNode;
}

/*
keyInterType
    : typeUnit ('^' typeUnit)*
    ;
*/
Value Builder::visitKeyInterType(cst::KeyInterType context) {
    ENTER("KeyInterType");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    for (size_t i = 1; i < context->types().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitType(context->type(i)));
        node_ptr_t typeExprNode = createNodeAs<TypeExprLoad>(TypeOp::KeyInter);
        setNodeTokenRangeByContext(typeExprNode, context->type(i));
        *typeExprNode << lhsNode << rhsNode;
        lhsNode = typeExprNode;
    }
    LEAVE("KeyInterType");
    return lhsNode;
}

/*
typeUnit : (identDef OF)? arrayType ;
*/
Value Builder::visitTypeUnit(cst::TypeUnit context) {
    ENTER("TypeUnit");
    node_ptr_t res = nullptr;
    if (context->get<cst::IdentDef>()) {
        // `name of T` used to end in an exception without a position.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit(
                context->get<cst::IdentDef>()->text() + " of ...",
                "the 'of' type form is not implemented");
        // The error above stops compilation; the type itself keeps the rest checked.
        res = any2node(visitType(context->type()));
    } else if (context->type()) {
        res = any2node(visitType(context->type()));
    }
    LEAVE("TypeUnit");
    return res;
}

/*
arrayType
    : specType ('[' ']')*
    ;
*/
Value Builder::visitArrayType(cst::ArrayType context) {
    ENTER("ArrayType");
    node_ptr_t res = any2node(visitType(context->type()));
    if (context->childCount() > 1) {
        size_t dims              = (context->childCount() - 1) / 2;
        node_ptr_t arrayTypeNode = createNodeAs<ArrayTypeLoad>(dims);
        setNodeTokenRangeByContext(arrayTypeNode, context);
        *arrayTypeNode << res;
        res = arrayTypeNode;
    }
    LEAVE("ArrayType");
    return res;
}

/*
typeOrData : typeExpr | CONST dataExpr ;
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

Value Builder::visitTypeOfType(cst::TypeOfType context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::TypeOf);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitExpr(context->expr()));
    return res;
}

Value Builder::visitTypeAsType(cst::TypeAsType context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::TypeAs);
    setNodeTokenRangeByContext(res, context);
    Reference ref(context->get<cst::IdentDef>()->text());
    *res << createNodeAs<RefTypeLoad>(ref);
    setNodeTokenRangeByContext(res, context->get<cst::IdentDef>());
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
        if (modifier->tokCount(TK::KwAtomic) > 0) {
            modSet.insert(Modifier::Atomic);
        }
        if (modifier->tokCount(TK::KwShared) > 0) {
            modSet.insert(Modifier::Shared);
        }
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
        if (modifier->tokCount(TK::KwMacro) > 0) {
            modSet.insert(Modifier::Macro);
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
    case NK::SpreadValue:
        return visitSpreadValue(cst::SpreadValue(context));
    case NK::WaitExpr:
        return visitWaitExpr(cst::WaitExpr(context));
    case NK::IfExpr:
    case NK::MatchExpr:
    case NK::TryExpr:
        return visitCtrlExpr(context);
    case NK::LoopExpr:
        return visitLoopExpr(cst::LoopExpr(context));
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
    case NK::CompExpr:
        return visitCompExpr(cst::CompExpr(context));
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
    case NK::InterType:
        return visitInterType(cst::InterType(context));
    case NK::DiffType:
        return visitDiffType(cst::DiffType(context));
    case NK::KeyUnionDiffType:
        return visitKeyUnionDiffType(cst::KeyUnionDiffType(context));
    case NK::KeyInterType:
        return visitKeyInterType(cst::KeyInterType(context));
    case NK::TypeUnit:
        return visitTypeUnit(cst::TypeUnit(context));
    case NK::ArrayType:
        return visitArrayType(cst::ArrayType(context));
    case NK::SpecType:
        return visitSpecType(cst::SpecType(context));
    case NK::ConstData:
        return visitConstData(cst::ConstData(context));
    case NK::StructType:
        return visitStructType(cst::StructType(context));
    case NK::RefType:
        return visitRefType(cst::RefType(context));
    case NK::ParenType:
        return visitType(context.type());
    case NK::TupleType:
        return visitTupleType(cst::TupleType(context));
    case NK::FuncType:
        return visitFuncType(cst::FuncType(context));
    case NK::TypeOfType:
        return visitTypeOfType(cst::TypeOfType(context));
    case NK::TypeAsType:
        return visitTypeAsType(cst::TypeAsType(context));
    case NK::EnumType:
        return visitEnumType(cst::EnumType(context));
    default:
        throw std::runtime_error(
            "Builder: not a type: " + std::string(syn::nodeKindName(context.kind())));
    }
}
