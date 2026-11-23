// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the lowering of a module's declarations -- its head, imports, exports, and
// the data, tag, resource, function, type, enum and struct declarations.

#include "builder.h"
#include <exception>

#include "camel/compile/semantic_recorder.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/module/userdef.h"
#include "camel/core/operator_names.h"
#include "camel/core/tags.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/native.h"
#include "camel/core/type/other.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typecall.h"
#include "camel/core/type/typevar.h"
#include "camel/parse/ast/type.h"
#include "camel/utils/escape.h"
#include "camel/utils/number_text.h"
#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"
#include <stdexcept>

#include "builder_internal.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gct {

// The resource model of a function type (design effects plan 2.2, 3.1): short, as `Type` is.
namespace sem = camel::core::sem;

Reference defaultImportNamespace(const std::string &path);

pair<node_ptr_t, data_ptr_t> Builder::makeRefData(const node_ptr_t &expr) {
    const string id    = std::to_string(idIndex_++);
    node_ptr_t refNode = tree_->makeNRef(id);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(id);
    return make_pair(refNode, data);
}

pair<data_ptr_t, bool> Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticDataFromNode(node);
    // An enclosing literal only fills the names among its own elements (CompositeData::refs),
    // so an inner literal with names of its own is an element computed like any other
    // expression: `((h, k), 1)` is `let a = (h, k)` then `(a, 1)`. Embedded as a constant it
    // kept its unresolved element types ("(ref, ref)") and its names were never filled.
    if (data && !isUnresolvedLiteral(data)) {
        if (node->type() == NodeKind::DREF) {
            // A name: read where the literal is built, unless an element computed after it is
            // hoisted first.
            auto entry = std::find_if(
                pendingNames_.begin(),
                pendingNames_.end(),
                [&](const auto &candidate) { return candidate.first == execNode; });
            if (entry == pendingNames_.end()) {
                pendingNames_.emplace_back(execNode, std::vector<PendingName>{});
                entry = std::prev(pendingNames_.end());
            }
            entry->second.push_back(
                PendingName{std::dynamic_pointer_cast<camel::core::data::RefData>(data), node});
        }
        return make_pair(data, false);
    } else {
        // The names written before this element are used before it: they are hoisted ahead of it.
        const auto entry =
            std::find_if(pendingNames_.begin(), pendingNames_.end(), [&](const auto &candidate) {
                return candidate.first == execNode;
            });
        if (entry != pendingNames_.end()) {
            for (const PendingName &name : entry->second) {
                auto [nameNode, nameData] = makeRefData(name.expr);
                *execNode << nameNode;
                if (name.data != nullptr) {
                    name.data->rename(
                        std::dynamic_pointer_cast<camel::core::data::RefData>(nameData)->ref());
                }
            }
            entry->second.clear();
        }
        auto [refNode, refData] = makeRefData(node);
        *execNode << refNode;
        return make_pair(refData, true);
    }
}

pair<data_ptr_t, bool>
Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) {
        dangling = true;
    }
    return make_pair(refData, dang);
}

