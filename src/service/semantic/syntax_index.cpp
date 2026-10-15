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
 * The tree may be that of a text with syntax errors: any child may be missing and a node may
 * cover no token. Every access below goes through helpers that accept
 * that, and a construct that is incomplete contributes what it has.
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/service/semantic/syntax_index.h"

#include "camel/parse/syntax/cst.h"

#include <algorithm>
#include <utility>

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

namespace syn = camel::parse::syntax;
namespace cst = camel::parse::syntax::cst;
using TK      = camel::parse::syntax::TokenKind;
using NK      = camel::parse::syntax::NodeKind;

bool isTypeKind(DeclKind kind) {
    return kind == DeclKind::Type || kind == DeclKind::TypeParameter ||
           kind == DeclKind::TypeVariable;
}

/// Whether a declaration of `kind` can be what a name in a type position (or a value
/// position) refers to. Imports and modules can be either.
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

/// Walks the concrete syntax tree of one file. The tree may be that of a text with errors:
/// a node may lack any of its children, and Error nodes hold what the parser skipped. Every
/// access therefore goes through handles that may be empty.
class Walker {
  public:
    Walker(SyntaxIndex &index, const syn::Tree &tree)
        : index_(index), tree_(tree), buffer_(tree.tokens()), text_(tree.source()) {}

