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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Builds the syntactic index of a file from its parse tree (see syntax_index.h).
 *
 * The tree may be one that ANTLR recovered from a syntax error: any child may be missing and
 * a context may have no stop token. Every access below goes through helpers that accept
 * that, and a construct that is incomplete contributes what it has.
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/service/semantic/syntax_index.h"

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"

#include <algorithm>

namespace camel::service::semantic {

const char *to_string(DeclKind kind) {
    switch (kind) {
    case DeclKind::Function:
        return "function";
    case DeclKind::Parameter:
        return "parameter";
    case DeclKind::WithParameter:
        return "with parameter";
    case DeclKind::Local:
        return "let";
    case DeclKind::Variable:
        return "var";
    case DeclKind::Type:
        return "type";
    case DeclKind::TypeParameter:
        return "type parameter";
    case DeclKind::TypeVariable:
        return "type variable";
    case DeclKind::Import:
        return "import";
    case DeclKind::Module:
        return "module";
    case DeclKind::Namespace:
        return "namespace";
    case DeclKind::Alias:
        return "alias";
    case DeclKind::Binding:
        return "binding";
    }
    return "";
}

namespace {

using antlr4::ParserRuleContext;
using antlr4::Token;
using antlr4::tree::ParseTree;
using antlr4::tree::TerminalNode;
using P = OpenCMLParser;

bool isTypeKind(DeclKind kind) {
    return kind == DeclKind::Type || kind == DeclKind::TypeParameter ||
           kind == DeclKind::TypeVariable;
}

// A declaration a type name may resolve to / a value name may resolve to. An imported name
// can be either, a module or namespace starts a path of either kind.
bool admits(DeclKind kind, bool types) {
    switch (kind) {
    case DeclKind::Import:
    case DeclKind::Module:
    case DeclKind::Namespace:
    case DeclKind::Alias:
        return true;
    default:
        return isTypeKind(kind) == types;
    }
}

class Walker {
  public:
    Walker(SyntaxIndex &index, const std::vector<Token *> &tokens, std::string_view text)
        : index_(index), tokens_(tokens), text_(text) {
        // ANTLR indexes the text by code point; ranges are byte offsets.
        bool ascii = true;
        for (unsigned char c : text) {
            if (c >= 0x80) {
                ascii = false;
                break;
            }
        }
        if (!ascii) {
            for (size_t i = 0; i < text.size(); ++i) {
                if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
                    cpToByte_.push_back(static_cast<uint32_t>(i));
                }
            }
            cpToByte_.push_back(static_cast<uint32_t>(text.size()));
        }
    }

    void run(ParseTree *tree) {
        collectTokens();
        Scope module;
        module.id    = 0;
        module.kind  = ScopeKind::Module;
        module.range = Range{0, static_cast<uint32_t>(text_.size())};
        index_.scopes.push_back(module);
        scope_ = 0;
        if (tree != nullptr) {
            walk(tree);
        }
        // Source order, whatever order the walk visited the parts of a construct in.
        std::stable_sort(index_.refs.begin(), index_.refs.end(), [](const Ref &a, const Ref &b) {
            return a.range.start < b.range.start;
        });
        std::stable_sort(
            index_.members.begin(),
            index_.members.end(),
            [](const Member &a, const Member &b) { return a.nameRange.start < b.nameRange.start; });
        resolve();
    }

  private:
    SyntaxIndex &index_;
    const std::vector<Token *> &tokens_;
    std::string_view text_;
    std::vector<uint32_t> cpToByte_;
    uint32_t scope_     = 0;
    int32_t container_  = -1;
    bool inFuncType_    = false;
    bool exportNext_    = false;
    // The function whose signature is being walked: `typeas` marks it generic.
    int32_t signatureOf_ = -1;

    // ---- positions ----