void Builder::initInnerTypes() {
    typeScope_->clear();
    typeScope_->insert(Reference("i32"), Type::Int32());
    typeScope_->insert(Reference("i64"), Type::Int64());
    typeScope_->insert(Reference("int"), Type::Int64());
    typeScope_->insert(Reference("f32"), Type::Float32());
    typeScope_->insert(Reference("f64"), Type::Float64());
    typeScope_->insert(Reference("float"), Type::Float64());
    typeScope_->insert(Reference("c64"), Type::Complex64());
    typeScope_->insert(Reference("c128"), Type::Complex128());
    // Reduced-precision tensor element types (design principles 7.3). They are names a
    // program writes only as an argument of an array spelling and of `from_bytes::<...>`; no
    // literal or scalar operator produces a value of either.
    typeScope_->insert(Reference("bf16"), Type::BFloat16());
    typeScope_->insert(Reference("f16"), Type::Float16());
    typeScope_->insert(Reference("bool"), Type::Bool());
    typeScope_->insert(Reference("string"), Type::String());
    // The type of binary data (design principles 11.1), a built-in name like `string`.
    typeScope_->insert(Reference("Bytes"), camel::core::type::BytesType::get());
    typeScope_->insert(Reference("void"), Type::Void());
    // The bottom type (design principles 3.9): `never` is the type of a function that never
    // returns and of an expression that leaves the function.
    typeScope_->insert(Reference("never"), Type::Never());
    // The built-in collection types, written `Map<K, V>` and `Set<T>` (design principles 11.3).
    typeScope_->insert(Reference("Map"), MapType::prototype());
    typeScope_->insert(Reference("Set"), SetType::prototype());
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/

node_ptr_t Builder::visitModule(const AST::node_ptr_t &ast) {
    ENTER("Module");
    // The root of a file, and the module the file is; the file's name is its name.
    GUARD(ast->type() == AST::LoadType::Module, "Expected the module load at the root");
    // Extract different parts of the module node
    auto importNodes   = ast->atAs<AST::RepeatedLoad>(0);
    auto exportOptNode = ast->atAs<AST::OptionalLoad>(1);
    auto stmtNodes     = ast->atAs<AST::RepeatedLoad>(2);

    // The names the module exports: a type among them is exported as soon as it is declared,
    // for the modules that import this one back (see below).
    exportedRefs_.clear();
    if (!exportOptNode->empty()) {
        for (const Reference &ref : exportOptNode->front()->loadAs<AST::ExportLoad>()->getRefs()) {
            exportedRefs_.insert(ref.toString());
        }
    }

    // Loading in steps (design principles 10.1: modules may import each other; the graph
    // model, 3.9.4). First every import is bound: the modules are found, nothing of them is
    // compiled. Then the module declares its types, compiling an imported module only when one
    // of its types is named. Then the imported modules are compiled as far as their
    // declarations, and only then the rest of this module. A module that imports this one back
    // finds it in the middle of this: with its types declared, and the rest of its
    // declarations known before any function body of the program is built. One import that
    // fails is reported and the others are still processed.
    importedModules_.clear();
    for (const auto &import : *importNodes) {
        try {
            visitImport(import);
        } catch (const BuildAbortException &) {
            continue;
        }
    }

    vector<node_ptr_t> decls;
    vector<node_ptr_t> stmts;

    node_ptr_t root = makeExec();

    // The tags written before `module` (`#gpu module m`, design principles 13.4) are kept beside
    // the module node, like the tags of a function beside the function; the GIR builder reads
    // them for each top-level function of this module. A module that writes none has no such
    // child at all.
    attachTagUses(root, ast);

    // The namespaces the module declares, and the `using namespace` of its top level, which
    // apply to the whole module whatever their position (design principles 10.2).
    ownNamespaces_.clear();
    namespacePath_.clear();
    usingFrames_.clear();
    std::vector<Reference> moduleUsings;
    for (const auto &stmt : *stmtNodes) {
        if (stmt->type() != AST::LoadType::Stmt) {
            continue;
        }
        const auto &load = stmt->loadAs<AST::StmtLoad>();
        std::vector<std::string> path;
        switch (load->stmtType()) {
        case AST::StmtType::Func:
            path = stmt->loadAs<AST::FuncDeclLoad>()->ref().paths();
            break;
        case AST::StmtType::Type:
            path = stmt->loadAs<AST::TypeDeclLoad>()->ref().paths();
            break;
        case AST::StmtType::Data:
            if (!stmt->loadAs<AST::DataDeclLoad>()->refs().empty()) {
                path = stmt->loadAs<AST::DataDeclLoad>()->refs().front().paths();
            }
            break;
        case AST::StmtType::UsingNamespace:
            moduleUsings.push_back(stmt->loadAs<AST::UsingNamespaceLoad>()->ref());
            break;
        default:
            break;
        }
        std::string prefix;
        for (const std::string &part : path) {
            prefix += (prefix.empty() ? "" : "::") + part;
            ownNamespaces_.insert(prefix);
        }
    }
    usingFrames_.push_back(moduleUsings);
    tree_->addUsings(root, moduleUsings);
    // A top-level declaration is lowered in its namespace, with the `using namespace` of the
    // namespace blocks it is written in; the GIR builder finds them on its node.
    const auto visitTopLevel = [&](const AST::node_ptr_t &stmt) {
        std::vector<std::string> path;
        std::vector<Reference> usings;
        if (stmt->type() == AST::LoadType::Stmt) {
            const auto &load = stmt->loadAs<AST::StmtLoad>();
            usings           = load->scopeUsings();
            if (load->stmtType() == AST::StmtType::Func) {
                // An operator is named without its namespaces (it belongs to the module,
                // design principles 4.3); it is still lowered in the block it is written in.
                const auto &func = stmt->loadAs<AST::FuncDeclLoad>();
                path = func->operatorSymbol().empty() ? func->ref().paths() : func->declaredIn();
            } else if (load->stmtType() == AST::StmtType::Type) {
                path = stmt->loadAs<AST::TypeDeclLoad>()->ref().paths();
            } else if (
                load->stmtType() == AST::StmtType::Data &&
                !stmt->loadAs<AST::DataDeclLoad>()->refs().empty()) {
                path = stmt->loadAs<AST::DataDeclLoad>()->refs().front().paths();
            }
        }
        struct Scope {
            Builder &self;
            std::vector<std::string> path;
            Scope(Builder &b, std::vector<std::string> p, const std::vector<Reference> &u)
                : self(b), path(std::move(b.namespacePath_)) {
                self.namespacePath_ = std::move(p);
                self.usingFrames_.push_back(u);
            }
            ~Scope() {
                self.namespacePath_ = std::move(path);
                self.usingFrames_.pop_back();
            }
        } scope(*this, path, usings);
        for (const Reference &using_ : usings) {
            if (!usableNamespace(using_)) {
                diags_->of(SemanticDiag::UnresolvedReference)
                    .at(stmt->load()->tokenRange())
                    .commit(using_.toString());
                throw BuildAbortException();
            }
        }
        node_ptr_t node = visitStmt(stmt);
        tree_->addUsings(node, usings);
        tree_->setNamespaceOf(node, namespacePath_);
        return node;
    };

    // The statements in two passes, types first; the results are put back in source order.
    const auto isTypeDecl = [](const AST::node_ptr_t &stmt) {
        return stmt->type() == AST::LoadType::Stmt &&
               stmt->loadAs<AST::StmtLoad>()->stmtType() == AST::StmtType::Type;
    };
    std::vector<node_ptr_t> visited(stmtNodes->size(), nullptr);
    std::vector<bool> failed(stmtNodes->size(), false);
    // A tag declaration is registered before anything is lowered (design principles 6.2): a `#`
    // use written before it in this module reads it, and so does every tag of a type or a
    // field. It lowers to no node, so it takes no part in the two passes below.
    for (const auto &stmt : *stmtNodes) {
        if (stmt->type() != AST::LoadType::Stmt ||
            stmt->loadAs<AST::StmtLoad>()->stmtType() != AST::StmtType::Tag) {
            continue;
        }
        try {
            visitTagDecl(stmt);
        } catch (const BuildAbortException &) {
        }
    }
    // A resource declaration is registered before anything is lowered (design effects plan
    // 3.2): a clause written above it reads it, and so does every signature. It lowers to no
    // node, so it takes no part in the two passes below. The groups are checked once every
    // member is known, so a group may name a resource declared later in the module.
    for (const auto &stmt : *stmtNodes) {
        if (stmt->type() != AST::LoadType::Stmt ||
            stmt->loadAs<AST::StmtLoad>()->stmtType() != AST::StmtType::Resource) {
            continue;
        }
        try {
            visitResourceDecl(stmt);
        } catch (const BuildAbortException &) {
        }
    }
    // A `var` at the top level is a resource of the module, named by the variable (design
    // principles 3.5): `uses cnt` names it like any other, and a function that reads or writes it
    // is measured as touching it. It is declared with the resources, before any clause is read.
    for (const auto &stmt : *stmtNodes) {
        if (stmt->type() != AST::LoadType::Stmt ||
            stmt->loadAs<AST::StmtLoad>()->stmtType() != AST::StmtType::Data) {
            continue;
        }
        const auto &declared = stmt->loadAs<AST::DataDeclLoad>();
        if (!declared->isVar()) {
            continue;
        }
        for (const Reference &ref : declared->refs()) {
            if (module_->resource(ref) != nullptr) {
                continue; // a second declaration of the name is reported where it is built
            }
            camel::core::module::Module::ResourceDecl resource;
            resource.variable = true;
            module_->declareResource(ref, std::move(resource));
            if (exportedRefs_.contains(ref.toString())) {
                module_->setResourceExported(ref);
            }
        }
    }
    try {
        checkResourceGroups();
    } catch (const BuildAbortException &) {
    }
    for (size_t i = 0; i < stmtNodes->size(); ++i) {
        const auto &stmt = stmtNodes->at(i);
        if (!isTypeDecl(stmt)) {
            continue;
        }
        try {
            visited[i] = visitTopLevel(stmt);
        } catch (const BuildAbortException &) {
            failed[i] = true;
        }
    }
    compileImportedModules();
    for (size_t i = 0; i < stmtNodes->size(); ++i) {
        if (isTypeDecl(stmtNodes->at(i))) {
            continue;
        }
        try {
            visited[i] = visitTopLevel(stmtNodes->at(i));
        } catch (const BuildAbortException &) {
            failed[i] = true;
        }
    }

    for (size_t i = 0; i < stmtNodes->size(); ++i) {
        const auto &stmt = stmtNodes->at(i);
        if (failed[i] || visited[i] == nullptr) {
            continue;
        }
        try {
            node_ptr_t node = visited[i];
            if (node->type() == GCT::NodeKind::DECL) {
                decls.push_back(node);
            } else if (node->type() == GCT::NodeKind::FUNC) {
                // Handle function declarations
                node_ptr_t declNode = tree_->makeDecl(node->name(), true);
                node_ptr_t typeNode = node->at(0);
                *declNode << tree_->clone(typeNode);
                // The declaration record is the same declaration: a head type the signature
                // never writes (`func is_numeric::<T>(): bool`) makes it a template there too,
                // so the graph builder does not give the declaration an empty graph of its own.
                if (const auto *head = tree_->headTypeParamsOf(node)) {
                    tree_->setHeadTypeParams(declNode, *head);
                }

                // The defaults of the declaration's parameters are the declaration's, and
                // the graph builder evaluates them when it sees the declaration (before any body
                // is built, so that a call written above the definition finds them too).
                if (const auto *defaults = tree_->parameterDefaultsOf(node)) {
                    tree_->setParameterDefaults(declNode, *defaults);
                }
                // The condition goes with the declaration record too, for every function and not
                // only for a body-less one: a body-less declaration with the pass applicability
                // is a pass, not an operator, and the graph builder tells them apart by it
                // (design principles 9.3), and for a function with a body the record is where the
                // condition of the declaration is read while the declarations of one name are
                // compared -- two of them whose conditions exclude each other are overloads and
                // may stand side by side (design principles 4.4).
                if (const auto *when = tree_->whenOf(node); when != nullptr) {
                    tree_->setWhen(declNode, *when);
                }
                if (node->nativeDeclaration()) {
                    // The declaration keeps what makes it one: no body, so the graph builder
                    // makes an operator rule of it instead of a graph (design principles 10.4),
                    // and its tag uses go with it (`#op('module:function')`; design principles
                    // 6.2).
                    declNode->setNativeDeclaration();
                    if (const auto *uses = tree_->tagUsesOf(node); uses != nullptr) {
                        tree_->addTagUses(declNode, *uses);
                    }
                }
                setOriginFromAst(
                    context_,
                    declNode,
                    stmt,
                    camel::source::OriginKind::GctNode,
                    "gct.func.decl",
                    true,
                    {nodeOrigin(node)});
                registerGctSemantic(
                    context_,
                    declNode,
                    stmt,
                    {
                        semanticPart(
                            camel::source::SemanticRole::FuncName,
                            findSemanticOrigin(
                                astSemantic(context_, stmt),
                                camel::source::SemanticRole::FuncName),
                            -1,
                            node->name()),
                        semanticPart(
                            camel::source::SemanticRole::ReturnType,
                            nodeOrigin(typeNode),
                            -1,
                            "type"),
                    },
                    {nodeOrigin(node)},
                    true,
                    "synthetic function declaration");
                decls.push_back(declNode);
                stmts.push_back(node);
            } else {
                stmts.push_back(node);
            }
        } catch (const BuildAbortException &) {
            continue;
        }
    }

    // Add collected declarations to the root node
    if (!decls.empty()) {
        for (const auto &decl : decls) {
            *root << decl;
        }
    }

    // Add collected statements to the root node
    for (const auto &stmt : stmts) {
        *root << stmt;
    }

    // Process the optional export declaration if it exists
    if (!exportOptNode->empty()) {
        node_ptr_t exportNode = visitExport(exportOptNode->front());
        setOriginFromAst(
            context_,
            exportNode,
            exportOptNode->front(),
            camel::source::OriginKind::GctNode,
            "gct.export");
        *root << exportNode;
    }

    LEAVE("Module");
    setOriginFromAst(context_, root, ast, camel::source::OriginKind::GctNode, "gct.module");
    registerGctSemantic(context_, root, ast);
    return root;
}

/*
ImportDecl(string path, Ref[] refs, Ref as) ;
*/
void_ptr_t Builder::visitImport(const AST::node_ptr_t &ast) {
    ENTER("ImportDecl");
    // Ensure the AST node is of the expected type
    GUARD(ast->type() == AST::LoadType::Import, "Expected ImportLoad type");

    // Extract the import load details
    const auto &load     = ast->loadAs<AST::ImportLoad>();
    const auto &fullPath = load->getPath();
    const auto &refs     = load->refs();
    // `from m::N`: the module `m`, and the namespace `N` of it the names are taken from
    // (design principles 10.2).
    const size_t nsAt      = fullPath.find("::");
    const std::string path = nsAt == std::string::npos ? fullPath : fullPath.substr(0, nsAt);
    std::vector<std::string> nsPath;
    if (nsAt != std::string::npos) {
        const Reference ns(fullPath.substr(nsAt + 2));
        nsPath = ns.paths();
        nsPath.push_back(ns.ident());
    }
    // A name of the module as the import names it: under the namespace, when there is one.
    const auto inModule = [&](const Reference &ref) {
        if (nsPath.empty()) {
            return ref;
        }
        std::vector<std::string> parts = nsPath;
        parts.insert(parts.end(), ref.paths().begin(), ref.paths().end());
        parts.push_back(ref.ident());
        return Reference(parts);
    };

    // Attempt to import the module (importModule throws ModuleNotFound with detail on failure).
    // The failure is reported at the import, and what the import would have declared is
    // remembered so that its uses are not reported as well.
    module_ptr_t mod;
    try {
        mod = context_->importModule(path, module_.get());
    } catch (Diagnostic &d) {
        if (refs.empty()) {
            wholeImportFailed_ = true;
        }
        for (const Reference &ref : refs) {
            poisonedTypes_.insert(ref.toString());
        }
        if (ast->load()->origin() != camel::source::kInvalidOriginId) {
            d.range = ast->load()->origin();
        } else {
            // An import has no origin of its own; its tokens say where it is.
            d.range = load->tokenRange();
        }
        diags_->add(std::move(d));
        throw BuildAbortException();
    }
    if (!mod) {
        diags_->of(SemanticDiag::ModuleNotFound)
            .at(load->tokenRange())
            .commit(path, "import returned empty.");
        throw BuildAbortException();
    }

    // A module importing itself: allowed, and it has no effect (design principles 10.1). Its
    // names are its own already (with or without its own name in front, 10.2); the import
    // binds nothing, and records no import a cycle could be made of.
    if (mod == module_) {
        LEAVE("ImportDecl");
        return nullptr;
    }

    // Where the import stands, for the errors of the imported module and for the importers
    // the program's graph builder reports when it is in error.
    const auto origin = ast->load()->origin();
    module_->noteImport(mod, origin, load->tokenRange().start, load->tokenRange().end);
    importedModules_.push_back({ast, mod});

    // Import all references if none are specified; otherwise still import the module's
    // default-import refs so operator groups and other ambient symbols can participate
    // in name/operator resolution after a normal import.
    if (refs.empty()) {
        module_->importDefaultRefsFromMod(mod);
        // `import m` binds `m`, `import m as x` binds `x` (design principles 10.2), `import
        // m::N` binds `N` to that namespace of the module.
        if (nsPath.empty()) {
            const Reference localNamespace =
                load->hasAs() ? load->getAs() : defaultImportNamespace(path);
            module_->markImportedNamespaceFromMod(localNamespace, mod);
        } else {
            const Reference localNamespace =
                load->hasAs() ? load->getAs() : Reference(nsPath.back());
            module_->markImportedNamespaceFromMod(localNamespace, Reference(nsPath), mod);
        }
    } else {
        module_->importDefaultRefsFromMod(mod);
        // Import specific references
        for (size_t i = 0; i < refs.size(); ++i) {
            // `import { find as str_find }`: the entity `find` of the module under the
            // local name `str_find`; without `as` the two are the same.
            const Reference ref    = inModule(refs[i]);
            const Reference &local = load->localOf(i);
            module_->markImportedRefFromMod(local, ref, mod);
            module_->noteNamedImport(
                local,
                ref,
                mod,
                ast->load()->origin(),
                load->tokenRange().start,
                load->tokenRange().end);
            module_->markImportedNamespaceFromMod(local, ref, mod);
        }
    }

    LEAVE("ImportDecl");
    return nullptr;
}

void Builder::compileImportedModules() {
    for (const auto &[ast, mod] : importedModules_) {
        auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
        if (!ud || ud->compiling()) {
            // Not written in Camel (loaded where it was found), or this import closes a cycle
            // of imports: the module is being compiled further up and will be complete before
            // any function body is built.
            continue;
        }
        bool ok = false;
        std::optional<Diagnostic> failure;
        try {
            ok = ud->compile(CompileStage::Declared);
        } catch (Diagnostic &d) {
            failure = std::move(d);
        }
        if (ok && !failure) {
            continue;
        }
        if (!failure) {
            // A module that was found and is in error is not a module that was not found:
            // its own diagnostics say what is wrong with it.
            failure = DiagnosticBuilder::of(SemanticDiag::ImportedModuleHasErrors)
                          .commit(mod->name(), mod->path());
        }
        const auto &load = ast->loadAs<AST::ImportLoad>();
        if (load->refs().empty()) {
            wholeImportFailed_ = true;
        }
        for (const Reference &ref : load->refs()) {
            poisonedTypes_.insert(ref.toString());
        }
        if (ast->load()->origin() != camel::source::kInvalidOriginId) {
            failure->range = ast->load()->origin();
        } else {
            failure->range = load->tokenRange();
        }
        diags_->add(std::move(*failure));
    }
}

/*
ExportDecl(Ref[] refs) ;
*/
node_ptr_t Builder::visitExport(const AST::node_ptr_t &ast) {
    ENTER("ExportDecl");
    // Ensure the AST node is of the expected type
    GUARD(ast->type() == AST::LoadType::Export, "Expected ExportLoad type");

    // Extract the export load details
    const auto &load = ast->loadAs<AST::ExportLoad>();
    const auto &refs = load->getRefs();

    // Create an export node with the extracted references
    node_ptr_t res = tree_->makeExpt(refs);

    LEAVE("ExportDecl");
    return res;
}

node_ptr_t Builder::visitStmt(const AST::node_ptr_t &ast) {
    ENTER("Stmt");
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type");
    const auto &stmt    = ast->loadAs<AST::StmtLoad>();
    node_ptr_t stmtNode = nullptr;

    switch (stmt->stmtType()) {
    case AST::StmtType::Data:
        stmtNode = visitDataDecl(ast);
        break;
    case AST::StmtType::Func:
        stmtNode = visitFuncDecl(ast);
        break;
    case AST::StmtType::Type:
        stmtNode = visitTypeDecl(ast);
        break;
    case AST::StmtType::UsingNamespace:
        stmtNode = visitUsingNamespaceDecl(ast);
        break;
    case AST::StmtType::Expr:
        stmtNode = visitExprStmt(ast);
        break;
    case AST::StmtType::Exit:
        stmtNode = visitExitStmt(ast);
        break;
    case AST::StmtType::Block:
        stmtNode = visitStmtBlock(ast);
        break;
    case AST::StmtType::Tag:
        // A tag declaration has no node: it is registered when the module is entered
        // (visitTagDecl, visitModule), so that a use written before it reads it.
        LEAVE("Stmt");
        return nullptr;
    case AST::StmtType::Resource:
        // A resource declaration has no node either: it is registered when the module is
        // entered (visitResourceDecl, visitModule), before any clause reads it.
        LEAVE("Stmt");
        return nullptr;
    default:
        throw std::runtime_error("Unknown statement type");
    }

    LEAVE("Stmt");
    // The same for a write written as a statement: the statement node gives it the place the
    // statement stands at, and the parts it carries -- the operator's place among them -- stay its
    // own (visitData).
    const auto ownParts = ownSemanticParts(context_, stmtNode);
    setOriginFromAst(context_, stmtNode, ast, camel::source::OriginKind::GctNode, "gct.stmt");
    registerGctSemantic(context_, stmtNode, ast, ownParts.parts, ownParts.inputs);
    return stmtNode;
}

Reference defaultImportNamespace(const std::string &path) {
    size_t begin = 0;
    while (begin < path.size() && path[begin] == '.') {
        ++begin;
    }
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < path.size()) {
        begin = dot + 1;
    }
    std::string name = path.substr(begin);
    if (name.empty()) {
        name = path;
    }
    return Reference(name);
}

