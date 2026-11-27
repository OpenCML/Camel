// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the module and the declarations in it -- the program, the four dispatchers,
// imports and exports, namespaces, the data, tag, resource and type declarations, the statements
// and blocks they hold -- and the definitions of the build's per-thread state.

#include "builder_internal.h"

namespace camel::parse::ast {

// The build in progress, defined here once for every unit (builder_internal.h declares them).
thread_local camel::source::source_context_ptr_t g_sourceContext = nullptr;
thread_local camel::source::source_file_id_t g_sourceFileId = camel::source::kInvalidSourceFileId;
thread_local std::shared_ptr<NodeArena> g_arena             = nullptr;

// The arena of one build: the nodes of the tree being built live in it, and it goes away after.
struct ArenaScope {
    std::shared_ptr<NodeArena> previous = std::exchange(g_arena, std::make_shared<NodeArena>());
    ~ArenaScope() { g_arena = std::move(previous); }
};

} // namespace camel::parse::ast

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
    if (const auto it = programPermissions_.find(std::string(name));
        it != programPermissions_.end()) {
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
        | ('export')? 'tag' identDef '=' bracketIdents ;
A tag is a name in the namespace of functions and types (design principles 6.2). The AST keeps
the positions as a bit set (TagPosition), the type parameters and the condition as written, the
parameters as written, the members of a group and the passes its declaration names; the GCT
builder registers the tag with them.
*/
Value Builder::visitTagDecl(cst::TagDecl context) {
    ENTER("TagDecl");
    // The name is written as a definition (`identDef`), not a reference: `tag name on ...`.
    const auto name     = context->get<cst::IdentDef>();
    Reference ref       = qualifyRef(Reference(name ? name->text() : std::string{}));
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
resourceDecl : 'export'? 'resource' identDef ('=' bracketIdents
    | ('unordered' | 'idempotent' | 'overwriting' | 'readonly' | 'observed')*)?
*/
Value Builder::visitResourceDecl(cst::ResourceDecl context) {
    ENTER("ResourceDecl");
    const auto name = context->get<cst::IdentDef>();
    node_ptr_t declNode =
        createNodeAs<ResourceDeclLoad>(qualifyRef(Reference(name ? name->text() : std::string{})));
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
    } else {
        // The words after the name are the modifiers, contextual words kept as tokens of this
        // declaration (so the tree stays lossless) rather than child nodes: `unordered`,
        // `idempotent`, `overwriting`, `readonly`, `observed` (design principles 3.3). Unknown
        // words are not read from here: the parser accepts only these five.
        for (size_t i = 0; i < context->childCount(); ++i) {
            const auto child = context->child(i);
            if (!child.isToken()) {
                continue;
            }
            const std::string word = child.text();
            if (word == "unordered") {
                decl->setUnordered(true);
            } else if (word == "idempotent") {
                decl->setIdempotent(true);
            } else if (word == "overwriting") {
                decl->setOverwriting(true);
            } else if (word == "readonly") {
                decl->setReadonly(true);
            } else if (word == "observed") {
                decl->setObserved(true);
            }
        }
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
resourceClause : ('spelling'? ('pure' | 'panic')* ('uses' useEntry (',' useEntry)* | 'pure')*)
useEntry : 'var'? identRef
*/
Value Builder::visitResourceClause(cst::ResourceClause context) {
    ENTER("ResourceClause");
    node_ptr_t clauseNode = createNodeAs<ResourceClauseLoad>();
    setNodeTokenRangeByContext(clauseNode, context);
    auto clause = unwrapNodeAs<ResourceClauseLoad>(clauseNode);
    // The clause's tokens run together without white space or comments: a leading `pure` is the
    // word, while `uses pure` (a parameter of that name) starts with `uses`. The parser reads
    // both words so that `pure uses fs` is reported where it is written (PureAndUses) rather
    // than as a syntax error. `spelling` is read the same way and is written before the rest
    // (design principles 9.5), so the prefix is read here too -- and `uses spelling` names a
    // resource of that name, which starts with `uses`.
    //
    // `panic` is read the same way and stands beside `pure` (design principles 3.9): it is a word
    // with no list, so nothing follows it to read. The run stops at `uses`, which keeps `uses
    // panic` a resource named `panic` rather than this word, and it keeps the reading of a clause
    // to the words the parser consumes before the list.
    std::string clauseText = context->text();
    if (clauseText.starts_with("spelling")) {
        clause->setSpelling(true);
        clauseText = clauseText.substr(std::string_view("spelling").size());
    }
    for (;;) {
        if (clauseText.starts_with("pure")) {
            clause->setPure(true);
            clauseText = clauseText.substr(std::string_view("pure").size());
        } else if (clauseText.starts_with("panic")) {
            clause->setPanic(true);
            clauseText = clauseText.substr(std::string_view("panic").size());
        } else {
            break;
        }
    }
    for (const auto entry : context->all<cst::UsesEntry>()) {
        // One resource, and whether it is written with `var` (design principles 3.3): the entry
        // reads the resource without it and writes the resource with it. The entry's own token
        // range goes with it, so a clause that breaks the rule is reported where the word belongs.
        const auto ident = entry->get<cst::IdentRef>();
        clause->addUse(
            value_cast<Reference>(visitIdentRef(ident)),
            entry->has(TK::KwVar),
            extractTokenRangeFromContext(ident));
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
    case NK::EditExpr:
        return visitEditExpr(cst::EditExpr(context));
    case NK::ViewExpr:
        return visitViewExpr(cst::ViewExpr(context));
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
    case NK::TypeOfExpr:
        return visitTypeOfExpr(cst::TypeOfExpr(context));
    case NK::MatchTypeType:
        return visitMatchTypeType(cst::MatchTypeType(context));
    case NK::EnumType:
        return visitEnumType(cst::EnumType(context));
    default:
        throw std::runtime_error(
            "Builder: not a type: " + std::string(syn::nodeKindName(context.kind())));
    }
}