    uint32_t byteOf(size_t codePoint) const {
        if (cpToByte_.empty()) {
            return static_cast<uint32_t>(std::min(codePoint, text_.size()));
        }
        return cpToByte_[std::min(codePoint, cpToByte_.size() - 1)];
    }
    Range rangeOf(const Token *token) const {
        if (token == nullptr || token->getStartIndex() == INVALID_INDEX) {
            return {};
        }
        const uint32_t start = byteOf(token->getStartIndex());
        const uint32_t end =
            token->getStopIndex() == INVALID_INDEX || token->getStopIndex() < token->getStartIndex()
                ? start
                : byteOf(token->getStopIndex() + 1);
        return {start, end};
    }
    Range rangeOf(TerminalNode *node) const {
        return node ? rangeOf(node->getSymbol()) : Range{};
    }
    Range rangeOf(ParserRuleContext *ctx) const {
        if (ctx == nullptr || ctx->getStart() == nullptr) {
            return {};
        }
        Range r = rangeOf(ctx->getStart());
        if (ctx->getStop() != nullptr) {
            Range stop = rangeOf(ctx->getStop());
            if (stop.end > r.end) {
                r.end = stop.end;
            }
        }
        return r;
    }
    std::string textOf(Range r) const {
        if (r.end > text_.size() || r.start >= r.end) {
            return {};
        }
        return std::string(text_.substr(r.start, r.end - r.start));
    }
    // The text of a range on one line: runs of white space become one space.
    std::string flatTextOf(Range r) const {
        std::string out;
        bool space = false;
        for (char c : textOf(r)) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                space = !out.empty();
                continue;
            }
            if (space) {
                out += ' ';
                space = false;
            }
            out += c;
        }
        return out;
    }
    static bool valid(TerminalNode *node) {
        // An identifier that error recovery conjured has no text position.
        return node != nullptr && node->getSymbol() != nullptr &&
               node->getSymbol()->getStartIndex() != INVALID_INDEX &&
               node->getSymbol()->getTokenIndex() != INVALID_INDEX &&
               dynamic_cast<antlr4::tree::ErrorNode *>(node) == nullptr;
    }

    // ---- tokens ----

    void collectTokens() {
        index_.tokens.reserve(tokens_.size());
        for (const Token *token : tokens_) {
            if (token == nullptr || token->getType() == Token::EOF) {
                continue;
            }
            TokenInfo info;
            info.range   = rangeOf(token);
            info.type    = static_cast<uint16_t>(token->getType());
            info.channel = static_cast<uint8_t>(token->getChannel());
            info.line    = static_cast<uint32_t>(token->getLine() > 0 ? token->getLine() - 1 : 0);
            index_.tokens.push_back(info);
        }
    }

    // The comment lines directly above the line of `first`: `//` lines without a blank line
    // between them and the declaration, or one block comment ending on the line above.
    std::string docAbove(const Token *first) const {
        if (first == nullptr || first->getTokenIndex() == INVALID_INDEX) {
            return {};
        }
        std::vector<std::string> lines;
        size_t expectLine = first->getLine();
        for (size_t i = first->getTokenIndex(); i-- > 0;) {
            const Token *token = tokens_[i];
            if (token->getChannel() == 3) {
                if (token->getLine() + 1 != expectLine) {
                    break;
                }
                std::string line = token->getText();
                size_t from      = line.find_first_not_of('/');
                line             = from == std::string::npos ? "" : line.substr(from);
                if (!line.empty() && line.front() == ' ') {
                    line.erase(0, 1);
                }
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                    line.pop_back();
                }
                lines.push_back(line);
                expectLine = token->getLine();
                continue;
            }
            if (token->getChannel() == 4 && lines.empty()) {
                std::string body = token->getText();
                const size_t newlines =
                    static_cast<size_t>(std::count(body.begin(), body.end(), '\n'));
                if (token->getLine() + newlines + 1 != expectLine) {
                    break;
                }
                if (body.size() >= 4) {
                    body = body.substr(2, body.size() - 4);
                }
                // Drop the leading ` * ` of each line.
                std::string cleaned;
                size_t pos = 0;
                while (pos <= body.size()) {
                    size_t eol       = body.find('\n', pos);
                    std::string line = body.substr(pos, eol == std::string::npos ? eol : eol - pos);
                    size_t from      = line.find_first_not_of(" \t*");
                    line             = from == std::string::npos ? "" : line.substr(from);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (!cleaned.empty() || !line.empty()) {
                        cleaned += line;
                        cleaned += '\n';
                    }
                    if (eol == std::string::npos) {
                        break;
                    }
                    pos = eol + 1;
                }
                while (!cleaned.empty() && cleaned.back() == '\n') {
                    cleaned.pop_back();
                }
                return cleaned;
            }
            break;
        }
        std::string doc;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (!doc.empty()) {
                doc += '\n';
            }
            doc += *it;
        }
        return doc;
    }

    // ---- scopes and declarations ----

    uint32_t pushScope(ScopeKind kind, Range range, int32_t owner = -1) {
        Scope scope;
        scope.id     = static_cast<uint32_t>(index_.scopes.size());
        scope.parent = static_cast<int32_t>(scope_);
        scope.kind   = kind;
        scope.range  = range;
        scope.owner  = owner;
        index_.scopes.push_back(scope);
        scope_ = scope.id;
        return scope.id;
    }
    void popScope() {
        const int32_t parent = index_.scopes[scope_].parent;
        scope_               = parent < 0 ? 0 : static_cast<uint32_t>(parent);
    }

    Decl &declare(DeclKind kind, TerminalNode *name, Range owner, Range full) {
        Decl decl;
        decl.id         = static_cast<uint32_t>(index_.decls.size());
        decl.kind       = kind;
        decl.name       = name->getText();
        decl.nameRange  = rangeOf(name);
        decl.ownerRange = owner;
        decl.fullRange  = full.empty() ? decl.nameRange : full;
        decl.scope      = scope_;
        decl.container  = container_;
        index_.scopes[scope_].decls.push_back(decl.id);
        index_.decls.push_back(std::move(decl));
        return index_.decls.back();
    }

    void addRef(P::IdentRefContext *ctx, RefKind kind, bool callee = false) {
        if (ctx == nullptr) {
            return;
        }
        Ref ref;
        ref.kind   = kind;
        ref.scope  = scope_;
        ref.callee = callee;
        for (TerminalNode *id : ctx->IDENTIFIER()) {
            if (valid(id)) {
                ref.path.push_back({id->getText(), rangeOf(id)});
            }
        }
        if (ref.path.empty()) {
            return;
        }
        ref.range = Range{ref.path.front().range.start, ref.path.back().range.end};
        index_.refs.push_back(std::move(ref));
    }

    // ---- the walk ----

    void walkChildren(ParseTree *tree) {
        for (ParseTree *child : tree->children) {
            walk(child);
        }
    }

    void walk(ParseTree *tree) {
        if (tree == nullptr) {
            return;
        }
        auto *ctx = dynamic_cast<ParserRuleContext *>(tree);
        if (ctx == nullptr) {
            return;
        }
        switch (ctx->getRuleIndex()) {
        case P::RuleModuleDecl:
            return moduleDecl(static_cast<P::ModuleDeclContext *>(ctx));
        case P::RuleImportDecl:
            return importDecl(static_cast<P::ImportDeclContext *>(ctx));
        case P::RuleExportDecl:
            return exportDecl(static_cast<P::ExportDeclContext *>(ctx));
        case P::RuleUsingNamespaceDecl:
            return addRef(
                static_cast<P::UsingNamespaceDeclContext *>(ctx)->identRef(),
                RefKind::Namespace);
        case P::RuleStmtBlock:
            return stmtBlock(static_cast<P::StmtBlockContext *>(ctx), ScopeKind::Block);
        case P::RuleFuncData:
            return funcData(static_cast<P::FuncDataContext *>(ctx));
        case P::RuleFuncAnno: {
            auto *anno = static_cast<P::FuncAnnoContext *>(ctx);
            addRef(anno->identRef(), RefKind::Value, true);
            return walk(anno->angledValues());
        }
        case P::RuleFuncDecl:
            return funcDecl(static_cast<P::FuncDeclContext *>(ctx));
        case P::RuleNamespaceDecl:
            return namespaceDecl(static_cast<P::NamespaceDeclContext *>(ctx));
        case P::RuleDataDecl:
            return dataDecl(static_cast<P::DataDeclContext *>(ctx));
        case P::RuleTypeDecl:
            return typeDecl(static_cast<P::TypeDeclContext *>(ctx));
        case P::RuleUseDecl:
            return useDecl(static_cast<P::UseDeclContext *>(ctx));
        case P::RuleKeyTypePair: {
            auto *pair = static_cast<P::KeyTypePairContext *>(ctx);
            addKey(pair->identDef(), true);
            return walk(pair->typeExpr());
        }
        case P::RuleKeyValuePair: {
            auto *pair = static_cast<P::KeyValuePairContext *>(ctx);
            addKey(pair->identDef(), false);
            return walk(pair->dataExpr());
        }
        case P::RuleKeyParamPair: {
            // Reached only inside a function *type*: the parameters of a declaration are
            // handled by the declaration.
            auto *pair = static_cast<P::KeyParamPairContext *>(ctx);
            addKey(pair->identDef(), true);
            walk(pair->typeExpr());
            return walk(pair->dataExpr());
        }
        case P::RuleMatchCase:
            return matchCase(static_cast<P::MatchCaseContext *>(ctx));
        case P::RuleCatchClause:
            return catchClause(static_cast<P::CatchClauseContext *>(ctx));
        case P::RuleCtrlExpr:
            return ctrlExpr(static_cast<P::CtrlExprContext *>(ctx));
        case P::RuleAnnoExpr:
            return annoExpr(static_cast<P::AnnoExprContext *>(ctx));
        case P::RuleAccessExpr:
            return accessExpr(static_cast<P::AccessExprContext *>(ctx), false);
        case P::RuleArrayData:
            return arrayData(static_cast<P::ArrayDataContext *>(ctx));
        case P::RulePrimaryData: {
            auto *primary = static_cast<P::PrimaryDataContext *>(ctx);
            if (primary->identRef()) {
                return addRef(primary->identRef(), RefKind::Value);
            }
            return walkChildren(ctx);
        }
        case P::RulePrimaryType:
            return primaryType(static_cast<P::PrimaryTypeContext *>(ctx));
        case P::RuleFuncType: {
            const bool saved = std::exchange(inFuncType_, true);
            walkChildren(ctx);
            inFuncType_ = saved;
            return;
        }
        default:
            return walkChildren(ctx);
        }
    }

    void addKey(P::IdentDefContext *ident, bool inType) {
        if (ident && valid(ident->IDENTIFIER())) {
            index_.keys.push_back(
                {ident->IDENTIFIER()->getText(), rangeOf(ident->IDENTIFIER()), inType});
        }
    }

    void moduleDecl(P::ModuleDeclContext *ctx) {
        if (ctx->identDef() && valid(ctx->identDef()->IDENTIFIER())) {
            index_.moduleName      = ctx->identDef()->IDENTIFIER()->getText();
            index_.moduleNameRange = rangeOf(ctx->identDef()->IDENTIFIER());
        }
    }

    void importDecl(P::ImportDeclContext *ctx) {
        auto *moduleName = ctx->moduleName();
        const std::string from = moduleName ? textOf(rangeOf(moduleName)) : std::string();
        const Range whole      = rangeOf(ctx);
        if (ctx->FROM() == nullptr && ctx->identDef() == nullptr && ctx->bracedIdents() == nullptr) {
            // `import a.b`: the namespace `b`.
            if (moduleName && !moduleName->IDENTIFIER().empty() &&
                valid(moduleName->IDENTIFIER().back())) {
                Decl &decl        = declare(DeclKind::Module, moduleName->IDENTIFIER().back(), whole, whole);
                decl.importedFrom = from;
            }
            return;
        }
        std::vector<P::IdentDefContext *> names;
        if (ctx->identDef()) {
            names.push_back(ctx->identDef());
        }
        if (ctx->bracedIdents() && ctx->bracedIdents()->identList()) {
            for (auto *ident : ctx->bracedIdents()->identList()->identDef()) {
                names.push_back(ident);
            }
        }
        for (auto *ident : names) {
            if (ident && valid(ident->IDENTIFIER())) {
                Decl &decl        = declare(DeclKind::Import, ident->IDENTIFIER(), whole, whole);
                decl.importedFrom = from;
            }
        }
    }

    void exportDecl(P::ExportDeclContext *ctx) {
        if (ctx->bracedIdents() && ctx->bracedIdents()->identList()) {
            // `export { a, b }`: references to what is exported.
            for (auto *ident : ctx->bracedIdents()->identList()->identDef()) {
                if (ident && valid(ident->IDENTIFIER())) {
                    Ref ref;
                    ref.kind  = RefKind::Value;
                    ref.scope = scope_;
                    ref.path.push_back({ident->IDENTIFIER()->getText(), rangeOf(ident->IDENTIFIER())});
                    ref.range = ref.path.back().range;
                    index_.refs.push_back(std::move(ref));
                    exportedNames_.push_back(ident->IDENTIFIER()->getText());
                }
            }
            return;
        }
        exportNext_ = true;
        walk(ctx->dataDecl());
        walk(ctx->typeDecl());
        exportNext_ = false;
    }

    void stmtBlock(P::StmtBlockContext *ctx, ScopeKind kind) {
        pushScope(kind, rangeOf(ctx));
        walk(ctx->stmtList());
        popScope();
    }

    // Declares the parameters of `params` in the current scope.
    void declareParams(
        P::PairedParamsContext *params, DeclKind kind, Range owner, bool lambda) {
        if (params == nullptr) {
            return;
        }
        for (auto *pair : params->keyParamPair()) {
            if (pair == nullptr) {
                continue;
            }
            walk(pair->typeExpr());
            walk(pair->dataExpr());
            if (pair->identDef() && valid(pair->identDef()->IDENTIFIER())) {
                Decl &decl      = declare(kind, pair->identDef()->IDENTIFIER(), owner, rangeOf(pair));
                decl.varParam   = pair->VAR() != nullptr;
                decl.lambda     = lambda;
                decl.annotation = flatTextOf(rangeOf(pair->typeExpr()));
                decl.header     = flatTextOf(rangeOf(pair));
            }
        }
    }

    void funcDecl(P::FuncDeclContext *ctx) {
        for (auto *anno : ctx->funcAnno()) {
            walk(anno);
        }
        const Range whole = rangeOf(ctx);
        int32_t self      = -1;
        TerminalNode *name =
            ctx->identRef() && !ctx->identRef()->IDENTIFIER().empty()
                ? ctx->identRef()->IDENTIFIER().back()
                : nullptr;
        if (valid(name)) {
            Decl &decl    = declare(DeclKind::Function, name, whole, whole);
            decl.exported = ctx->EXPORT() != nullptr;
            if (ctx->modifiers()) {
                decl.sync  = !ctx->modifiers()->SYNC().empty();
                decl.macro = !ctx->modifiers()->MACRO().empty();
            }
            // The header: from the first token after the decorators to the body.
            Range header = whole;
            if (!ctx->funcAnno().empty()) {
                header.start = rangeOf(ctx->funcAnno().back()).end;
            }
            if (ctx->stmtBlock() && rangeOf(ctx->stmtBlock()).start > header.start) {
                header.end = rangeOf(ctx->stmtBlock()).start;
            }
            decl.header     = flatTextOf(header);
            decl.annotation = flatTextOf(rangeOf(ctx->typeExpr()));
            decl.doc        = docAbove(ctx->getStart());
            self            = static_cast<int32_t>(decl.id);
        }
        const int32_t savedContainer = container_;
        const int32_t savedSignature = signatureOf_;
        if (self >= 0) {
            container_ = self;
        }
        signatureOf_           = self;
        const uint32_t scopeId = pushScope(ScopeKind::Function, whole, self);
        if (self >= 0) {
            index_.decls[self].bodyScope = static_cast<int32_t>(scopeId);
        }
        if (ctx->angledParams()) {
            declareParams(ctx->angledParams()->pairedParams(), DeclKind::WithParameter, whole, false);
        }
        if (ctx->parentParams()) {
            declareParams(ctx->parentParams()->pairedParams(), DeclKind::Parameter, whole, false);
        }
        walk(ctx->typeExpr());
        signatureOf_ = savedSignature;
        // The body shares the scope of the parameters.
        if (ctx->stmtBlock()) {
            walk(ctx->stmtBlock()->stmtList());
        }
        popScope();
        container_ = savedContainer;
    }

    void funcData(P::FuncDataContext *ctx) {
        const Range whole = rangeOf(ctx);
        pushScope(ScopeKind::Lambda, whole);
        const int32_t savedSignature = std::exchange(signatureOf_, -1);
        if (ctx->angledParams()) {
            declareParams(ctx->angledParams()->pairedParams(), DeclKind::WithParameter, whole, true);
        }
        if (ctx->parentParams()) {
            declareParams(ctx->parentParams()->pairedParams(), DeclKind::Parameter, whole, true);
        }
        walk(ctx->typeExpr());
        signatureOf_ = savedSignature;
        if (auto *body = ctx->blockExpr()) {
            if (body->stmtBlock()) {
                walk(body->stmtBlock()->stmtList());
            } else {
                walk(body->dataExpr());
            }
        }
        popScope();
    }

    void namespaceDecl(P::NamespaceDeclContext *ctx) {
        const Range whole = rangeOf(ctx);
        int32_t self      = -1;
        TerminalNode *name =
            ctx->identRef() && !ctx->identRef()->IDENTIFIER().empty()
                ? ctx->identRef()->IDENTIFIER().back()
                : nullptr;
        if (valid(name)) {
            self = static_cast<int32_t>(declare(DeclKind::Namespace, name, whole, whole).id);
        }
        const int32_t savedContainer = container_;
        if (self >= 0) {
            container_ = self;
        }
        const uint32_t scopeId = pushScope(ScopeKind::Namespace, whole, self);
        if (self >= 0) {
            index_.decls[self].bodyScope = static_cast<int32_t>(scopeId);
        }
        for (auto *item : ctx->namespaceItem()) {
            walk(item);
        }
        popScope();
        container_ = savedContainer;
    }

    void dataDecl(P::DataDeclContext *ctx) {
        const bool exported = std::exchange(exportNext_, false);
        const Range whole   = rangeOf(ctx);
        // The initializer does not see the names being declared.
        walk(ctx->typeList());
        walk(ctx->dataList());
        std::vector<P::IdentDefContext *> names;
        if (auto *carrier = ctx->carrier()) {
            P::IdentListContext *list = carrier->identList();
            if (!list && carrier->parentIdents()) {
                list = carrier->parentIdents()->identList();
            }
            if (!list && carrier->bracedIdents()) {
                list = carrier->bracedIdents()->identList();
            }
            if (!list && carrier->bracketIdents()) {
                list = carrier->bracketIdents()->identList();
            }
            if (list) {
                names = list->identDef();
            }
        }
        const bool plain = ctx->carrier() && ctx->carrier()->identList() && names.size() == 1;
        for (auto *ident : names) {
            if (!ident || !valid(ident->IDENTIFIER())) {
                continue;
            }
            Decl &decl = declare(
                ctx->VAR() ? DeclKind::Variable : DeclKind::Local,
                ident->IDENTIFIER(),
                whole,
                whole);
            decl.exported    = exported;
            decl.visibleFrom = whole.end;
            decl.doc         = docAbove(ctx->getStart());
            if (plain && ctx->typeList()) {
                decl.annotation = flatTextOf(rangeOf(ctx->typeList()));
            }
        }
    }

    void typeDecl(P::TypeDeclContext *ctx) {
        const bool exported = std::exchange(exportNext_, false);
        const Range whole   = rangeOf(ctx);
        TerminalNode *name =
            ctx->identRef() && !ctx->identRef()->IDENTIFIER().empty()
                ? ctx->identRef()->IDENTIFIER().back()
                : nullptr;
        if (valid(name)) {
            Decl &decl    = declare(DeclKind::Type, name, whole, whole);
            decl.exported = exported;
            decl.header   = flatTextOf(whole);
            decl.doc      = docAbove(ctx->getStart());
        }
        pushScope(ScopeKind::Type, whole);
        for (auto *ident : ctx->identDef()) {
            if (ident && valid(ident->IDENTIFIER())) {
                declare(DeclKind::TypeParameter, ident->IDENTIFIER(), whole, rangeOf(ident));
            }
        }
        walk(ctx->typeExpr());
        popScope();
    }

    void useDecl(P::UseDeclContext *ctx) {
        addRef(ctx->identRef(), RefKind::Namespace);
        const Range whole = rangeOf(ctx);
        if (ctx->identDef() && valid(ctx->identDef()->IDENTIFIER())) {
            declare(DeclKind::Alias, ctx->identDef()->IDENTIFIER(), whole, whole);
        }
    }

    void matchCase(P::MatchCaseContext *ctx) {
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        for (auto *pattern : ctx->pattern()) {
            if (pattern == nullptr) {
                continue;
            }
            const Range whole = rangeOf(pattern);
            if (pattern->identRef()) {
                addRef(pattern->identRef(), RefKind::Value);
                if (pattern->identDef() && valid(pattern->identDef()->IDENTIFIER())) {
                    declare(DeclKind::Binding, pattern->identDef()->IDENTIFIER(), whole, whole);
                }
            } else if (pattern->identList()) {
                for (auto *ident : pattern->identList()->identDef()) {
                    if (ident && valid(ident->IDENTIFIER())) {
                        declare(DeclKind::Binding, ident->IDENTIFIER(), whole, whole);
                    }
                }
            } else {
                walkChildren(pattern);
            }
        }
        if (auto *body = ctx->blockExpr()) {
            if (body->stmtBlock()) {
                walk(body->stmtBlock()->stmtList());
            } else {
                walk(body->dataExpr());
            }
        }
        popScope();
    }

    void catchClause(P::CatchClauseContext *ctx) {
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        walk(ctx->typeExpr());
        if (ctx->identDef() && valid(ctx->identDef()->IDENTIFIER())) {
            declare(DeclKind::Binding, ctx->identDef()->IDENTIFIER(), rangeOf(ctx), rangeOf(ctx));
        }
        if (ctx->stmtBlock()) {
            walk(ctx->stmtBlock()->stmtList());
        }
        popScope();
    }

    void ctrlExpr(P::CtrlExprContext *ctx) {
        if (ctx->MATCH()) {
            addRef(ctx->identRef(), RefKind::Value);
            for (auto *matchCase : ctx->matchCase()) {
                walk(matchCase);
            }
            return;
        }
        if (ctx->IF()) {
            walk(ctx->logicalOrExpr());
            // Each arm is a graph of its own, and so a scope of its own.
            for (auto *arm : ctx->blockExpr()) {
                if (arm == nullptr) {
                    continue;
                }
                pushScope(ScopeKind::Arm, rangeOf(arm));
                if (arm->stmtBlock()) {
                    walk(arm->stmtBlock()->stmtList());
                } else {
                    walk(arm->dataExpr());
                }
                popScope();
            }
            return;
        }
        walkChildren(ctx);
    }

    void accessExpr(P::AccessExprContext *ctx, bool callee) {
        auto *primary = ctx->primaryData();
        if (primary && primary->identRef()) {
            // `f(x)`: a callee only when nothing stands between the name and the arguments.
            addRef(primary->identRef(), RefKind::Value, callee && ctx->DOT().empty());
        } else {
            walk(primary);
        }
        // The members after the dots, in order; the receiver of each is all before its dot.
        const Range whole = rangeOf(ctx);
        for (size_t i = 0; i < ctx->children.size(); ++i) {
            auto *node = dynamic_cast<TerminalNode *>(ctx->children[i]);
            if (!valid(node) || i == 0) {
                continue;
            }
            const size_t type = node->getSymbol()->getType();
            if (type != OpenCMLLexer::IDENTIFIER && type != OpenCMLLexer::INTEGER) {
                continue;
            }
            auto *dot = dynamic_cast<TerminalNode *>(ctx->children[i - 1]);
            if (dot == nullptr) {
                continue;
            }
            Member member;
            member.name      = node->getText();
            member.nameRange = rangeOf(node);
            member.receiver  = Range{whole.start, rangeOf(dot).start};
            member.numeric   = type == OpenCMLLexer::INTEGER;
            index_.members.push_back(std::move(member));
        }
    }

    void annoExpr(P::AnnoExprContext *ctx) {
        // Children: accessExpr, then indices / arguments / with-arguments / `!` / members.
        bool callee = false;
        if (ctx->children.size() >= 2) {
            auto *next = dynamic_cast<ParserRuleContext *>(ctx->children[1]);
            callee     = next != nullptr && (next->getRuleIndex() == P::RuleParentArgues ||
                                         next->getRuleIndex() == P::RuleAngledValues);
        }
        const Range whole = rangeOf(ctx);
        for (ParseTree *child : ctx->children) {
            auto *sub = dynamic_cast<ParserRuleContext *>(child);
            if (sub == nullptr) {
                continue;
            }
            if (sub->getRuleIndex() == P::RuleAccessExpr) {
                accessExpr(static_cast<P::AccessExprContext *>(sub), callee);
            } else if (sub->getRuleIndex() == P::RuleMemberAccess) {
                auto *access       = static_cast<P::MemberAccessContext *>(sub);
                TerminalNode *name = access->IDENTIFIER() ? access->IDENTIFIER() : access->INTEGER();
                if (valid(name) && access->DOT()) {
                    Member member;
                    member.name      = name->getText();
                    member.nameRange = rangeOf(name);
                    member.receiver  = Range{whole.start, rangeOf(access->DOT()).start};
                    member.numeric   = access->INTEGER() != nullptr;
                    index_.members.push_back(std::move(member));
                }
            } else {
                walk(sub);
            }
        }
    }

    void arrayData(P::ArrayDataContext *ctx) {
        if (ctx->FOR() == nullptr) {
            return walkChildren(ctx);
        }
        // `[e for x in xs if c]`: x is bound in e and c, not in xs.
        const auto exprs = ctx->dataExpr();
        if (exprs.size() >= 2) {
            walk(exprs[1]);
        }
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        if (ctx->identRef() && !ctx->identRef()->IDENTIFIER().empty() &&
            valid(ctx->identRef()->IDENTIFIER().back())) {
            declare(
                DeclKind::Binding,
                ctx->identRef()->IDENTIFIER().back(),
                rangeOf(ctx),
                rangeOf(ctx->identRef()));
        }
        if (!exprs.empty()) {
            walk(exprs[0]);
        }
        if (exprs.size() >= 3) {
            walk(exprs[2]);
        }
        popScope();
    }

    void primaryType(P::PrimaryTypeContext *ctx) {
        if (ctx->identRef()) {
            return addRef(ctx->identRef(), RefKind::Type);
        }
        if (ctx->TYPEAS()) {
            // `typeas T` binds T for the whole function whose signature it stands in.
            if (ctx->identDef() && valid(ctx->identDef()->IDENTIFIER())) {
                const Range whole = rangeOf(ctx);
                declare(DeclKind::TypeVariable, ctx->identDef()->IDENTIFIER(), whole, whole);
                if (signatureOf_ >= 0) {
                    index_.decls[signatureOf_].generic = true;
                }
            }
            return;
        }
        walkChildren(ctx);
    }

    // ---- resolution ----

    std::vector<std::string> exportedNames_;

    void resolve() {
        for (const std::string &name : exportedNames_) {
            for (uint32_t id : index_.scopes[0].decls) {
                if (index_.decls[id].name == name) {
                    index_.decls[id].exported = true;
                }
            }
        }
        for (Ref &ref : index_.refs) {
            const bool types = ref.kind == RefKind::Type;
            const auto found = index_.lookup(
                ref.scope,
                ref.path.front().name,
                ref.range.start,
                ref.path.size() == 1 ? types : false);
            if (found.empty()) {
                continue;
            }
            ref.decl = static_cast<int32_t>(found.front());
            // A path through namespaces declared in this file.
            int32_t current = ref.decl;
            for (size_t i = 1; i < ref.path.size() && current >= 0; ++i) {
                const Decl &decl = index_.decls[current];
                int32_t next     = -1;
                if (decl.kind == DeclKind::Namespace && decl.bodyScope >= 0) {
                    for (uint32_t id : index_.scopes[decl.bodyScope].decls) {
                        if (index_.decls[id].name == ref.path[i].name) {
                            next = static_cast<int32_t>(id);
                            break;
                        }
                    }
                }
                current = next;
                if (i + 1 == ref.path.size()) {
                    ref.target = current;
                }
            }
        }
    }
};

} // namespace