/*
DataDecl(bool isVar, Ref[] refs) : Type* type, Data* value;
*/
node_ptr_t Builder::visitDataDecl(const AST::node_ptr_t &ast) {
    ENTER("DataDecl");
    // Ensure the provided AST node is of type Stmt
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for DataDecl");

    // Extract data declaration details, including whether it's a variable, references, type nodes,
    // and data nodes
    const auto &dataDeclLoad = ast->loadAs<AST::DataDeclLoad>();
    bool isVar               = dataDeclLoad->isVar();
    const auto &refs         = dataDeclLoad->refs();
    const auto &typeNodes    = ast->atAs<AST::RepeatedLoad>(0);
    const auto &dataNodes    = ast->atAs<AST::RepeatedLoad>(1);

    // The names of the declaration get a scope of their own, which is not a block of the
    // program: in a sync block the initializer keeps its place in the block's order.
    node_ptr_t res = makeExec(false, true);

    if (dataNodes->size() == 0) {
        // `export let inf: float`: no right side. The value is the one the C++ module of the
        // declaration registers under the name (design principles 6.4); the declaration is its
        // type, and the graph builder reads the module.
        if (refs.size() != 1 || typeNodes->size() != 1 || isVar) {
            diags_->of(SemanticDiag::TupleUnpackingCountMismatch)
                .at(ast->load()->tokenRange())
                .commit();
            throw BuildAbortException();
        }
        if (!validateIdent(refs[0].ident())) {
            diags_->of(SemanticDiag::ReservedIdentifier).at(dataDeclLoad->tokenRange()).commit();
            throw BuildAbortException();
        }
        Type *declaredType  = visitType(typeNodes->atAs<AST::TypeLoad>(0));
        node_ptr_t nRefNode = tree_->makeNRef(refs[0].toString());
        tree_->markNativeValue(nRefNode, declaredType);
        setOriginFromOrigin(
            context_,
            nRefNode,
            findBindingOrigin(context_, ast, 0),
            camel::source::OriginKind::GctNode,
            "gct.nref");
        LEAVE("DataDecl");
        return nRefNode;
    }

    // The declaration binds one name, several names, or the elements of a tuple (the
    // parenthesized carrier). A struct or an array carrier is not in the language: the
    // parser refuses it, so no declaration reaches here with one.
    if (refs.size() == dataNodes->size()) {
        for (size_t i = 0; i < refs.size(); ++i) {
            const auto &ref         = refs[i];
            const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(i);
            // The declared type first: an array literal takes the form of its elements
            // from it (visitDataInto).
            Type *declaredType = nullptr;
            if (i < typeNodes->size()) {
                declaredType = visitType(typeNodes->atAs<AST::TypeLoad>(i));
            }
            bool enteredLiteral = false;
            node_ptr_t dataNode = declaredType != nullptr
                                      ? visitDataInto(dataASTNode, declaredType, enteredLiteral)
                                      : visitData(dataASTNode);

            // Validate the identifier
            if (!validateIdent(ref.ident())) {
                diags_->of(SemanticDiag::ReservedIdentifier)
                    .at(dataDeclLoad->tokenRange())
                    .commit();
                throw BuildAbortException();
            }

            if (i < typeNodes->size() && !enteredLiteral) {
                dataNode = declaredValue(dataNode, declaredType, ast);
            }

            // Ensure the data node is of type Data
            GUARD(
                dataASTNode->type() == AST::LoadType::Data,
                "A declaration requires Data type nodes");
            // Create a reference node and link the data node to it. A value of a
            // namespace block is named with its namespaces (`Config::limit`).
            node_ptr_t nRefNode = tree_->makeNRef(ref.toString());
            *nRefNode << dataNode;
            setOriginFromOrigin(
                context_,
                nRefNode,
                findBindingOrigin(context_, ast, i),
                camel::source::OriginKind::GctNode,
                "gct.nref");
            if (refs.size() == 1) {
                res = nRefNode;
            } else {
                // `let a, b = 1, 2` binds every name, not only the last one.
                *res << nRefNode;
            }
        }
    } else {
        if (dataNodes->size() == 1) {
            // If there is only one data node, process it as a reference and access node
            const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(0);
            node_ptr_t dataNode     = visitData(dataASTNode);

            if (typeNodes->size() > 0) {
                const auto &typeASTNode = typeNodes->atAs<AST::TypeLoad>(0);
                Type *type              = nullptr;
                {
                    type = visitType(typeASTNode);
                }
                dataNode = declaredValue(dataNode, type, ast);
            }

            const string id     = std::to_string(idIndex_++);
            node_ptr_t nRefNode = tree_->makeNRef(id);
            node_ptr_t dRefNode = tree_->makeDRef(id);
            *nRefNode << dataNode;
            *res << nRefNode;
            for (size_t i = 0; i < refs.size(); ++i) {
                const auto &ref = refs[i];
                if (!validateIdent(ref.ident())) {
                    diags_->of(SemanticDiag::ReservedIdentifier)
                        .at(dataDeclLoad->tokenRange())
                        .commit();
                    throw BuildAbortException();
                }
                node_ptr_t nRefNode = tree_->makeNRef(ref.toString());
                node_ptr_t accsNode = tree_->makeAccs(i);
                *accsNode << tree_->clone(dRefNode);
                // How many names this declaration wrote, so the graph builder can refuse a count
                // the value's tuple cannot meet -- in either direction -- at the declaration, and
                // not at an element that happens to be past the end.
                tree_->markUnpack(accsNode, refs.size());
                setOriginFromAst(
                    context_,
                    accsNode,
                    ast,
                    camel::source::OriginKind::GctNode,
                    "gct.accs");
                *nRefNode << accsNode;
                setOriginFromOrigin(
                    context_,
                    nRefNode,
                    findBindingOrigin(context_, ast, i),
                    camel::source::OriginKind::GctNode,
                    "gct.nref");
                *res << nRefNode;
            }
        } else {
            // If the number of data nodes does not match, log diagnostics and throw an
            // exception
            diags_->of(SemanticDiag::TupleUnpackingCountMismatch)
                .at(ast->load()->tokenRange())
                .commit();
            throw BuildAbortException();
        }
    }

    // If it's a variable declaration, create a variable node and link the result node to it
    if (isVar) {
        node_ptr_t variNode = tree_->makeVari();
        *variNode << res;
        setOriginFromAst(context_, variNode, ast, camel::source::OriginKind::GctNode, "gct.var");
        registerGctSemantic(
            context_,
            variNode,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::BindingName,
                    findSemanticOrigin(
                        astSemantic(context_, ast),
                        camel::source::SemanticRole::BindingName),
                    -1,
                    "binding"),
                semanticPart(
                    camel::source::SemanticRole::ValueProducer,
                    nodeOrigin(res),
                    -1,
                    "initializer"),
            },
            {nodeOrigin(res)});
        res = variNode;
    }

    LEAVE("DataDecl");
    return res;
}