    void run() {
        collectTokens();
        Scope module;
        module.id    = 0;
        module.kind  = ScopeKind::Module;
        module.range = Range{0, static_cast<uint32_t>(text_.size())};
        index_.scopes.push_back(module);
        scope_ = 0;
        if (tree_.root()) {
            walk(tree_.root());
        }
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
    const syn::Tree &tree_;
    const syn::TokenBuffer &buffer_;
    std::string_view text_;
    uint32_t scope_      = 0;
    int32_t container_   = -1;
    bool inFuncType_     = false;
    bool exportNext_     = false;
    // The declaration whose signature is being walked: a `typeas` in it makes it generic.
    int32_t signatureOf_ = -1;

    // ---- ranges and text ----

    Range rangeOf(syn::TokenRef token) const {
        return token ? Range{token.begin(), token.end()} : Range{};
    }
    Range rangeOf(syn::Node node) const {
        if (!node || node.empty()) {
            return {};
        }
        Range r           = rangeOf(node.firstToken());
        const Range stop  = rangeOf(node.lastToken());
        if (stop.end > r.end) {
            r.end = stop.end;
        }
        return r;
    }
    std::string textOf(Range r) const {
        if (r.end > text_.size() || r.start >= r.end) {
            return {};
        }
        return std::string(text_.substr(r.start, r.end - r.start));
    }
    /// The text of a range on one line, runs of white space as one space.
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
    /// The name token of an identDef.
    static syn::TokenRef nameOf(cst::IdentDef ident) { return ident.tok(TK::Identifier); }
    /// The last name of a qualified name (`a::b::c`: `c`).
    static syn::TokenRef lastNameOf(syn::Node ref) {
        const size_t n = ref ? ref.tokCount(TK::Identifier) : 0;
        return n == 0 ? syn::TokenRef() : ref.tok(TK::Identifier, n - 1);
    }
    /// The child nodes of a block's statement list.
    void walkBlockBody(cst::StmtBlock block) {
        if (block) {
            walk(block.get<cst::StmtList>());
        }
    }
    /// A branch arm or a lambda body: a block (its statements) or an expression.
    void walkBody(syn::Node body) {
        if (body.is(NK::StmtBlock)) {
            walkBlockBody(cst::StmtBlock(body));
        } else {
            walk(body);
        }
    }

    void collectTokens() {
        index_.tokens.reserve(buffer_.size());
        for (const syn::Token &token : buffer_.tokens()) {
            if (token.kind == TK::Eof || token.kind == TK::Error) {
                continue;
            }
            TokenInfo info;
            info.range   = Range{token.begin, token.end};
            info.type    = static_cast<uint16_t>(token.kind);
            info.channel = token.kind == TK::Command        ? 2
                           : token.kind == TK::LineComment  ? 3
                           : token.kind == TK::DocComment   ? 3
                           : token.kind == TK::BlockComment ? 4
                                                            : 0;
            info.line    = static_cast<uint32_t>(buffer_.lineOf(token.begin));
            index_.tokens.push_back(info);
        }
    }

    /// The documentation of a declaration: the `///` lines right above it (design principles
    /// 2.10).
    std::string docAbove(syn::TokenRef first) const {
        return first ? buffer_.docAbove(first.index()) : std::string();
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

    Decl &declare(DeclKind kind, syn::TokenRef name, Range owner, Range full) {
        Decl decl;
        decl.id         = static_cast<uint32_t>(index_.decls.size());
        decl.kind       = kind;
        decl.name       = name.text();
        decl.nameRange  = rangeOf(name);
        decl.ownerRange = owner;
        decl.fullRange  = full.empty() ? decl.nameRange : full;
        decl.scope      = scope_;
        decl.container  = container_;
        // Declared directly in a namespace block: named with the namespaces around it.
        if (index_.scopes[scope_].kind == ScopeKind::Namespace && container_ >= 0 &&
            index_.decls[container_].kind == DeclKind::Namespace) {
            decl.qualified = index_.decls[container_].shownName() + "::" + decl.name;
        }
        index_.scopes[scope_].decls.push_back(decl.id);
        index_.decls.push_back(std::move(decl));
        return index_.decls.back();
    }

    /// A qualified name (IdentRef, RefData or RefType) used at this place.
    void addRef(syn::Node name, RefKind kind, bool callee = false) {
        if (!name) {
            return;
        }
        Ref ref;
        ref.kind   = kind;
        ref.scope  = scope_;
        ref.callee = callee;
        const size_t n = name.tokCount(TK::Identifier);
        for (size_t i = 0; i < n; ++i) {
            const syn::TokenRef id = name.tok(TK::Identifier, i);
            ref.path.push_back({id.text(), rangeOf(id)});
        }
        if (ref.path.empty()) {
            return;
        }
        ref.range = Range{ref.path.front().range.start, ref.path.back().range.end};
        index_.refs.push_back(std::move(ref));
    }

    // ---- the walk ----

    void walkChildren(syn::Node node) {
        for (const syn::Node child : node.nodes()) {
            walk(child);
        }
    }

    void walk(syn::Node node) {
        if (!node) {
            return;
        }
        switch (node.kind()) {
        case NK::ModuleDecl:
            return moduleDecl(cst::ModuleDecl(node));
        case NK::ImportDecl:
            return importDecl(cst::ImportDecl(node));
        case NK::ExportDecl:
            return exportDecl(cst::ExportDecl(node));
        case NK::UsingNamespaceDecl:
            return addRef(node.get<cst::IdentRef>(), RefKind::Namespace);
        case NK::StmtBlock:
            pushScope(ScopeKind::Block, rangeOf(node));
            walkBlockBody(cst::StmtBlock(node));
            return popScope();
        case NK::FuncData:
            return funcData(cst::FuncData(node));
        case NK::FuncAnno:
            addRef(node.get<cst::IdentRef>(), RefKind::Value, true);
            return walk(node.get<cst::AngledValues>());
        case NK::FuncDecl:
            return funcDecl(cst::FuncDecl(node));
        case NK::NamespaceDecl:
            return namespaceDecl(cst::NamespaceDecl(node));
        case NK::DataDecl:
            return dataDecl(cst::DataDecl(node));
        case NK::TypeDecl:
            return typeDecl(cst::TypeDecl(node));
        case NK::KeyTypePair:
            addKey(node.get<cst::IdentDef>(), true);
            return walk(node.type());
        case NK::KeyValuePair:
            addKey(node.get<cst::IdentDef>(), false);
            return walk(node.expr());
        case NK::KeyParamPair:
            // Reached only inside a function *type*: the parameters of a declaration are
            // handled by the declaration.
            addKey(node.get<cst::IdentDef>(), true);
            walk(node.type());
            return walk(node.expr());
        case NK::MatchCase:
            return matchCase(cst::MatchCase(node));
        case NK::CatchClause:
            return catchClause(cst::CatchClause(node));
        case NK::MatchExpr:
            addRef(node.get<cst::IdentRef>(), RefKind::Value);
            for (const auto arm : node.all<cst::MatchCase>()) {
                walk(arm);
            }
            return;
        case NK::IfExpr:
            return ifExpr(cst::IfExpr(node));
        case NK::LoopExpr:
            return loopExpr(cst::LoopExpr(node));
        case NK::AnnoExpr:
            return annoExpr(cst::AnnoExpr(node));
        case NK::AccessExpr:
            return accessExpr(cst::AccessExpr(node), false);
        case NK::ArrayData:
            return arrayData(cst::ArrayData(node));
        case NK::RefData:
            return addRef(node, RefKind::Value);
        case NK::RefType:
            return addRef(node, RefKind::Type);
        case NK::TypeAsType:
            // `typeas T` declares the type variable T for the rest of the signature.
            if (auto ident = node.get<cst::IdentDef>(); ident && nameOf(ident)) {
                const Range whole = rangeOf(node);
                declare(DeclKind::TypeVariable, nameOf(ident), whole, whole);
                if (signatureOf_ >= 0) {
                    index_.decls[signatureOf_].generic = true;
                }
            }
            return;
        case NK::FuncType: {
            const bool saved = std::exchange(inFuncType_, true);
            walkChildren(node);
            inFuncType_ = saved;
            return;
        }
        default:
            return walkChildren(node);
        }
    }

    void addKey(cst::IdentDef ident, bool inType) {
        if (ident && nameOf(ident)) {
            index_.keys.push_back({nameOf(ident).text(), rangeOf(nameOf(ident)), inType});
        }
    }

    void moduleDecl(cst::ModuleDecl ctx) {
        if (auto ident = ctx.get<cst::IdentDef>(); ident && nameOf(ident)) {
            index_.moduleName      = nameOf(ident).text();
            index_.moduleNameRange = rangeOf(nameOf(ident));
        }
    }

    void importDecl(cst::ImportDecl ctx) {
        const auto moduleName  = ctx.get<cst::ModuleName>();
        const std::string from = moduleName ? textOf(rangeOf(moduleName)) : std::string();
        const Range whole      = rangeOf(ctx);
        const auto alias       = ctx.get<cst::IdentDef>();
        const auto single      = ctx.get<cst::ImportItem>();
        const auto braced      = ctx.get<cst::BracedImports>();
        if (!ctx.has(TK::KwFrom)) {
            // `import a.b`: the module is known by its last name; `import a.b as x` by x.
            if (alias && nameOf(alias)) {
                Decl &decl        = declare(DeclKind::Module, nameOf(alias), whole, whole);
                decl.importedFrom = from;
            } else if (const syn::TokenRef last = lastNameOf(moduleName)) {
                Decl &decl        = declare(DeclKind::Module, last, whole, whole);
                decl.importedFrom = from;
            }
            return;
        }
        std::vector<cst::IdentDef> names;
        if (single) {
            // `import a from m` is `import { a } from m` (design principles 2.12).
            const auto local = single.get<cst::IdentDef>(1);
            names.push_back(local ? local : single.get<cst::IdentDef>(0));
        }
        // `{ find as str_find }` declares str_find; the name in the module is not visible.
        for (const auto item : braced.all<cst::ImportItem>()) {
            const auto local = item.get<cst::IdentDef>(1);
            names.push_back(local ? local : item.get<cst::IdentDef>(0));
        }
        for (const auto ident : names) {
            if (nameOf(ident)) {
                Decl &decl        = declare(DeclKind::Import, nameOf(ident), whole, whole);
                decl.importedFrom = from;
            }
        }
    }

    void exportDecl(cst::ExportDecl ctx) {
        if (auto list = ctx.get<cst::BracedIdents>().get<cst::IdentList>()) {
            // `export { a, b }`: uses of the names, which become exported.
            for (const auto ident : list.all<cst::IdentDef>()) {
                if (nameOf(ident)) {
                    Ref ref;
                    ref.kind  = RefKind::Value;
                    ref.scope = scope_;
                    ref.path.push_back({nameOf(ident).text(), rangeOf(nameOf(ident))});
                    ref.range = ref.path.back().range;
                    index_.refs.push_back(std::move(ref));
                    exportedNames_.push_back(nameOf(ident).text());
                }
            }
            return;
        }
        exportNext_ = true;
        walk(ctx.get<cst::DataDecl>());
        walk(ctx.get<cst::TypeDecl>());
        exportNext_ = false;
    }

    void declareParams(cst::PairedParams params, DeclKind kind, Range owner, bool lambda) {
        for (const auto pair : params.all<cst::KeyParamPair>()) {
            walk(pair.type());
            walk(pair.expr());
            if (auto ident = pair.get<cst::IdentDef>(); ident && nameOf(ident)) {
                Decl &decl      = declare(kind, nameOf(ident), owner, rangeOf(pair));
                decl.varParam   = pair.has(TK::KwVar);
                decl.lambda     = lambda;
                decl.annotation = flatTextOf(rangeOf(pair.type()));
                decl.header     = flatTextOf(rangeOf(pair));
            }
        }
    }

    void funcDecl(cst::FuncDecl ctx) {
        const auto annos = ctx.all<cst::FuncAnno>();
        for (const auto anno : annos) {
            walk(anno);
        }
        const Range whole        = rangeOf(ctx);
        int32_t self             = -1;
        const auto block         = ctx.get<cst::StmtBlock>();
        // An operator overload (`func operator +(...)`) is named by its `operator` token.
        syn::Node nameNode = ctx.get<cst::IdentRef>();
        if (!nameNode) {
            nameNode = ctx.get<cst::OperatorName>();
        }
        if (const syn::TokenRef name = lastNameOf(nameNode)) {
            Decl &decl    = declare(DeclKind::Function, name, whole, whole);
            decl.exported = ctx.has(TK::KwExport);
            if (auto modifiers = ctx.get<cst::Modifiers>()) {
                decl.sync  = modifiers.has(TK::KwSync);
                decl.macro = modifiers.has(TK::KwMacro);
            }
            // The header: from after the annotations to the body.
            Range header = whole;
            if (!annos.empty()) {
                header.start = rangeOf(annos.back()).end;
            }
            if (block && rangeOf(block).start > header.start) {
                header.end = rangeOf(block).start;
            }
            decl.header     = flatTextOf(header);
            decl.annotation = flatTextOf(rangeOf(ctx.type()));
            decl.doc        = docAbove(ctx.firstToken());
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
        declareParams(
            ctx.get<cst::AngledParams>().get<cst::PairedParams>(),
            DeclKind::WithParameter,
            whole,
            false);
        declareParams(
            ctx.get<cst::ParentParams>().get<cst::PairedParams>(),
            DeclKind::Parameter,
            whole,
            false);
        walk(ctx.type());
        signatureOf_ = savedSignature;
        walkBlockBody(block);
        popScope();
        container_ = savedContainer;
    }

    void funcData(cst::FuncData ctx) {
        const Range whole = rangeOf(ctx);
        pushScope(ScopeKind::Lambda, whole);
        const int32_t savedSignature = std::exchange(signatureOf_, -1);
        declareParams(
            ctx.get<cst::AngledParams>().get<cst::PairedParams>(),
            DeclKind::WithParameter,
            whole,
            true);
        declareParams(
            ctx.get<cst::ParentParams>().get<cst::PairedParams>(),
            DeclKind::Parameter,
            whole,
            true);
        walk(ctx.type());
        signatureOf_ = savedSignature;
        // The body follows the `=>`.
        if (ctx.has(TK::FatArrow)) {
            const syn::Child last = ctx.lastChild();
            if (last.isNode()) {
                walkBody(last.node());
            }
        }
        popScope();
    }

    void namespaceDecl(cst::NamespaceDecl ctx) {
        const Range whole = rangeOf(ctx);
        int32_t self      = -1;
        const syn::Node path = ctx.get<cst::IdentRef>();
        if (const syn::TokenRef name = lastNameOf(path)) {
            Decl &decl = declare(DeclKind::Namespace, name, whole, whole);
            self       = static_cast<int32_t>(decl.id);
            // `namespace A::B { ... }`: the whole path, after the namespaces around the block.
            const size_t parts = path.tokCount(TK::Identifier);
            if (parts > 1) {
                std::string written;
                for (size_t i = 0; i < parts; ++i) {
                    written += (i == 0 ? "" : "::") + std::string(path.tok(TK::Identifier, i).text());
                }
                const size_t outer = decl.qualified.empty()
                                         ? std::string::npos
                                         : decl.qualified.size() - decl.name.size();
                decl.qualified =
                    (outer == std::string::npos ? std::string() : decl.qualified.substr(0, outer)) +
                    written;
            }
        }
        const int32_t savedContainer = container_;
        if (self >= 0) {
            container_ = self;
        }
        const uint32_t scopeId = pushScope(ScopeKind::Namespace, whole, self);
        if (self >= 0) {
            index_.decls[self].bodyScope = static_cast<int32_t>(scopeId);
        }
        for (const syn::Node item : ctx.nodes()) {
            if (!item.is(NK::IdentRef)) {
                walk(item);
            }
        }
        popScope();
        container_ = savedContainer;
    }

    void dataDecl(cst::DataDecl ctx) {
        const bool exported = std::exchange(exportNext_, false);
        const Range whole   = rangeOf(ctx);
        walk(ctx.get<cst::TypeList>());
        walk(ctx.get<cst::DataList>());
        // carrier : identList | parentIdents | bracedIdents | bracketIdents
        cst::IdentList list = ctx.get<cst::IdentList>();
        const bool plain    = static_cast<bool>(list);
        if (!list) {
            list = ctx.get<cst::ParentIdents>().get<cst::IdentList>();
        }
        if (!list) {
            list = ctx.get<cst::BracedIdents>().get<cst::IdentList>();
        }
        if (!list) {
            list = ctx.get<cst::BracketIdents>().get<cst::IdentList>();
        }
        const auto names = list.all<cst::IdentDef>();
        const bool one   = plain && names.size() == 1;
        for (const auto ident : names) {
            if (!nameOf(ident)) {
                continue;
            }
            Decl &decl = declare(
                ctx.has(TK::KwVar) ? DeclKind::Variable : DeclKind::Local,
                nameOf(ident),
                whole,
                whole);
            decl.exported = exported;
            // A binding is visible after its declaration, not inside its own initializer.
            decl.visibleFrom = whole.end;
            decl.doc         = docAbove(ctx.firstToken());
            if (one && ctx.get<cst::TypeList>()) {
                decl.annotation = flatTextOf(rangeOf(ctx.get<cst::TypeList>()));
            }
        }
    }

    void typeDecl(cst::TypeDecl ctx) {
        const bool exported = std::exchange(exportNext_, false);
        const Range whole   = rangeOf(ctx);
        if (const syn::TokenRef name = lastNameOf(ctx.get<cst::IdentRef>())) {
            Decl &decl    = declare(DeclKind::Type, name, whole, whole);
            decl.exported = exported;
            decl.header   = flatTextOf(whole);
            decl.doc      = docAbove(ctx.firstToken());
        }
        // `type Pair<T> = (T, T)`: the parameters are type names inside the declaration.
        pushScope(ScopeKind::Type, whole);
        for (const auto ident : ctx.all<cst::IdentDef>()) {
            if (nameOf(ident)) {
                declare(DeclKind::TypeParameter, nameOf(ident), whole, rangeOf(ident));
            }
        }
        walk(ctx.type());
        popScope();
    }

    void matchCase(cst::MatchCase ctx) {
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        for (const auto pattern : ctx.all<cst::Pattern>()) {
            const Range whole = rangeOf(pattern);
            if (auto ref = pattern.get<cst::IdentRef>()) {
                // `error e` binds e; a lone identifier is a reference (a constant or a tag).
                addRef(ref, RefKind::Value);
                if (auto bound = pattern.get<cst::IdentDef>(); bound && nameOf(bound)) {
                    declare(DeclKind::Binding, nameOf(bound), whole, whole);
                }
            } else if (auto list = pattern.get<cst::IdentList>()) {
                for (const auto ident : list.all<cst::IdentDef>()) {
                    if (nameOf(ident)) {
                        declare(DeclKind::Binding, nameOf(ident), whole, whole);
                    }
                }
            } else {
                walkChildren(pattern);
            }
        }
        if (ctx.has(TK::FatArrow)) {
            const syn::Child last = ctx.lastChild();
            if (last.isNode()) {
                walkBody(last.node());
            }
        }
        popScope();
    }

    void catchClause(cst::CatchClause ctx) {
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        walk(ctx.type());
        if (auto ident = ctx.get<cst::IdentDef>(); ident && nameOf(ident)) {
            declare(DeclKind::Binding, nameOf(ident), rangeOf(ctx), rangeOf(ctx));
        }
        walkBlockBody(ctx.get<cst::StmtBlock>());
        popScope();
    }

    /// loop state = init for x in xs while c { body }: init and xs are outside the scope of the
    /// state names and of x; c and the body are inside.
    void loopExpr(cst::LoopExpr ctx) {
        const auto forClause   = ctx.get<cst::LoopFor>();
        const auto whileClause = ctx.get<cst::LoopWhile>();
        walk(ctx.expr());
        walk(forClause.expr());
        const Range whole = rangeOf(ctx);
        pushScope(ScopeKind::Arm, whole);
        if (auto tuple = ctx.get<cst::ParentIdents>()) {
            for (const auto ident : tuple.get<cst::IdentList>().all<cst::IdentDef>()) {
                if (nameOf(ident)) {
                    declare(DeclKind::Binding, nameOf(ident), whole, rangeOf(ident));
                }
            }
        } else if (auto ident = ctx.get<cst::IdentDef>(); ident && nameOf(ident)) {
            declare(DeclKind::Binding, nameOf(ident), whole, rangeOf(ident));
        }
        if (auto ident = forClause.get<cst::IdentDef>(); ident && nameOf(ident)) {
            declare(DeclKind::Binding, nameOf(ident), whole, rangeOf(ident));
        }
        walk(whileClause.expr());
        walkBlockBody(ctx.get<cst::StmtBlock>());
        popScope();
    }

    /// if c then a else b: the condition, then each arm in a scope of its own.
    void ifExpr(cst::IfExpr ctx) {
        bool arms = false;
        for (size_t i = 0; i < ctx.childCount(); ++i) {
            const syn::Child child = ctx.child(i);
            if (child.isToken()) {
                arms = arms || child.token().is(TK::KwThen);
                continue;
            }
            if (!arms) {
                walk(child.node());
                continue;
            }
            pushScope(ScopeKind::Arm, rangeOf(child.node()));
            walkBody(child.node());
            popScope();
        }
    }

    void accessExpr(cst::AccessExpr ctx, bool callee) {
        const syn::Child first = ctx.child(0);
        if (first.isNode() && first.node().is(NK::RefData)) {
            addRef(first.node(), RefKind::Value, callee && !ctx.has(TK::Dot));
        } else if (first.isNode()) {
            walk(first.node());
        }
        const Range whole = rangeOf(ctx);
        for (size_t i = 1; i < ctx.childCount(); ++i) {
            const syn::TokenRef token = ctx.child(i).token();
            if (!token || (!token.is(TK::Identifier) && !token.is(TK::Integer))) {
                continue;
            }
            const syn::TokenRef dot = ctx.child(i - 1).token();
            if (!dot) {
                continue;
            }
            Member member;
            member.name      = token.text();
            member.nameRange = rangeOf(token);
            member.receiver  = Range{whole.start, rangeOf(dot).start};
            member.numeric   = token.is(TK::Integer);
            index_.members.push_back(std::move(member));
        }
    }

    void annoExpr(cst::AnnoExpr ctx) {
        bool callee = false;
        if (ctx.childCount() >= 2) {
            const syn::Node next = ctx.child(1).node();
            callee = next && (next.is(NK::ParentArgues) || next.is(NK::AngledValues));
        }
        const Range whole = rangeOf(ctx);
        bool first        = true;
        for (const syn::Node sub : ctx.nodes()) {
            if (first) {
                // The expression the postfixes apply to: a name used as a callee, a member
                // chain, or anything else.
                first = false;
                if (sub.is(NK::AccessExpr)) {
                    accessExpr(cst::AccessExpr(sub), callee);
                } else if (sub.is(NK::RefData)) {
                    addRef(sub, RefKind::Value, callee);
                } else {
                    walk(sub);
                }
                continue;
            }
            if (sub.is(NK::MemberAccess)) {
                syn::TokenRef name = sub.tok(TK::Identifier);
                if (!name) {
                    name = sub.tok(TK::Integer);
                }
                const syn::TokenRef dot = sub.tok(TK::Dot);
                if (name && dot) {
                    Member member;
                    member.name      = name.text();
                    member.nameRange = rangeOf(name);
                    member.receiver  = Range{whole.start, rangeOf(dot).start};
                    member.numeric   = name.is(TK::Integer);
                    index_.members.push_back(std::move(member));
                }
            } else {
                walk(sub);
            }
        }
    }

    void arrayData(cst::ArrayData ctx) {
        if (!ctx.has(TK::KwFor)) {
            return walkChildren(ctx);
        }
        // [e for x in xs if c]: xs is outside the scope of x, e and c are inside.
        const auto exprs = ctx.exprs();
        const size_t n   = exprs.size();
        if (n >= 2) {
            walk(exprs[1]);
        }
        pushScope(ScopeKind::Arm, rangeOf(ctx));
        const auto binder = ctx.get<cst::IdentRef>();
        if (const syn::TokenRef name = lastNameOf(binder)) {
            declare(DeclKind::Binding, name, rangeOf(ctx), rangeOf(binder));
        }
        if (n >= 1) {
            walk(exprs[0]);
        }
        if (n >= 3) {
            walk(exprs[2]);
        }
        popScope();
    }

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
            // The rest of a qualified name walks into namespaces of this file.
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

SyntaxIndex buildSyntaxIndex(const camel::parse::syntax::Tree *tree, bool complete) {
    SyntaxIndex index;
    index.complete = complete;
    if (tree != nullptr) {
        Walker(index, *tree).run();
    } else {
        Scope module;
        module.kind = ScopeKind::Module;
        index.scopes.push_back(module);
    }
    return index;
}

} // namespace camel::service::semantic