uint32_t SyntaxIndex::scopeAt(uint32_t offset) const {
    // Scopes are created in pre-order: the last one that contains the offset is the innermost.
    uint32_t best = 0;
    for (const Scope &scope : scopes) {
        if (scope.range.start <= offset && offset <= scope.range.end) {
            best = scope.id;
        }
    }
    return best;
}

std::vector<uint32_t> SyntaxIndex::lookup(
    uint32_t scope, std::string_view name, uint32_t offset, bool types) const {
    std::vector<uint32_t> found;
    for (int32_t s = static_cast<int32_t>(scope); s >= 0; s = scopes[s].parent) {
        for (uint32_t id : scopes[s].decls) {
            const Decl &decl = decls[id];
            if (decl.name != name || !admits(decl.kind, types)) {
                continue;
            }
            if (decl.visibleFrom != 0 && decl.visibleFrom > offset) {
                continue;
            }
            found.push_back(id);
        }
        if (!found.empty()) {
            break;
        }
    }
    return found;
}

std::vector<uint32_t> SyntaxIndex::visibleAt(uint32_t offset) const {
    std::vector<uint32_t> out;
    std::vector<std::string_view> seen;
    for (int32_t s = static_cast<int32_t>(scopeAt(offset)); s >= 0; s = scopes[s].parent) {
        std::vector<std::string_view> here;
        for (uint32_t id : scopes[s].decls) {
            const Decl &decl = decls[id];
            if (decl.visibleFrom != 0 && decl.visibleFrom > offset) {
                continue;
            }
            if (std::find(seen.begin(), seen.end(), std::string_view(decl.name)) != seen.end()) {
                continue;
            }
            out.push_back(id);
            here.push_back(decl.name);
        }
        seen.insert(seen.end(), here.begin(), here.end());
    }
    return out;
}