void Builder::checkOperatorDeclaration(
    const std::string &symbol, const AST::node_ptr_t &funcData, const AST::node_ptr_t &decl) {
    const auto at = decl->load()->tokenRange();
    if (const auto reason = camel::core::whyNotOverloadable(symbol)) {
        diags_->of(SemanticDiag::OperatorNotOverloadable).at(at).commit(symbol, *reason);
        throw BuildAbortException();
    }
    // FuncData: FuncType (with-parameters, parameters, return type), then the body.
    const auto &funcType   = funcData->atAs<AST::FuncTypeLoad>(0);
    const auto &withParams = funcType->atAs<AST::RepeatedLoad>(0);
    const auto &normParams = funcType->atAs<AST::RepeatedLoad>(1);
    if (!camel::core::overloadableOperatorGroup(symbol, normParams->size())) {
        diags_->of(SemanticDiag::OperatorOverloadInvalid)
            .at(at)
            .commit(
                symbol,
                std::format(
                    "it takes {}, and this declaration has {}",
                    camel::core::arityOf(symbol),
                    normParams->size()));
        throw BuildAbortException();
    }
    // `a + b` has nowhere to give a with-argument, and does not write its operands.
    if (withParams->size() > 0) {
        diags_->of(SemanticDiag::OperatorOverloadInvalid)
            .at(at)
            .commit(
                symbol,
                "an operator has no with-parameters: its operands are all it is given (a "
                "type parameter is declared after the operator's name, 'func operator "
                "+::<T>(...)')");
        throw BuildAbortException();
    }
    for (const auto &param : *normParams) {
        if (param->loadAs<AST::NamedPairLoad>()->isVar()) {
            diags_->of(SemanticDiag::OperatorOverloadInvalid)
                .at(param->load()->tokenRange())
                .commit(symbol, "an operator does not change its operands: no var parameters");
            throw BuildAbortException();
        }
    }
    // `for x in c` reads what the operator answers round by round, so what it answers is a
    // stream (design principles 5.4): `func operator for(r: Range): int..`. A `for` that answers
    // a value has no reading in a loop, and a loop that would use it cannot be the place that
    // finds out.
    if (symbol == "for") {
        const AST::node_ptr_t returned = funcType->optAtAs<AST::TypeLoad>(2);
        if (returned == nullptr ||
            returned->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Stream) {
            diags_->of(SemanticDiag::OperatorOverloadInvalid)
                .at(at)
                .commit(
                    symbol,
                    "what it answers is what the loop reads round by round, so its return type is "
                    "a stream ('func operator for(r: Range): int..')");
            throw BuildAbortException();
        }
    }
}

/*
FuncDecl(Ref ref) : FuncAnno* anno, FuncData ;
*/
/*
tagDecl : 'export'? 'tag' identDef typeParams? parentParams? 'on' tagPositions
            ('read_by' tagReaders)? whenClause?
        | 'export'? 'tag' identDef '=' bracketIdents ;
The tag is registered here, before any type is declared (visitModule), so that a `#` use reads
it. It lowers to no node: a tag is a compile-time name, not a value of the program (design
principles 6.2).
*/
void Builder::visitTagDecl(const AST::node_ptr_t &ast) {
    ENTER("TagDecl");
    const auto decl = ast->loadAs<AST::TagDeclLoad>();
    camel::core::tag::Decl tag;
    tag.module = module_->name();
    tag.ident  = decl->ref().toString();
    // A program may not declare a tag with the name of a core permission or of `strict`:
    // `#reassociate` in the program would still mean the core's permission, so the declaration
    // would never be read (design principles 6.2, 7.4).
    if (tag.ident == "finite_math" || tag.ident == "strict" ||
        camel::core::sem::parsePermission(tag.ident).has_value()) {
        diags_->of(SemanticDiag::TagShadowsPermission)
            .at(ast->load()->tokenRange())
            .commit(tag.ident);
    }
    tag.name = tag.module.empty() ? tag.ident : tag.module + "::" + tag.ident;
    tag.on   = static_cast<camel::core::tag::Positions>(decl->on());
    for (const auto &param : decl->params()) {
        tag.params.push_back({param.name, param.type});
    }
    tag.typeParams = decl->typeParams();
    tag.when       = decl->when();
    for (const auto &alias : decl->aliases()) {
        tag.aliases.push_back(alias);
    }
    for (const auto &reader : decl->readBy()) {
        tag.readBy.push_back(reader.toString());
    }
    const auto id = camel::core::tag::declare(std::move(tag));
    module_->addDeclaredTag(decl->ref().toString(), id);
    LEAVE("TagDecl");
}

namespace {
/// The text of a resource name as a diagnostic shows it: a resource of the module it is
/// declared in is written with that module (`fs::handle`), a standard one as itself.
std::string resourceCycleText(const std::vector<std::string> &chain, const std::string &closing) {
    std::string text;
    for (const std::string &step : chain) {
        text += (text.empty() ? "" : " -> ") + step;
    }
    text += (text.empty() ? "" : " -> ") + closing;
    return text;
}
} // namespace

node_ptr_t Builder::visitResourceDecl(const AST::node_ptr_t &ast) {
    ENTER("ResourceDecl");
    const auto decl = ast->loadAs<AST::ResourceDeclLoad>();
    camel::core::module::Module::ResourceDecl resource;
    resource.unordered = decl->unordered();
    resource.readonly  = decl->readonly();
    for (const Reference &member : decl->members()) {
        resource.members.push_back(member);
    }
    const std::string name = decl->ref().toString();
    if (module_->resource(decl->ref()) != nullptr) {
        // One name, one resource (design principles 10.2: a name is declared once in a scope).
        diags_->of(SemanticDiag::Redeclaration).at(ast->load()->tokenRange()).commit(name);
        throw BuildAbortException();
    }
    module_->declareResource(decl->ref(), std::move(resource));
    if (exportedRefs_.contains(name)) {
        module_->setResourceExported(decl->ref());
    }
    resourceDecls_[name] = ast;
    LEAVE("ResourceDecl");
    return nullptr;
}

void Builder::checkResourceGroups() {
    for (const auto &[name, ast] : resourceDecls_) {
        const auto *decl = module_->resource(Reference(name));
        if (decl == nullptr || decl->members.empty()) {
            continue;
        }
        std::vector<AmbientEntry> expanded;
        std::vector<std::string> active;
        std::string failed;
        if (!resolveAmbientResource(module_, Reference(name), expanded, active, &failed, ast)) {
            diags_->of(SemanticDiag::UnknownResource)
                .at(ast->load()->tokenRange())
                .commit(failed, name);
            throw BuildAbortException();
        }
    }
}

bool Builder::resolveAmbientResource(
    const camel::core::module::module_ptr_t &in, const Reference &name,
    std::vector<AmbientEntry> &out, std::vector<std::string> &active, std::string *failed,
    const AST::node_ptr_t &where) {
    const std::string written = name.toString();
    const std::string key     = (in ? in->name() : std::string()) + "::" + written;
    if (std::find(active.begin(), active.end(), key) != active.end()) {
        diags_->of(SemanticDiag::ResourceGroupCycle)
            .at(where->load()->tokenRange())
            .commit(written, resourceCycleText(active, key));
        throw BuildAbortException();
    }
    struct ActiveGuard {
        std::vector<std::string> &active;
        ~ActiveGuard() { active.pop_back(); }
    } guard{active};
    active.push_back(key);
    if (in) {
        if (const auto *decl = in->resource(name)) {
            if (in.get() == module_.get() || decl->exported) {
                if (decl->members.empty()) {
                    // A resource of a module is named with the module, as an operator of one is
                    // (design principles 10.2): `fs::handle` is one thing, wherever it is used.
                    // A name shown for a resource of this module leaves the module out, since
                    // there is nothing to tell it apart from; the module the resource is declared
                    // in is half of its identity, and that is what the operator's own declaration
                    // and this clause are compared as (see resourceIdentities).
                    out.push_back(
                        AmbientEntry{
                            .key = in.get() == module_.get() && !decl->variable
                                       ? written
                                       : in->name() + (decl->variable ? ":" : "::") + written,
                            // A `resource` declaration is state outside the program; a module's
                            // `var` is a cell only the program observes (design principles 3.3).
                            .external = !decl->variable,
                            // `resource net unordered`: the effects on it keep no written order.
                            .unordered = decl->unordered,
                            // `resource graph readonly`: no clause may write it (design principles
                            // 9.12, 15.5), which resourceSignatureOf refuses.
                            .readonly = decl->readonly});
                    return true;
                }
                for (const Reference &member : decl->members) {
                    if (!resolveAmbientResource(in, member, out, active, failed, where)) {
                        return false;
                    }
                }
                return true;
            }
        }
    }
    if (sem::isStandardResource(written)) {
        out.push_back(AmbientEntry{.key = written, .external = true});
        return true;
    }
    if (in) {
        // A name imported by name (`import { r } from m`), then a module prefix (`m::r`).
        if (auto source = in->importedSource(name)) {
            return resolveAmbientResource(
                source->first,
                source->second,
                out,
                active,
                failed,
                where);
        }
        for (const auto &[prefix, mod] : in->importedNamespaces()) {
            if (written.size() > prefix.size() + 2 &&
                written.compare(0, prefix.size(), prefix) == 0 &&
                written.compare(prefix.size(), 2, "::") == 0) {
                return resolveAmbientResource(
                    mod,
                    Reference(written.substr(prefix.size() + 2)),
                    out,
                    active,
                    failed,
                    where);
            }
        }
    }
    if (failed != nullptr) {
        *failed = written;
    }
    return false;
}