const Decl *SyntaxIndex::declAtName(uint32_t offset) const {
    for (const Decl &decl : decls) {
        if (decl.nameRange.touches(offset)) {
            return &decl;
        }
    }
    return nullptr;
}

const Ref *SyntaxIndex::refAt(uint32_t offset) const {
    for (const Ref &ref : refs) {
        if (ref.range.touches(offset)) {
            return &ref;
        }
    }
    return nullptr;
}

const Member *SyntaxIndex::memberAt(uint32_t offset) const {
    for (const Member &member : members) {
        if (member.nameRange.touches(offset)) {
            return &member;
        }
    }
    return nullptr;
}

int32_t SyntaxIndex::tokenAt(uint32_t offset) const {
    int32_t found = -1;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Range &r = tokens[i].range;
        if (r.start <= offset && offset < r.end) {
            return static_cast<int32_t>(i);
        }
        if (r.end == offset) {
            found = static_cast<int32_t>(i);
        }
        if (r.start > offset) {
            break;
        }
    }
    return found;
}

SyntaxIndex buildSyntaxIndex(
    antlr4::tree::ParseTree *tree, const std::vector<antlr4::Token *> &tokens,
    std::string_view text, bool complete) {
    SyntaxIndex index;
    index.complete = complete;
    Walker(index, tokens, text).run(tree);
    return index;
}

} // namespace camel::service::semantic