sem::ResourceSignature
Builder::resourceSignatureOf(const AST::node_ptr_t &funcTypeAst, const FunctionType *built) {
    sem::ResourceSignature signature;
    const AST::ResourceClauseLoad *clause = nullptr;
    if (funcTypeAst != nullptr && funcTypeAst->size() > 3) {
        const AST::node_ptr_t &clauseNode = funcTypeAst->at(3);
        if (clauseNode != nullptr && clauseNode->load()->type() == AST::LoadType::ResourceClause) {
            clause = clauseNode->loadAs<AST::ResourceClauseLoad>().get();
        }
    }
    if (clause == nullptr || built == nullptr) {
        // No clause: a type that promises nothing about resources (Generic).
        return signature;
    }
    signature.spelling = clause->spelling();
    // A clause that writes a capability and nothing else (`f! spelling`, `f(): T panic`) declares
    // that capability without promising anything about the rest: the same reading as a declaration
    // that writes no clause at all, which promises nothing (Generic). Otherwise the word could only
    // be written beside a full account of the body's resources -- and a pass that reads the
    // spelling and does anything else could not declare it (design principles 9.5), while `panic`
    // would claim the empty resource set (`pure`) merely by being written. A fault statement is not
    // a promise about effects: an operator that faults may still write stdout (design principles
    // 3.9), and the resources a declaration publishes stay the operator's own (design effects plan
    // 2.2).
    if ((clause->spelling() || clause->panic()) && !clause->pure() && clause->uses().empty()) {
        return signature;
    }
    signature.mode = sem::ResourceSignature::Mode::Concrete;
    if (clause->pure() && !clause->uses().empty()) {
        diags_->of(SemanticDiag::PureAndUses).at(clause->tokenRange()).commit(built->toString());
        throw BuildAbortException();
    }
    if (clause->pure()) {
        // `pure` promises the empty set (design effects plan 4.3, step 5). A `var` parameter is a
        // resource by itself: the cell is shared with the caller, so a call through the parameter
        // reads and writes that cell wherever the body is written, and no body can bring the set
        // back to empty. So a signature that writes `pure` and gives a parameter `var` is refused
        // where it is written, and the note points at the parameter that breaks the promise (the
        // error is at the declaration's clause, and the note at the first use the
        // signature does not promise).
        const auto varRangeOf =
            [&](const std::string &name) -> std::optional<camel::core::error::TokenRange> {
            for (size_t list = 0; list < 2; ++list) {
                const auto &params = funcTypeAst->atAs<AST::RepeatedLoad>(list);
                for (size_t pi = 0; pi < params->size(); ++pi) {
                    const auto &pair = (*params)[pi];
                    if (pair == nullptr) {
                        continue;
                    }
                    const auto &load = pair->loadAs<AST::NamedPairLoad>();
                    if (load->isVar() && load->getRef().ident() == name) {
                        return load->tokenRange();
                    }
                }
            }
            return std::nullopt;
        };
        const auto varNameAt = [&](size_t index) -> std::string {
            // The parameter numbering a call uses: `with` parameters first, ordinary ones after
            // them (the same numbering resourceSignatureOf uses below).
            const size_t withCount = built->withTypesCount();
            if (index < withCount) {
                return built->withIsVarAt(index) ? std::string(built->argNameAt(index))
                                                 : std::string();
            }
            const size_t norm = index - withCount;
            return built->normIsVarAt(norm) ? std::string(built->argNameAt(index)) : std::string();
        };
        for (size_t i = 0; i < built->withTypesCount() + built->normTypesCount(); ++i) {
            const std::string name = varNameAt(i);
            if (name.empty()) {
                continue;
            }
            // The builder is kept by value: `of` hands out a temporary and the chain returns a
            // reference to it, so a reference held across statements would outlive the builder.
            auto diag = diags_->of(SemanticDiag::UsesPromiseViolated).at(clause->tokenRange());
            if (const auto range = varRangeOf(name); range.has_value()) {
                diag.related(
                    *range,
                    "this parameter is a resource: a call through it writes the caller's cell, so "
                    "the call touches it wherever the body is written");
            }
            diag.commit(built->toString(), "the 'var' parameter '" + name + "'");
            throw BuildAbortException();
        }
        return signature;
    }
    // The parameters the clause may name, in the numbering a call uses: with parameters first,
    // ordinary ones after them; the `constas` parameter is not a runtime argument and is
    // dropped from that numbering (builder_generics.cpp: signatureIndexAt).
    struct Parameter {
        std::string name;
        uint8_t index = 0;
        /// The parameter's place in the numbering a call uses, before it is narrowed to the one
        /// byte a signature entry holds. A parameter past the 256th has no entry a signature can
        /// carry, which is what the measurement gives up on (builder_resources.cpp:
        /// addVarParameters).
        size_t runtime = 0;
        Type *type     = nullptr;
        bool isVar     = false;
    };
    std::vector<Parameter> parameters;
    const size_t withCount = built->withTypesCount();
    const size_t normCount = built->normTypesCount();
    size_t runtimeIndex    = 0;
    for (size_t i = 0; i < withCount + normCount; ++i) {
        const bool isWith = i < withCount;
        if (isWith ? built->withIsConstasAt(i) : built->normIsConstasAt(i - withCount)) {
            continue;
        }
        Parameter param;
        param.name    = std::string(built->argNameAt(i));
        param.index   = static_cast<uint8_t>(runtimeIndex);
        param.runtime = runtimeIndex++;
        param.type    = isWith ? built->withTypeAt(i) : built->normTypeAt(i - withCount);
        param.isVar   = isWith ? built->withIsVarAt(i) : built->normIsVarAt(i - withCount);
        parameters.push_back(std::move(param));
    }
    std::vector<AmbientEntry> ambient;
    std::vector<sem::ResourceSignature::AmbientUse> ambientUses;
    std::vector<std::string> active;
    std::string failed;
    for (const AST::ResourceClauseLoad::Uses &use : clause->uses()) {
        const std::string written = use.resource.toString();
        const auto param =
            std::find_if(parameters.begin(), parameters.end(), [&](const Parameter &candidate) {
                return use.resource.plain() && candidate.name == use.resource.ident();
            });
        if (param != parameters.end()) {
            // A `var` parameter is a variable, and a clause never names one (design principles
            // 3.3): the `var` in the parameter list is where the cell is declared, and which
            // calls touch it is measured from the body. A handle parameter is the other kind of
            // entry a clause may name -- it says which handle the call is handed.
            if (param->isVar && !sem::isHandleType(param->type)) {
                diags_->of(SemanticDiag::VariableInUses)
                    .at(use.range)
                    .commit(written, clause->geneCode());
                throw BuildAbortException();
            }
            sem::ResourceSignature::ParamUse paramUse;
            paramUse.index = param->index;
            // `var` in a clause says the call writes the resource; without it the call reads the
            // resource and nothing else (design principles 3.3). A write of a handle is its whole
            // value replaced, which is what a reader of the graph sees as a read-modify-write.
            paramUse.mode = use.write ? sem::AccessMode::ReadModifyWrite : sem::AccessMode::Read;
            if (sem::isHandleType(param->type)) {
                paramUse.cls  = sem::ResourceClass::Handle;
                paramUse.name = sem::handleResourceName(param->type);
            } else {
                paramUse.cls = sem::ResourceClass::Object;
            }
            const bool known = std::any_of(
                signature.params.begin(),
                signature.params.end(),
                [&](const auto &other) { return other.index == paramUse.index; });
            if (known) {
                // One parameter is one resource: `uses x, x` says nothing twice.
                continue;
            }
            signature.params.push_back(std::move(paramUse));
            continue;
        }
        if (use.resource.plain() &&
            std::any_of(parameters.begin(), parameters.end(), [&](const Parameter &candidate) {
                return candidate.name == use.resource.ident();
            })) {
            continue;
        }
        ambient.clear();
        if (!resolveAmbientResource(module_, use.resource, ambient, active, &failed, funcTypeAst)) {
            diags_->of(SemanticDiag::UnknownResource)
                .at(clause->tokenRange())
                .commit(written, built->toString());
            throw BuildAbortException();
        }
        for (const AmbientEntry &entry : ambient) {
            // A module's own `var` is a variable of the outermost scope, not a resource a clause
            // names (design principles 3.3): the functions of the module capture it, and which of
            // them read or write it is measured from their bodies. An external resource is what a
            // clause is for, and `var` says the call writes it rather than only reading it.
            if (!entry.external) {
                diags_->of(SemanticDiag::VariableInUses)
                    .at(use.range)
                    .commit(sem::resourceDisplayName(entry.key), clause->geneCode());
                throw BuildAbortException();
            }
            if (entry.readonly && use.write) {
                // A read-only resource is only ever read (design principles 9.12, 15.5): the graph
                // a function is and the spelling of a name are values nothing that runs writes, so
                // no clause can declare a write of one. The refusal names the resource and the
                // modifier that forbids it, so that it reads as the declaration's answer and not
                // as the clause's.
                diags_->of(SemanticDiag::ReadonlyResourceWritten)
                    .at(use.range)
                    .commit(sem::resourceDisplayName(entry.key), clause->geneCode());
                throw BuildAbortException();
            }
            const auto known =
                std::find_if(ambientUses.begin(), ambientUses.end(), [&](const auto &other) {
                    return other.name == entry.key;
                });
            if (known != ambientUses.end()) {
                // One ambient resource is one use: `uses fs, var fs` says nothing twice.
                known->mode = sem::uniteAccess(
                    known->mode,
                    use.write ? sem::AccessMode::ReadModifyWrite : sem::AccessMode::Read);
                known->unordered = known->unordered || entry.unordered;
                continue;
            }
            ambientUses.push_back(
                sem::ResourceSignature::AmbientUse{
                    .name = entry.key,
                    .mode = use.write ? sem::AccessMode::ReadModifyWrite : sem::AccessMode::Read,
                    .unordered = entry.unordered});
        }
    }
    std::sort(signature.params.begin(), signature.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    std::sort(ambientUses.begin(), ambientUses.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    signature.ambient = std::move(ambientUses);
    if (clause->pure()) {
        signature.params.clear();
        signature.ambient.clear();
    } else {
        // A `var` parameter is a resource by itself -- the caller's cell, which the call writes --
        // and the parameter list is where the declaration says so (design principles 3.3). The
        // body's measurement carries every `var` parameter whether its body reaches it or not, and
        // a caller reads this signature to know what calling it touches, so the declaration adds
        // them too. This is the one place they are written: a clause never names a variable.
        for (const Parameter &param : parameters) {
            const bool known = std::any_of(
                signature.params.begin(),
                signature.params.end(),
                [&](const auto &other) { return other.index == param.index; });
            if (!param.isVar || known || param.runtime > 0xFF) {
                continue;
            }
            sem::ResourceSignature::ParamUse paramUse;
            paramUse.index = param.index;
            paramUse.cls   = sem::ResourceClass::Variable;
            paramUse.mode  = sem::AccessMode::ReadModifyWrite;
            signature.params.push_back(std::move(paramUse));
        }
        std::sort(
            signature.params.begin(),
            signature.params.end(),
            [](const auto &a, const auto &b) { return a.index < b.index; });
    }
    // The resources the clause wrote are what it promises, and a resource the program does not own
    // brings its effect on the world with it (design principles 3.3): a clause that names one
    // covers the body that reaches it, and one that leaves it out does not.
    sem::ambientCarriesExternal(signature);
    return signature;
}

std::vector<GCT::TagUse> Builder::tagUsesOf(const AST::node_ptr_t &ast) {
    std::vector<GCT::TagUse> uses;
    if (ast == nullptr) {
        return uses;
    }
    for (size_t i = 0; i < ast->size(); ++i) {
        const auto &child = ast->at(i);
        if (child->type() != AST::LoadType::Repeated ||
            child->loadAs<AST::RepeatedLoad>()->nodeName() != "TagUse") {
            continue;
        }
        for (const auto &useAst : *child) {
            GCT::TagUse use;
            use.name   = useAst->loadAs<AST::TagUseLoad>()->name().toString();
            use.origin = useAst->load()->origin();
            for (size_t a = 0; a < useAst->size(); ++a) {
                // The uses are attached as a list of `Data` lists (one per use); the arguments
                // are compile-time values, lowered like any other expression. The GIR builder
                // keeps the constant ones and reports the others.
                const AST::node_ptr_t &dataList = useAst->at(a);
                for (const auto &argument : *dataList) {
                    use.args.push_back(visitData(argument));
                }
            }
            uses.push_back(std::move(use));
        }
    }
    return uses;
}

void Builder::attachTagUses(const GCT::node_ptr_t &gct, const AST::node_ptr_t &ast) {
    if (gct == nullptr) {
        return;
    }
    tree_->addTagUses(gct, tagUsesOf(ast));
}

node_ptr_t Builder::visitFuncDecl(const AST::node_ptr_t &ast) {
    ENTER("FuncDecl");
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for FuncDecl");
    GUARD(ast->size() >= 1, "FuncDecl AST should have at least one child.");

    const auto &funcDataNode = ast->atAs<AST::FuncDataLoad>(ast->size() - 1);
    const auto &declLoad     = ast->loadAs<AST::FuncDeclLoad>();
    if (!declLoad->operatorSymbol().empty()) {
        // `func operator +(a: V, b: V): V` (design principles 4.3): the function of the
        // operator's reserved name, which the parser and the AST builder chose.
        checkOperatorDeclaration(declLoad->operatorSymbol(), funcDataNode, ast);
    } else if (!validateIdent(funcDataNode->loadAs<AST::FuncDataLoad>()->ref().ident())) {
        // The names of the form `__x__` are the compiler's: the operators are functions of
        // such names (`a + b` is `__add__(a, b)`), and code the compiler writes itself refers
        // to them (`t += u` on a tensor, the tag operators of `??`). A `let` could not take
        // one; a function could, and `func __add__` then replaced `+` in the whole module. A
        // program overloads an operator with `func operator +` instead.
        diags_->of(SemanticDiag::ReservedIdentifier).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    }
    funcDeclPending_    = true;
    node_ptr_t funcNode = visitFuncData(funcDataNode);
    if (declLoad->ruleBinding()) {
        // The function a rule with `with` parameters is written as: a use of its name as a value
        // is the call that binds them and gives the `Rule`, not a function value (design
        // principles 5.5). The graph builder reads the mark from the FUNC node.
        funcNode->setRuleBinding();
    }

    // Decorator annotations are lowered to ANNO nodes attached after FUNC payload.
    // Each ANNO node owns one lowered GCT expression (currently lowered from funcAnno -> Data).
    // The tag uses written before the declaration are in a repeated child of their own; they
    // are kept beside the node (Tree::addTagUses), not in it.
    for (size_t i = 0; i + 1 < ast->size(); ++i) {
        const auto &child = ast->at(i);
        if (child->type() != AST::LoadType::Repeated ||
            child->loadAs<AST::RepeatedLoad>()->nodeName() != "FuncAnno") {
            continue;
        }
        for (const auto &annoAst : *child) {
            GUARD(annoAst->type() == AST::LoadType::Data, "FuncAnno should lower to AST Data.");
            node_ptr_t annoExprNode = visitData(annoAst);
            node_ptr_t annoNode     = tree_->makeAnno();
            *annoNode << annoExprNode;
            *funcNode << annoNode;
        }
    }
    attachTagUses(funcNode, ast);

    LEAVE("FuncDecl");
    return funcNode;
}

/*
TypeDecl(Ref ref, string uri) : Type? type ;
*/
namespace {

/// A mention of the type being declared, and whether it is held directly.
struct TypeMention {
    bool any      = false; // the type names itself somewhere in its right-hand side
    bool directly = false; // ... in a place a value of it would be held in, not behind `[]`
    // ... behind a map, a set or another non-array collection: legal by 5.2, but only the array
    // case has a representation today.
    bool viaOtherCollection = false;
};

void findTypeMention(
    const AST::node_ptr_t &ast, const std::string &name, bool inCollection, bool inArray,
    TypeMention &out) {
    if (ast == nullptr || out.directly) {
        return;
    }
    if (ast->type() == AST::LoadType::Type) {
        const AST::TypeType typeType = ast->loadAs<AST::TypeLoad>()->typeType();
        if (typeType == AST::TypeType::Ref &&
            ast->loadAs<AST::RefTypeLoad>()->ref().toString() == name) {
            out.any = true;
            out.directly |= !inCollection;
            out.viaOtherCollection |= inCollection && !inArray;
            return;
        }
        // An array or another collection type holds its elements by reference, which is what
        // lets a type contain itself (design principles 5.2). Anything else holds its parts in
        // the value.
        const bool indirect =
            inCollection || typeType == AST::TypeType::Array || typeType == AST::TypeType::Spec;
        // `Node[]` is the case with a representation (a slot array); a map or a set is not
        // handled yet, so its mentions are told apart here.
        const bool intoArray = inArray || typeType == AST::TypeType::Array;
        for (size_t i = 0; i < ast->size(); ++i) {
            findTypeMention(ast->at(i), name, indirect, intoArray, out);
        }
        return;
    }
    for (size_t i = 0; i < ast->size(); ++i) {
        findTypeMention(ast->at(i), name, inCollection, inArray, out);
    }
}

} // namespace

node_ptr_t Builder::visitTypeDecl(const AST::node_ptr_t &ast) {
    ENTER("TypeDecl");
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for TypeDecl");
    const auto &typeNode = ast->optAtAs<AST::TypeLoad>(0);
    Type *type;
    const auto &typeLoad = ast->loadAs<AST::TypeDeclLoad>();
    // A declaration in error declares nothing; uses of the name follow from that error.
    struct PoisonOnFailure {
        Builder &self;
        std::string name;
        int exceptions = std::uncaught_exceptions();
        ~PoisonOnFailure() {
            if (std::uncaught_exceptions() > exceptions) {
                self.poisonedTypes_.insert(name);
            }
        }
    } poisonOnFailure{*this, typeLoad->ref().toString()};
    // In progress until it is declared, for a module whose types this one needs and which
    // needs this one back (rejectTypeCycle).
    struct InProgress {
        camel::core::context::Context &context;
        InProgress(
            camel::core::context::Context &ctx, const camel::core::module::Module *mod,
            const std::string &name)
            : context(ctx) {
            context.enterTypeDeclaration(mod, name);
        }
        ~InProgress() { context.leaveTypeDeclaration(); }
    } inProgress{*context_, module_.get(), typeLoad->ref().toString()};
    // The declaration's node is made first: the tags written on its fields are recorded on it
    // while the type is lowered (visitStructType).
    node_ptr_t declNode = tree_->makeDecl(typeLoad->ref(), false);
    struct FieldTagOwner {
        Builder &self;
        node_ptr_t outer;
        FieldTagOwner(Builder &b, node_ptr_t owner)
            : self(b), outer(std::exchange(b.fieldTagOwner_, owner)) {}
        ~FieldTagOwner() { self.fieldTagOwner_ = outer; }
    } fieldTagOwner(*this, declNode);
    if (!typeNode && !typeLoad->params().empty() && module_->name() == "types" &&
        TypeCallType::isTypeFunction(typeLoad->ref().toString())) {
        // `export type PayloadOf<T>`: a primitive type function (design principles 5.6),
        // declared here without a body; the compiler answers it, because what it answers is a field
        // of the type that a `match type` case cannot name. A type function that only takes a type
        // apart or puts one back together is written in the library as a `match type` instead.
        const string typeName = typeLoad->ref().toString();
        struct TypeScopeGuard {
            Builder &self;
            scope_ptr_t<Reference, Type *> outer;
            ~TypeScopeGuard() { self.typeScope_ = outer; }
        } typeScopeGuard{*this, typeScope_};
        pushScope();
        const std::vector<Type *> parameters = bindTypeParameters(typeLoad.get(), ast, typeName);
        type                                 = TypeFunctionType::create(
            typeName,
            typeLoad->params(),
            TypeCallType::create(typeName, parameters));
    } else if (!typeNode) {
        // `export type Graph`, `export type Txn<F>`: the type is provided by the C++ module of
        // the same name, under this name (design principles 10.4), like a function without a
        // body. Its identity is the module and the name.
        type = camel::core::type::findNativeType(module_->name(), typeLoad->ref().toString());
        if (type == nullptr) {
            diags_->of(SemanticDiag::NativeTypeNotProvided)
                .at(ast->load()->tokenRange())
                .commit(typeLoad->ref().toString(), module_->name());
            throw BuildAbortException();
        }
    } else if (typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Enum) {
        // The name is in scope before the variants are read, so that a variant field can hold
        // the enum itself (`type Tree = enum { Node(Tree, Tree) }`).
        type = declareEnumType(typeNode, typeLoad.get());
    } else if (typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Struct) {
        // `type Name<params> = { field: T, ... }` declares a named struct type (design
        // principles 5.2). A name used where a value of a struct type is expected declares
        // nothing: `type Alias = Other` stays an alias for the type `Other` (structural).
        if (typeLoad->params().empty()) {
            // A type that mentions itself where a value of it would have to be held: a cycle
            // that can never be built (design principles 5.2). Recursion through an array, a map,
            // a set or an enum is admitted by the rule and needs a shape this compiler does not
            // have, so it is named rather than left unresolved.
            TypeMention mention;
            findTypeMention(typeNode, typeLoad->ref().toString(), false, false, mention);
            if (mention.any) {
                const std::string name = typeLoad->ref().toString();
                if (mention.directly) {
                    diags_->of(SemanticDiag::ModuleTypeCycle)
                        .at(ast->load()->tokenRange())
                        .commit(name, name + " -> " + name);
                    throw BuildAbortException();
                }
                if (mention.viaOtherCollection) {
                    diags_->of(SemanticDiag::TypeAliasRecursionUnrepresented)
                        .at(ast->load()->tokenRange())
                        .commit(name);
                    throw BuildAbortException();
                }
                // Through an array: declareStructType puts the name in scope before the fields
                // are read, so `type Node = { children: Node[] }` denotes one type object.
            }
        }
        type = declareStructType(typeNode, typeLoad.get());
    } else if (typeLoad->params().empty()) {
        // A type that mentions itself where a value of it would have to be held: a cycle that
        // can never be built. Only an enum is declared with its name in scope, so any other
        // self-reference is reported here, with the chain that closes it (design principles
        // 5.2: recursion is through an array, a map, a set or an enum).
        TypeMention mention;
        findTypeMention(typeNode, typeLoad->ref().toString(), false, false, mention);
        if (mention.any) {
            const std::string name = typeLoad->ref().toString();
            if (!mention.directly) {
                // A recursion the rule admits, whose shape this compiler does not have: the
                // diagnostic names the rule and what to write instead of leaving the name
                // unresolved.
                diags_->of(SemanticDiag::TypeAliasRecursionUnrepresented)
                    .at(ast->load()->tokenRange())
                    .commit(name);
            } else {
                diags_->of(SemanticDiag::ModuleTypeCycle)
                    .at(ast->load()->tokenRange())
                    .commit(name, name + " -> " + name);
            }
            throw BuildAbortException();
        }
        type = visitType(typeNode);
    } else {
        // A type function (`type Pair<T> = (T, T)`): the right-hand side is an ordinary type
        // expression in which the parameters are type variables. A use `Pair<int>` replaces
        // them (visitSpecType); nothing of the declaration reaches a graph.
        const string typeName = typeLoad->ref().toString();
        struct TypeScopeGuard {
            Builder &self;
            scope_ptr_t<Reference, Type *> outer;
            ~TypeScopeGuard() { self.typeScope_ = outer; }
        } typeScopeGuard{*this, typeScope_};
        pushScope();
        bindTypeParameters(typeLoad.get(), ast, typeName);
        type = TypeFunctionType::create(typeName, typeLoad->params(), visitType(typeNode));
    }
    // A program's own `Map` or `Set` takes the place of the built-in collection type of that
    // name: programs written before those existed keep their meaning.
    if (const auto builtin = typeScope_->get(typeLoad->ref(), false);
        builtin.has_value() && isCollectionPrototype(builtin.value())) {
        typeScope_->erase(typeLoad->ref(), false);
    }
    // `#resource` (design effects plan 3.1, 3.2): a value of the type is a handle, one resource
    // per value, named by the declaration. The tag is the only spelling. The type's own flag is
    // what a signature reads (isHandleType), and the tag still reaches the declaration facts in
    // GIR, where the standard tag machinery attaches it (attachTagUses, below).
    //
    // The uses are read here, not through tagUsesOf: that lowers their arguments and this pass
    // must not lower them twice (the facts are attached at the end of the declaration).
    for (size_t i = 0; i < ast->size(); ++i) {
        const AST::node_ptr_t &child = ast->at(i);
        if (child == nullptr || child->type() != AST::LoadType::Repeated ||
            child->loadAs<AST::RepeatedLoad>()->nodeName() != "TagUse") {
            continue;
        }
        for (const AST::node_ptr_t &useAst : *child) {
            const auto use = useAst->loadAs<AST::TagUseLoad>();
            if (use->name().toString() != "resource") {
                continue;
            }
            bool nominal = false;
            if (type->code() == TypeCode::Struct) {
                const auto *structType = static_cast<camel::core::type::StructType *>(type);
                camel::core::type::StructDecl *decl = structType->decl();
                nominal = decl != nullptr && decl->module() == module_.get() &&
                          decl->name() == typeLoad->ref().toString();
                if (nominal) {
                    decl->setHandle();
                }
            } else if (type->code() == camel::core::type::EnumType::typeCode()) {
                const auto *enumType = static_cast<camel::core::type::EnumType *>(type);
                camel::core::type::EnumDecl *decl = enumType->decl();
                nominal = decl != nullptr && decl->module() == module_.get() &&
                          decl->name() == typeLoad->ref().toString();
                if (nominal) {
                    decl->setHandle();
                }
            }
            if (!nominal) {
                diags_->of(SemanticDiag::HandleTypeNotNominal)
                    .atOrigin(useAst->load()->origin())
                    .commit(typeLoad->ref().toString());
                throw BuildAbortException();
            }
        }
    }
    typeScope_->insert(typeLoad->ref(), type);
    if (auto *recorder = context_ ? context_->semanticRecorder() : nullptr) {
        recorder->typeDeclared(ast->load()->origin(), typeLoad->ref().toString(), type);
    }
    // Where a use of this name is to say it was declared: the language server answers
    // "where is this type declared" from this, in this module and in the modules importing it.
    {
        std::vector<std::string> declared(namespacePath_);
        const auto paths = typeLoad->ref().paths();
        declared.insert(declared.end(), paths.begin(), paths.end());
        declared.push_back(typeLoad->ref().ident());
        module_->noteTypeDeclarationOrigin(Reference(declared), ast->load()->origin());
    }
    if (exportedRefs_.contains(typeLoad->ref().toString())) {
        // Exported at once: a module that imports this one back may need it before this one
        // is through (see visitModule).
        module_->exportType(typeLoad->ref(), type);
    }
    *declNode << tree_->makeType(type);
    // The tags written before the declaration itself (`#tag type P = ...`).
    attachTagUses(declNode, ast);
    LEAVE("TypeDecl");
    return declNode;
}

std::vector<Type *> Builder::bindTypeParameters(
    const AST::TypeDeclLoad *decl, const AST::node_ptr_t &ast, const std::string &name) {
    std::vector<Type *> parameters;
    parameters.reserve(decl->params().size());
    for (const string &param : decl->params()) {
        const Reference paramRef(param);
        if (typeScope_->has(paramRef, false)) {
            diags_->of(SemanticDiag::DuplicateParameter)
                .at(ast->load()->tokenRange())
                .commit(param);
            throw BuildAbortException();
        }
        if (typeScope_->get(paramRef).has_value() ||
            module_->getImportedType(paramRef).has_value()) {
            diags_->of(SemanticDiag::TypeFunctionArguments)
                .at(ast->load()->tokenRange())
                .commit(name, "its parameter '" + param + "' already denotes a type here");
            throw BuildAbortException();
        }
        Type *variable = TypeVarType::create(param, false);
        typeScope_->insert(paramRef, variable);
        parameters.push_back(variable);
    }
    return parameters;
}

Type *Builder::declareEnumType(const AST::node_ptr_t &ast, const AST::TypeDeclLoad *decl) {
    const string name                    = decl->ref().toString();
    const auto enumLoad                  = ast->loadAs<AST::EnumTypeLoad>();
    EnumDecl *declaration                = EnumDecl::create(name, decl->params(), module_.get());
    const bool generic                   = !decl->params().empty();
    scope_ptr_t<Reference, Type *> outer = typeScope_;
    if (generic) {
        pushScope();
        bindTypeParameters(decl, ast, name);
    }
    Type *result;
    if (!generic) {
        // Without parameters the declaration has one instance, which is the type itself; its
        // name denotes it from here on.
        EnumType *instance = declaration->prototype();
        typeScope_->insert(decl->ref(), instance);
        declaration->define(buildEnumVariants(ast, instance));
        result = instance;
    } else {
        // The written field types are those of the instance whose arguments are the parameters
        // themselves; a use `Name<int>` replaces them (as `type Pair<T> = (T, T)` does).
        std::vector<Type *> parameters;
        parameters.reserve(decl->params().size());
        for (const string &param : decl->params()) {
            parameters.push_back(TypeVarType::create(param, false));
        }
        EnumType *body = EnumType::get(declaration, parameters);
        Type *function = TypeFunctionType::create(name, decl->params(), body);
        typeScope_->insert(decl->ref(), function);
        declaration->define(buildEnumVariants(ast, body));
        result = function;
    }
    if (generic) {
        typeScope_ = outer;
    }
    return result;
}

Type *Builder::declareStructType(const AST::node_ptr_t &ast, const AST::TypeDeclLoad *decl) {
    const string name                    = decl->ref().toString();
    const bool generic                   = !decl->params().empty();
    StructDecl *declaration              = StructDecl::create(name, decl->params(), module_.get());
    scope_ptr_t<Reference, Type *> outer = typeScope_;
    std::vector<Type *> parameters;
    if (generic) {
        pushScope();
        parameters = bindTypeParameters(decl, ast, name);
    }
    std::vector<std::pair<std::string, Type *>> fields;
    std::unordered_set<string> fieldNames;
    // The tags written on the fields belong to the type declaration (visitTypeDecl made its
    // node the owner); a named struct type is declared here, not through visitStructType.
    const node_ptr_t fieldTagOwner = std::exchange(fieldTagOwner_, nullptr);
    // A non-generic declaration is read with its own name in scope, so a field may hold the
    // type being declared behind an array (`type Node = { children: Node[] }`, design
    // principles 5.2). The names are collected first, the instance is made from them, and the
    // field types are filled in when they have been read: the one type object is what the name
    // denotes inside the fields and after the declaration.
    StructType *instance = nullptr;
    if (!generic) {
        std::vector<std::string> names;
        names.reserve(ast->atAs<AST::RepeatedLoad>(0)->size());
        for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
            const auto &namedType   = child->loadAs<AST::NamedTypeLoad>();
            const string &fieldName = namedType->getRef().ident();
            if (!fieldNames.insert(fieldName).second) {
                diags_->of(SemanticDiag::DuplicateDictKey)
                    .at(namedType->tokenRange())
                    .commit(fieldName);
                throw BuildAbortException();
            }
            names.push_back(fieldName);
        }
        instance = StructType::declare(declaration, std::span<Type *const>{}, names);
        typeScope_->insert(decl->ref(), instance);
    }
    size_t fieldIndex = 0;
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedType   = child->loadAs<AST::NamedTypeLoad>();
        const string &fieldName = namedType->getRef().ident();
        if (fieldTagOwner != nullptr) {
            tree_->addFieldTagUses(fieldTagOwner, fieldIndex, tagUsesOf(child));
        }
        ++fieldIndex;
        // The fields are read in the order written: that order is the type's, and every
        // instance keeps it (design principles 5.2).
        Type *fieldType = visitType(child->atAs<AST::TypeLoad>(0));
        if (generic && !fieldNames.insert(fieldName).second) {
            diags_->of(SemanticDiag::DuplicateDictKey)
                .at(namedType->tokenRange())
                .commit(fieldName);
            throw BuildAbortException();
        }
        fields.emplace_back(fieldName, fieldType);
    }
    declaration->define(std::move(fields));
    Type *result;
    if (!generic) {
        // Without parameters the declaration has one instance, which is the type itself; its
        // name denotes it from here on (it was already in scope while the fields were read).
        instance->completeFields(declaration->fields());
        result = instance;
    } else {
        // The written field types are those of the instance whose arguments are the parameters
        // themselves; a use `Name<int>` replaces them (as `type Pair<T> = (T, T)` does).
        StructType *body = StructType::get(declaration, parameters);
        Type *function   = TypeFunctionType::create(name, decl->params(), body);
        typeScope_->insert(decl->ref(), function);
        result     = function;
        typeScope_ = outer;
    }
    return result;
}

std::vector<EnumVariant>
Builder::buildEnumVariants(const AST::node_ptr_t &ast, const EnumType *instance) {
    const auto enumLoad = ast->loadAs<AST::EnumTypeLoad>();
    GUARD(
        enumLoad->variants().size() == ast->size(),
        "Every variant of an enum has one node with its field types");
    std::vector<EnumVariant> variants;
    variants.reserve(enumLoad->variants().size());
    std::unordered_set<string> variantNames;
    for (size_t i = 0; i < enumLoad->variants().size(); ++i) {
        const AST::EnumVariantDecl &written = enumLoad->variants()[i];
        EnumVariant variant;
        variant.name  = written.name;
        variant.shape = written.shape == AST::EnumVariantShape::Tuple    ? VariantShape::Tuple
                        : written.shape == AST::EnumVariantShape::Struct ? VariantShape::Struct
                                                                         : VariantShape::Unit;
        const auto fieldTypes = ast->atAs<AST::RepeatedLoad>(i);
        if (!variantNames.insert(variant.name).second) {
            diags_->of(SemanticDiag::DuplicateEnumVariant)
                .at(fieldTypes->load()->tokenRange())
                .commit(variant.name, instance->decl()->name());
            throw BuildAbortException();
        }
        variant.fieldNames = written.fieldNames;
        std::unordered_set<string> fieldNames;
        for (const auto &field : *fieldTypes) {
            variant.fieldTypes.push_back(visitType(field));
            // Where the field's type is written: a field that cannot be a key is reported
            // there (a map key of enum type, design principles 11.3).
            variant.fieldOrigins.push_back(field->load()->origin());
        }
        if (variant.shape == VariantShape::Struct) {
            for (const string &fieldName : variant.fieldNames) {
                if (!fieldNames.insert(fieldName).second) {
                    diags_->of(SemanticDiag::DuplicateEnumField)
                        .at(fieldTypes->load()->tokenRange())
                        .commit(fieldName, variant.name, instance->decl()->name());
                    throw BuildAbortException();
                }
            }
        }
        variants.push_back(std::move(variant));
    }
    return variants;
}

/*
UsingNamespace(Ref ref) ;
*/
node_ptr_t Builder::visitUsingNamespaceDecl(const AST::node_ptr_t &ast) {
    ENTER("UsingNamespaceDecl");
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for UsingNamespace");
    const auto &load = ast->loadAs<AST::UsingNamespaceLoad>();
    // It applies to the block it is written in (visitStmtBlock, visitModule), and what it
    // names is found where a name is used (design principles 10.2); here it is only checked.
    if (!usableNamespace(load->ref())) {
        diags_->of(SemanticDiag::UnresolvedReference)
            .at(ast->load()->tokenRange())
            .commit(load->ref().toString());
        throw BuildAbortException();
    }
    LEAVE("UsingNamespaceDecl");
    return makeExec();
}

} // namespace camel::compile::gct