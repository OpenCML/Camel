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
 * Created: Jul. 09, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"
#include <exception>

#include "camel/compile/semantic_recorder.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/module/userdef.h"
#include "camel/core/operator_names.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"
#include "camel/parse/ast/type.h"
#include "camel/utils/escape.h"
#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gct {

inline camel::source::origin_id_t nodeOrigin(const node_ptr_t &node) {
    return node ? node->origin() : camel::source::kInvalidOriginId;
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

Reference defaultImportNamespace(const std::string &path);

inline std::vector<camel::source::origin_id_t> collectChildOrigins(const node_ptr_t &node) {
    std::vector<camel::source::origin_id_t> origins;
    if (!node) {
        return origins;
    }
    for (const auto &child : *node) {
        auto origin = nodeOrigin(child);
        if (origin != camel::source::kInvalidOriginId) {
            origins.push_back(origin);
        }
    }
    return origins;
}

inline const camel::source::SemanticBundle *
astSemantic(const context_ptr_t &context, const AST::node_ptr_t &ast) {
    if (!context || !ast) {
        return nullptr;
    }
    auto sourceContext = context->sourceContext();
    return sourceContext ? sourceContext->astSemantic(ast->load()->origin()) : nullptr;
}

inline camel::source::origin_id_t findSemanticOrigin(
    const camel::source::SemanticBundle *bundle, camel::source::SemanticRole role,
    int32_t slot = -1) {
    if (!bundle) {
        return camel::source::kInvalidOriginId;
    }
    for (const auto &part : bundle->parts) {
        if (part.role == role && (slot < 0 || part.slot == slot)) {
            return part.origin;
        }
    }
    return camel::source::kInvalidOriginId;
}

inline camel::source::origin_id_t
findBindingOrigin(const context_ptr_t &context, const AST::node_ptr_t &ast, size_t slot) {
    return findSemanticOrigin(
        astSemantic(context, ast),
        camel::source::SemanticRole::BindingName,
        static_cast<int32_t>(slot));
}

inline void setOriginFromOrigin(
    const context_ptr_t &context, const node_ptr_t &node, camel::source::origin_id_t parent,
    camel::source::OriginKind kind = camel::source::OriginKind::GctNode,
    const std::string &label = "gct", bool synthetic = false,
    std::vector<camel::source::origin_id_t> inputs = {}) {
    if (!context || !node || parent == camel::source::kInvalidOriginId) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    node->setOrigin(sourceContext->deriveOrigin(
        parent,
        camel::source::OriginStage::GCT,
        kind,
        label,
        camel::source::kInvalidSpanId,
        synthetic,
        std::move(inputs)));
}

inline void setOriginFromAst(
    const context_ptr_t &context, const node_ptr_t &node, const AST::node_ptr_t &ast,
    camel::source::OriginKind kind = camel::source::OriginKind::GctNode,
    const std::string &label = "gct", bool synthetic = false,
    std::vector<camel::source::origin_id_t> inputs = {}) {
    // GCT is a lossy lowering of AST:
    // an AST node may be rewritten into a different semantic shape, but we still inherit the
    // AST primarySpan along the main derivation chain so diagnostics and later GIR/debugger paths
    // can recover the main source-responsibility span.
    if (!context || !node || !ast) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto astOrigin = ast->load()->origin();
    if (astOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    node->setOrigin(sourceContext->deriveOrigin(
        astOrigin,
        camel::source::OriginStage::GCT,
        kind,
        label,
        camel::source::kInvalidSpanId,
        synthetic,
        std::move(inputs)));
}

inline void registerGctSemantic(
    const context_ptr_t &context, const node_ptr_t &node, const AST::node_ptr_t &ast,
    std::vector<camel::source::SemanticPart> extraParts  = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    if (!context || !node) {
        return;
    }
    auto sourceContext = context->sourceContext();
    auto mainOrigin    = nodeOrigin(node);
    if (!sourceContext || mainOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    std::vector<camel::source::SemanticPart> parts;
    if (const auto *bundle = astSemantic(context, ast)) {
        parts = bundle->parts;
        if (mergedInputs.empty()) {
            mergedInputs = bundle->mergedInputs;
        }
    }
    parts.insert(parts.end(), extraParts.begin(), extraParts.end());
    if (mergedInputs.empty()) {
        mergedInputs = collectChildOrigins(node);
    }
    sourceContext->registerGctSemantic(
        mainOrigin,
        camel::source::SemanticBundle{
            .mainOrigin      = mainOrigin,
            .parts           = std::move(parts),
            .mergedInputs    = std::move(mergedInputs),
            .synthetic       = synthetic,
            .syntheticReason = syntheticReason});
}

data_ptr_t extractStaticDataFromNode(const node_ptr_t &node) {
    if (node->type() == NodeKind::DATA) {
        return node->data();
    } else if (node->type() == NodeKind::DREF) {
        return make_shared<RefData>(node->name());
    } else {
        return nullptr;
    }
}

pair<node_ptr_t, data_ptr_t> Builder::makeRefData(const node_ptr_t &expr) {
    const string id    = std::to_string(idIndex_++);
    node_ptr_t refNode = tree_->makeNRef(id);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(id);
    return make_pair(refNode, data);
}

/// A tuple, array or struct literal that still names values (`(h, k)`): it is built when the
/// program runs, from what the names stand for, so it is not a constant element of a literal
/// that encloses it.
bool isUnresolvedLiteral(const data_ptr_t &data) {
    if (!data || !data->type()) {
        return false;
    }
    switch (data->type()->code()) {
    case TypeCode::Tuple:
    case TypeCode::Array:
    case TypeCode::Struct: {
        const auto composite = std::dynamic_pointer_cast<CompositeData>(data);
        return composite && !composite->resolved();
    }
    default:
        return false;
    }
}

pair<data_ptr_t, bool> Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticDataFromNode(node);
    // An enclosing literal only fills the names among its own elements (CompositeData::refs),
    // so an inner literal with names of its own is an element computed like any other
    // expression: `((h, k), 1)` is `let a = (h, k)` then `(a, 1)`. Embedded as a constant it
    // kept its unresolved element types ("(ref, ref)") and its names were never filled.
    if (data && !isUnresolvedLiteral(data)) {
        return make_pair(data, false);
    } else {
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

namespace {

/// Whether `type` is what the built-in names `Map` and `Set` resolve to.
bool isCollectionPrototype(Type *type) {
    const CollectionType *coll = asCollectionTypeOrPrototype(type);
    return coll != nullptr && !coll->complete();
}

} // namespace

Builder::TypePositionGuard::TypePositionGuard(Builder &builder, TypePosition position)
    : builder(builder), saved(std::exchange(builder.typePosition_, position)) {}

Builder::TypePositionGuard::~TypePositionGuard() { builder.typePosition_ = saved; }

void Builder::reportAnyType(const AST::node_ptr_t &ast) {
    // Design principles 5.10: every value has a definite type after compilation, and
    // `any` is refused wherever a recommended form exists. Each position has one.
    const char *instead = "";
    switch (typePosition_) {
    case TypePosition::Parameter:
        instead = "a parameter that takes values of several types declares a type parameter "
                  "where its type is written, 'func f(x: typeas T): T'";
        break;
    case TypePosition::WithParameter:
        instead = "a 'with' parameter that takes values of several types declares a type "
                  "parameter, 'with <tag: typeas T>'";
        break;
    case TypePosition::ReturnType:
        instead = "write the type the function returns, or leave the return type out to have "
                  "it inferred from the body";
        break;
    case TypePosition::CastTarget:
        instead = "'as any' converts nothing; leave it out and the value keeps its own type";
        break;
    case TypePosition::Annotation:
        instead = "write the type of the value, or leave the annotation out";
        break;
    case TypePosition::TypeDeclaration:
        instead = "a type that stands for several types is a type function, "
                  "'type Box<T> = (T, int)'";
        break;
    case TypePosition::Other:
        instead = "write the type of the value; values of different types side by side go in "
                  "a tuple or a struct";
        break;
    }
    diags_->of(SemanticDiag::AnyNotAllowed).at(ast->load()->tokenRange()).commit(instead);
    throw BuildAbortException();
}

void Builder::initInnerTypes() {
    typeScope_->clear();
    typeScope_->insert(Reference("i32"), Type::Int32());
    typeScope_->insert(Reference("i64"), Type::Int64());
    typeScope_->insert(Reference("int"), Type::Int64());
    typeScope_->insert(Reference("f32"), Type::Float32());
    typeScope_->insert(Reference("f64"), Type::Float64());
    typeScope_->insert(Reference("float"), Type::Float64());
    typeScope_->insert(Reference("bool"), Type::Bool());
    typeScope_->insert(Reference("string"), Type::String());
    // The type of binary data (design principles 11.1), a built-in name like `string`.
    typeScope_->insert(Reference("Bytes"), camel::core::type::BytesType::get());
    // No `any`: see reportAnyType.
    typeScope_->insert(Reference("void"), Type::Void());
    // The built-in collection types, written `Map<K, V>` and `Set<T>` (design principles 11.3).
    typeScope_->insert(Reference("Map"), MapType::prototype());
    typeScope_->insert(Reference("Set"), SetType::prototype());
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
// A literal that could not be converted to the type its position asks for: out of the range
// of a narrower numeric type (`let b: i32 = 3000000000`), or not convertible at all.
template <typename Diags, typename Range>
static void
reportLiteralCastFailure(Diags &diags, const Range &range, const data_ptr_t &data, Type *type) {
    if (const std::string limits = literalRangeViolation(data, type); !limits.empty()) {
        diags->of(SemanticDiag::LiteralOutOfRange)
            .at(range)
            .commit(literalValueText(data), type->toString(), limits);
        return;
    }
    diags->of(SemanticDiag::LiteralStaticCastFailed)
        .at(range)
        .commit(data->toString(), type->toString());
}

node_ptr_t Builder::visitModule(const AST::node_ptr_t &ast) {
    ENTER("Module");
    // Ensure the AST node is of the expected type
    ASSERT(ast->type() == AST::LoadType::Module, "Expected ModuleLoad type");

    module_->setDeclaredName(ast->loadAs<AST::ModuleLoad>()->getModuleNameOrEmpty());
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
    ASSERT(ast->type() == AST::LoadType::Import, "Expected ImportLoad type");

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
        mod = context_->importModule(path, module_->name());
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
    ASSERT(ast->type() == AST::LoadType::Export, "Expected ExportLoad type");

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
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type");
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
    default:
        throw std::runtime_error("Unknown statement type");
    }

    LEAVE("Stmt");
    setOriginFromAst(context_, stmtNode, ast, camel::source::OriginKind::GctNode, "gct.stmt");
    registerGctSemantic(context_, stmtNode, ast);
    return stmtNode;
}

inline bool validateIdent(const std::string &str) {
    // If the string length is less than 4, return true directly, indicating the identifier is valid
    if (str.length() < 4) {
        return true;
    }
    // Check if the string starts and ends with "__"
    // If true, return false indicating the identifier is invalid; otherwise, return true
    return !(str.substr(0, 2) == "__" && str.substr(str.length() - 2) == "__");
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
DataDecl(bool isVar, UnpackType type, Ref[] refs) : Type* type, Data* value;
*/
node_ptr_t Builder::visitDataDecl(const AST::node_ptr_t &ast) {
    ENTER("DataDecl");
    // Ensure the provided AST node is of type Stmt
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for DataDecl");

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

    // Handle different unpacking types
    switch (dataDeclLoad->unpackType()) {
    case AST::UnpackType::Struct: {
        // Struct unpacking is not supported; log diagnostics and throw an exception
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("Struct Unpacking");
        throw BuildAbortException();
    } break;

    case AST::UnpackType::Array: {
        // Array unpacking is not supported; log diagnostics and throw an exception
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("Array Unpacking");
        throw BuildAbortException();
    } break;

    case AST::UnpackType::Tuple: {
        if (refs.size() == dataNodes->size()) {
            for (size_t i = 0; i < refs.size(); ++i) {
                const auto &ref         = refs[i];
                const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(i);
                // The declared type first: an array literal takes the form of its elements
                // from it (visitDataInto).
                Type *declaredType = nullptr;
                if (i < typeNodes->size()) {
                    TypePositionGuard position(*this, TypePosition::Annotation);
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
                ASSERT(
                    dataASTNode->type() == AST::LoadType::Data,
                    "Tuple unpacking requires Data type nodes");
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
                    // `let a, b = 1, 2` binds every name: only the last one used to be kept.
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
                        TypePositionGuard position(*this, TypePosition::Annotation);
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
    } break;
    default:
        // If the unpacking type is unknown, throw an exception
        ASSERT(false, "Unknown unpack type in DataDecl");
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
                "type variable is written 'typeas T' in a parameter's type, not in angle "
                "brackets)");
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
}

/*
FuncDecl(Ref ref) : FuncAnno* anno, FuncData ;
*/
node_ptr_t Builder::visitFuncDecl(const AST::node_ptr_t &ast) {
    ENTER("FuncDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for FuncDecl");
    ASSERT(ast->size() >= 1, "FuncDecl AST should have at least one child.");

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

    // Decorator annotations are lowered to ANNO nodes attached after FUNC payload.
    // Each ANNO node owns one lowered GCT expression (currently lowered from funcAnno -> Data).
    if (ast->size() > 1) {
        const auto &annoListNode = ast->atAs<AST::RepeatedLoad>(0);
        for (const auto &annoAst : *annoListNode) {
            ASSERT(annoAst->type() == AST::LoadType::Data, "FuncAnno should lower to AST Data.");
            node_ptr_t annoExprNode = visitData(annoAst);
            node_ptr_t annoNode     = tree_->makeAnno();
            *annoNode << annoExprNode;
            *funcNode << annoNode;
        }
    }

    LEAVE("FuncDecl");
    return funcNode;
}

/*
TypeDecl(Ref ref, ImplMark impl, string uri) : Type? type ;
*/
node_ptr_t Builder::visitTypeDecl(const AST::node_ptr_t &ast) {
    ENTER("TypeDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for TypeDecl");
    const auto &typeNode = ast->optAtAs<AST::TypeLoad>(0);
    Type *type;
    ASSERT(typeNode, "TypeDecl must have a type");
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
    TypePositionGuard position(*this, TypePosition::TypeDeclaration);
    if (typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Enum) {
        // The name is in scope before the variants are read, so that a variant field can hold
        // the enum itself (`type Tree = enum { Node(Tree, Tree) }`).
        type = declareEnumType(typeNode, typeLoad.get());
    } else if (typeLoad->params().empty()) {
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
    typeScope_->insert(typeLoad->ref(), type);
    if (exportedRefs_.contains(typeLoad->ref().toString())) {
        // Exported at once: a module that imports this one back may need it before this one
        // is through (see visitModule).
        module_->exportType(typeLoad->ref(), type);
    }
    node_ptr_t declNode = tree_->makeDecl(typeLoad->ref(), false);
    *declNode << tree_->makeType(type, typeLoad->implMark());
    LEAVE("TypeDecl");
    return declNode;
}

void Builder::bindTypeParameters(
    const AST::TypeDeclLoad *decl, const AST::node_ptr_t &ast, const std::string &name) {
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
        typeScope_->insert(paramRef, TypeVarType::create(param, false));
    }
}

Type *Builder::declareEnumType(const AST::node_ptr_t &ast, const AST::TypeDeclLoad *decl) {
    const string name                    = decl->ref().toString();
    const auto enumLoad                  = ast->loadAs<AST::EnumTypeLoad>();
    EnumDecl *declaration                = EnumDecl::create(name, decl->params());
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

std::vector<EnumVariant>
Builder::buildEnumVariants(const AST::node_ptr_t &ast, const EnumType *instance) {
    const auto enumLoad = ast->loadAs<AST::EnumTypeLoad>();
    ASSERT(
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
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for UsingNamespace");
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

/*
ExprStmt() : Data data ;
*/
node_ptr_t Builder::visitExprStmt(const AST::node_ptr_t &ast) {
    ENTER("ExprStmt");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExprStmt");
    node_ptr_t exprNode = visitData(ast->atAs<AST::DataLoad>(0));
    LEAVE("ExprStmt");
    return exprNode;
}

/*
ExitStmt(ExitType type) : Data* data ;
*/
node_ptr_t Builder::visitExitStmt(const AST::node_ptr_t &ast) {
    ENTER("ExitStmt");
    // Ensure the AST node is of the expected type (Stmt)
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExitStmt");

    // Create an ExitLoad node to represent the exit statement. `throw` is an exit as well:
    // it returns the failed form of the function's declared `T!` (the graph builder checks).
    const bool isThrow  = ast->loadAs<AST::ExitStmtLoad>()->exitType() == AST::ExitType::Throw;
    node_ptr_t exitNode = tree_->makeExit(isThrow ? ExitType::Throw : ExitType::Return);
    reportExitInUnusedArm(ast, isThrow);

    // Retrieve the data nodes associated with the exit statement
    const auto &dataNodes = ast->atAs<AST::RepeatedLoad>(0);

    // If there are data nodes, process them
    if (dataNodes->size() > 0) {
        // Case 1: Single data node
        if (dataNodes->size() == 1) {
            // Lowered once. It used to be lowered twice and the first result dropped: every
            // `return` nested in the returned expression doubled the work (2^n lowerings at
            // depth n) and repeated the diagnostics of the expression.
            *exitNode << visitData(dataNodes->front());
        } else {
            // Case 2: Multiple data nodes - build once with Factory to avoid recomputing the type.
            TupleDataFactory tupleFactory;
            bool dangling       = false;
            node_ptr_t execNode = makeExec(false, true);

            for (const auto &item : *dataNodes) {
                node_ptr_t dataNode = visitData(item);
                auto [data, _]      = extractData(dataNode, execNode, dangling);
                tupleFactory.add(data);
            }

            auto tupleData      = tupleFactory.build();
            node_ptr_t dataNode = tree_->makeData(tupleData);

            if (dangling) {
                *execNode << dataNode;
                dataNode = execNode;
            }

            *exitNode << dataNode;
        }
    }

    LEAVE("ExitStmt");
    return exitNode;
}

void Builder::enterArmOf(const AST::node_ptr_t &ctrl, bool valueUnused) {
    valueUse_.unusedCtrl  = nullptr;
    valueUse_.blockUnused = valueUnused;
    valueUse_.unusedArmOf = valueUnused ? ctrl : nullptr;
}

void Builder::reportExitInUnusedArm(const AST::node_ptr_t &exit, bool isThrow) {
    const AST::node_ptr_t &ctrl = valueUse_.unusedArmOf;
    if (ctrl == nullptr) {
        return;
    }
    // `return null` and a bare `return` are reported like any other: an arm needs no value
    // to compile (design principles 3.7: an `if` without `else` has none, and its arm may
    // be of any type), so one written there was most likely meant to leave the function.
    // One report per statement: the arm of `case a | b` is lowered once for each pattern.
    const auto range = exit->load()->tokenRange();
    if (!reportedExits_.insert({range.start, range.end}).second) {
        return;
    }
    const bool isMatch  = ctrl->loadAs<AST::DataLoad>()->dataType() == AST::DataType::MatchExpr;
    const char *keyword = isThrow ? "throw" : "return";
    const char *branch  = isMatch ? "match" : "if";
    diags_->of(SemanticDiag::ExitInUnusedBranch)
        .at(range)
        .relatedOrigin(
            ctrl->load()->origin(),
            std::format("the value of this '{}' is not used", branch))
        .commit(keyword, branch);
}

/// An EXEC with the numeric permissions in force here (GCT::Node::numericPermissions).
node_ptr_t Builder::makeExec(bool synced, bool operands) {
    node_ptr_t exec = tree_->makeExec(synced, operands);
    exec->setNumericPermissions(numericPermissions_);
    return exec;
}

/*
StmtBlock(bool sync) : Stmt* stmts ;
*/
node_ptr_t Builder::visitDecoratedBlock(const AST::node_ptr_t &ast) {
    ENTER("DecoratedBlock");
    node_ptr_t execNode = visitStmtBlock(ast, false);
    FunctionTypeFactory factory; // no parameters; the result is the block's value
    FunctionType *type  = factory.buildOpen();
    node_ptr_t typeNode = tree_->makeType(type, type->implMark());
    node_ptr_t funcNode = tree_->makeFunc("");
    funcNode->setDecoratedBlock();
    *funcNode << typeNode << execNode;
    for (const auto &annoAst : *ast->atAs<AST::RepeatedLoad>(1)) {
        ASSERT(annoAst->type() == AST::LoadType::Data, "A decorator should lower to AST Data.");
        node_ptr_t annoNode = tree_->makeAnno();
        *annoNode << visitData(annoAst);
        *funcNode << annoNode;
    }
    node_ptr_t linkNode = tree_->makeLink(0);
    *linkNode << funcNode;
    setOriginFromAst(context_, typeNode, ast, camel::source::OriginKind::GctNode, "gct.block.type");
    setOriginFromAst(context_, funcNode, ast, camel::source::OriginKind::GctNode, "gct.block.func");
    setOriginFromAst(context_, linkNode, ast, camel::source::OriginKind::GctNode, "gct.block.call");
    LEAVE("DecoratedBlock");
    return linkNode;
}

node_ptr_t Builder::visitStmtBlock(const AST::node_ptr_t &ast, bool decorators) {
    ENTER("StmtBlock");
    // Ensure the AST node is of the expected type (Stmt)
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for StmtBlock");
    if (decorators && ast->size() > 1) {
        LEAVE("StmtBlock");
        return visitDecoratedBlock(ast);
    }

    // Extract the statement block load and create an ExecLoad node
    // The ExecLoad node represents the execution of the statement block
    const auto &stmtBlock = ast->loadAs<AST::StmtBlockLoad>();
    if (stmtBlock->waited()) {
        // `wait { ... }`: the grammar takes it and the mark was dropped. `wait` is defined
        // for one expression (design principles log 2.6); what it means for a block is not.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(ast->load()->tokenRange())
            .commit(
                "wait",
                "it is not implemented before a block and would be ignored; write 'sync { ... }' "
                "to run the statements of a block in order");
        throw BuildAbortException();
    }
    // The block's own grant joins those of the blocks it is in, for everything written in it.
    const uint8_t outerPermissions = numericPermissions_;
    numericPermissions_ |= stmtBlock->numeric();
    struct PermissionScope {
        uint8_t &current;
        uint8_t outer;
        ~PermissionScope() { current = outer; }
    } permissionScope{numericPermissions_, outerPermissions};
    node_ptr_t execNode = makeExec(stmtBlock->synced());

    // Push a new scope for the statement block
    pushScope();
    // The `using namespace` written in the block apply to the whole block, and to it only
    // (design principles 10.2).
    std::vector<Reference> blockUsings;
    for (const auto &stmt : *ast->atAs<AST::RepeatedLoad>(0)) {
        if (stmt->type() == AST::LoadType::Stmt &&
            stmt->loadAs<AST::StmtLoad>()->stmtType() == AST::StmtType::UsingNamespace) {
            blockUsings.push_back(stmt->loadAs<AST::UsingNamespaceLoad>()->ref());
        }
    }
    tree_->addUsings(execNode, blockUsings);
    struct UsingFrame {
        std::vector<std::vector<Reference>> &frames;
        UsingFrame(std::vector<std::vector<Reference>> &f, std::vector<Reference> u) : frames(f) {
            frames.push_back(std::move(u));
        }
        ~UsingFrame() { frames.pop_back(); }
    } usingFrame(usingFrames_, blockUsings);

    // Whether anything takes the value of this block (see ValueUse). The value of a block is
    // that of its last statement; the value of any other statement nothing takes.
    // (A function or type declaration is not a statement with a value.)
    const bool blockUnused = std::exchange(valueUse_.blockUnused, false);
    const auto &stmts      = ast->atAs<AST::RepeatedLoad>(0);
    const AST::Node *last  = nullptr;
    for (const auto &stmt : *stmts) {
        const auto stmtType = stmt->loadAs<AST::StmtLoad>()->stmtType();
        if (stmtType != AST::StmtType::Func && stmtType != AST::StmtType::Type) {
            last = stmt.get();
        }
    }
    for (const auto &stmt : *stmts) {
        // Ensure each item in the block is a statement
        ASSERT(stmt->type() == AST::LoadType::Stmt, "Expected a statement in StmtBlock");
        const bool stmtUnused = blockUnused || stmt.get() != last;
        const auto stmtType   = stmt->loadAs<AST::StmtLoad>()->stmtType();
        valueUse_.unusedCtrl  = nullptr;
        valueUse_.blockUnused = false;
        valueUse_.stmtExpr =
            stmtType == AST::StmtType::Expr && stmt->size() > 0 ? stmt->at(0).get() : nullptr;
        if (stmtUnused && stmtType == AST::StmtType::Expr && stmt->size() > 0) {
            // An `if` / `match` written as a statement whose value nothing takes.
            const auto &expr = stmt->at(0);
            const auto kind  = expr->type() == AST::LoadType::Data
                                   ? expr->loadAs<AST::DataLoad>()->dataType()
                                   : AST::DataType::Literal;
            if (kind == AST::DataType::IfExpr || kind == AST::DataType::MatchExpr) {
                valueUse_.unusedCtrl = expr.get();
            }
        } else if (stmtType == AST::StmtType::Block) {
            // A block written as a statement is part of this body: its exits are exits of
            // the enclosing arm, and its value is used when the statement's is.
            valueUse_.blockUnused = stmtUnused;
        }
        // Process the statement and attach it to the ExecLoad node
        *execNode << visitStmt(stmt);
    }
    valueUse_.unusedCtrl  = nullptr;
    valueUse_.blockUnused = false;

    // Pop the scope after processing the block
    popScope();

    LEAVE("StmtBlock");
    setOriginFromAst(context_, execNode, ast, camel::source::OriginKind::GctNode, "gct.block");
    registerGctSemantic(context_, execNode, ast);
    // Return the ExecLoad node representing the processed statement block
    return execNode;
}

node_ptr_t Builder::visitData(const AST::node_ptr_t &ast) {
    ENTER("Data");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for visitData");
    const auto &data    = ast->loadAs<AST::DataLoad>();
    node_ptr_t dataNode = nullptr;

    switch (data->dataType()) {
    case AST::DataType::UnaryExpr:
        dataNode = visitUnaryExpr(ast);
        break;
    case AST::DataType::BinaryExpr:
        dataNode = visitBinaryExpr(ast);
        break;
    case AST::DataType::ReservedExpr:
        dataNode = visitReservedExpr(ast);
        break;
    case AST::DataType::IfExpr:
        dataNode = visitIfExpr(ast);
        break;
    case AST::DataType::MatchExpr:
        dataNode = visitMatchExpr(ast);
        break;
    case AST::DataType::TryExpr:
        dataNode = visitTryExpr(ast);
        break;
    case AST::DataType::LoopExpr:
        dataNode = visitLoopExpr(ast);
        break;
    case AST::DataType::Literal:
        dataNode = visitLiteral(ast);
        break;
    case AST::DataType::Array:
        dataNode = visitArrayData(ast);
        break;
    case AST::DataType::Struct:
        dataNode = visitStructData(ast);
        break;
    case AST::DataType::Tuple:
        dataNode = visitTupleData(ast);
        break;
    case AST::DataType::Func:
        dataNode = visitFuncData(ast);
        break;
    case AST::DataType::Ref:
        dataNode = visitRefData(ast);
        break;
    default:
        throw std::runtime_error("Unknown data type");
    }
    if (data->waited() && data->dataType() != AST::DataType::BinaryExpr &&
        data->dataType() != AST::DataType::ReservedExpr) {
        // `wait` orders the one expression after it (design principles log 2.6). Only an operator
        // expression, an assignment, a call, an access or a binding is lowered with the mark;
        // on anything else (a name, a literal, a unary operator, a lambda) the mark was
        // dropped without a word. What it should order there has not been decided.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(ast->load()->tokenRange())
            .commit(
                "wait",
                "it is implemented before a call, an assignment or a binary operator "
                "expression only, and would be ignored before this expression");
        throw BuildAbortException();
    }

    LEAVE("Data");
    setOriginFromAst(context_, dataNode, ast, camel::source::OriginKind::GctNode, "gct.data");
    registerGctSemantic(context_, dataNode, ast);
    return dataNode;
}

/*
enum UnaryDataOp {
    Not,
    Neg,
    Inv
}
UnaryExpr(UnaryDataOp op) := Data data ;
*/
node_ptr_t Builder::visitUnaryExpr(const AST::node_ptr_t &ast) {
    ENTER("UnaryExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for UnaryExpr");

    // Extract the unary expression load and the associated data node
    const auto &unaryExpr   = ast->loadAs<AST::UnaryExprLoad>();
    const auto &dataASTNode = ast->atAs<AST::DataLoad>(0);

    node_ptr_t opNode = nullptr;

    // Determine the unary operator and create the corresponding node
    switch (unaryExpr->op()) {
    case AST::UnaryDataOp::Not: {
        opNode = tree_->makeDRef("__not__");
    } break;
    case AST::UnaryDataOp::Neg: {
        opNode = tree_->makeDRef("__neg__");
    } break;
    case AST::UnaryDataOp::Inv: {
        opNode = tree_->makeDRef("__inv__");
    } break;
    default:
        // If the operator is unknown, assert and return nullptr
        ASSERT(false, "Unknown unary operation");
        return nullptr;
    }
    auto unaryAstBundle = astSemantic(context_, ast);
    setOriginFromOrigin(
        context_,
        opNode,
        findSemanticOrigin(unaryAstBundle, camel::source::SemanticRole::Operator),
        camel::source::OriginKind::GctNode,
        "gct.unary.operator");

    // Create a LinkLoad node to link the operator and the operand
    node_ptr_t operandNode = visitData(dataASTNode);
    node_ptr_t linkNode    = tree_->makeLink(1);
    *linkNode << opNode << operandNode;
    setOriginFromAst(context_, linkNode, ast, camel::source::OriginKind::GctNode, "gct.unary.link");
    registerGctSemantic(
        context_,
        linkNode,
        ast,
        {
            semanticPart(camel::source::SemanticRole::Operator, nodeOrigin(opNode), -1, "operator"),
            semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(operandNode),
                0,
                "operand"),
        },
        {nodeOrigin(opNode), nodeOrigin(operandNode)});

    LEAVE("UnaryExpr");
    // Return the constructed LinkLoad node
    return linkNode;
}

/*
enum BinaryDataOp {
    Assign,
    AssignAdd,
    AssignSub,
    AssignMul,
    AssignDiv,
    AssignMod,
    AssignMat,
    AssignExp,
    AssignAnd,
    AssignOr,
    Or,
    And,
    Eq,
    Neq,
    StrictEq,
    StrictNeq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Mat,
    Pow,
    Index, // Data obj, Data* indices
}
BinaryExpr(BinaryDataOp op) := Data lhs, Data rhs ;
*/
node_ptr_t Builder::visitBinaryExpr(const AST::node_ptr_t &ast) {
    ENTER("BinaryExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for BinaryExpr");

    // Extract the binary expression load and the left-hand side (LHS) AST node
    const auto &binaryExpr = ast->loadAs<AST::BinaryExprLoad>();
    const auto &lhsASTNode = ast->atAs<AST::DataLoad>(0);

    // Check if the binary expression is marked as "waited"
    bool waited = binaryExpr->waited();

    node_ptr_t opNode = nullptr;

    // Determine the binary operator and create the corresponding node
    switch (binaryExpr->op()) {
    case AST::BinaryDataOp::LogicalOr:
    case AST::BinaryDataOp::LogicalAnd: {
        // `||` and `&&` are branches, not operators (design principles 4.2).
        node_ptr_t res = visitLogicalExpr(ast);
        LEAVE("BinaryExpr");
        return res;
    }
    case AST::BinaryDataOp::StrictEq:
    case AST::BinaryDataOp::StrictNeq: {
        // Design principles 4.1: `===` is not supported for now (and `!==` with it). It
        // used to compile and compared objects, so two equal string literals were unequal.
        // Located at the whole comparison (the range of the node itself is its right side).
        auto range  = ast->load()->tokenRange();
        range.start = lhsASTNode->load()->tokenRange().start;
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(range)
            .commit(
                binaryExpr->op() == AST::BinaryDataOp::StrictEq ? "===" : "!==",
                "identity comparison is not part of the language for now; compare values "
                "with '==' and '!='");
        throw BuildAbortException();
    }
    default: {
        // Every other operator is a call of the group the table of operator_names.cpp gives
        // its symbol: plain, compound assignment and `=` alike.
        const auto group = camel::core::binaryOperatorGroup(AST::to_string(binaryExpr->op()));
        ASSERT(group.has_value(), "Unknown binary operation");
        if (!group)
            return nullptr;
        opNode = tree_->makeDRef(std::string(*group));
    } break;
    }
    auto binaryAstBundle = astSemantic(context_, ast);
    setOriginFromOrigin(
        context_,
        opNode,
        findSemanticOrigin(binaryAstBundle, camel::source::SemanticRole::Operator),
        camel::source::OriginKind::GctNode,
        "gct.binary.operator");

    // Create a LinkLoad node to link the operator and the operands
    node_ptr_t res     = tree_->makeLink(2);
    node_ptr_t lhsNode = visitData(lhsASTNode);
    *res << opNode << lhsNode;
    std::vector<camel::source::SemanticPart> parts = {
        semanticPart(camel::source::SemanticRole::Operator, nodeOrigin(opNode), -1, "operator"),
        semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
    };
    std::vector<camel::source::origin_id_t> mergedInputs = {
        nodeOrigin(opNode),
        nodeOrigin(lhsNode)};

    // Handle the right-hand side (RHS) or indices for specific operators
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        auto indices      = ast->atAs<AST::RepeatedLoad>(1);
        int32_t indexSlot = 0;
        for (const auto &index : *indices) {
            node_ptr_t indexNode = visitData(index);
            *res << indexNode;
            mergedInputs.push_back(nodeOrigin(indexNode));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::IndexExpr,
                nodeOrigin(indexNode),
                indexSlot++,
                "index"));
        }
    } else {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t rhsNode     = visitData(rhsASTNode);
        *res << rhsNode;
        mergedInputs.push_back(nodeOrigin(rhsNode));
        parts.push_back(
            semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"));
    }
    setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.binary.link");
    registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));

    // If the expression is marked as "waited", wrap it in a WaitLoad node
    if (waited) {
        node_ptr_t waitNode = tree_->makeWait();
        *waitNode << res;
        setOriginFromAst(context_, waitNode, ast, camel::source::OriginKind::GctNode, "gct.wait");
        registerGctSemantic(
            context_,
            waitNode,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    nodeOrigin(waitNode),
                    -1,
                    "wait"),
                semanticPart(
                    camel::source::SemanticRole::ValueProducer,
                    nodeOrigin(res),
                    -1,
                    "inner"),
            },
            {nodeOrigin(res)});
        res = waitNode;
    }
    LEAVE("BinaryExpr");
    return res;
}

/*
`a && b` is `if a then b else false`, `a || b` is `if a then true else b` (design principles
2.11): a branch on the left side with the right side as one arm, so the right side is
evaluated only when the left does not decide. The node built has the children the `if` would
have; its kind keeps what was written (see BranchKind). Neither can be overloaded: no
name is looked up for them. The eager, overloadable pair is `&` and `|` (`__and__`,
`__or__`).
*/
node_ptr_t Builder::visitLogicalExpr(const AST::node_ptr_t &ast) {
    ENTER("LogicalExpr");
    const auto &logicalExpr = ast->loadAs<AST::BinaryExprLoad>();
    const bool isAnd        = logicalExpr->op() == AST::BinaryDataOp::LogicalAnd;

    node_ptr_t brchNode = tree_->makeBrch(isAnd ? BranchKind::LogicalAnd : BranchKind::LogicalOr);
    // Located where the binary expression is, like the operators `&` and `|`.
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.logical");

    node_ptr_t lhsNode = visitData(ast->atAs<AST::DataLoad>(0));
    node_ptr_t rhsNode = visitData(ast->atAs<AST::DataLoad>(1));
    // The arm that is not the right side: the value the left side has already decided.
    node_ptr_t decidedNode =
        tree_->makeData(tt::as_shared<Data>(make_shared<PrimaryData<bool>>(!isAnd)));
    setOriginFromAst(
        context_,
        decidedNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.logical.decided");

    const auto armOf = [&](CaseType caseType, node_ptr_t value) {
        node_ptr_t execNode = makeExec();
        setOriginFromOrigin(
            context_,
            execNode,
            nodeOrigin(value),
            camel::source::OriginKind::GctNode,
            "gct.logical.arm");
        *execNode << value;
        node_ptr_t caseNode = tree_->makeCase(caseType);
        *caseNode << execNode;
        return caseNode;
    };
    *brchNode << lhsNode << armOf(CaseType::True, isAnd ? rhsNode : decidedNode)
              << armOf(CaseType::Else, isAnd ? decidedNode : rhsNode);

    node_ptr_t res = brchNode;
    if (logicalExpr->waited()) {
        node_ptr_t waitNode = tree_->makeWait();
        *waitNode << res;
        setOriginFromAst(context_, waitNode, ast, camel::source::OriginKind::GctNode, "gct.wait");
        res = waitNode;
    }
    LEAVE("LogicalExpr");
    return res;
}

/*
enum ReservedDataOp {
    NullThen,
    NullThen,
    ErrorThen,
    NotNullThen,
    Call, // Data obj, Data* args, NamedData* kwargs
    Bind, // Data obj, Data* args, NamedData* kwargs
    Comp, // Data lhs, Data rhs
    As, // Data lhs, Type rhs
    Is, // Data lhs, Type rhs
    Access, // Data obj, RefData ref
}
ReservedExpr(ReservedDataOp op) := Data lhs, Any? rhs ;
*/
node_ptr_t Builder::visitReservedExpr(const AST::node_ptr_t &ast) {
    ENTER("ReservedExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ReservedExpr");

    node_ptr_t res           = nullptr;
    const auto &reservedExpr = ast->loadAs<AST::ReservedExprLoad>();
    bool waited              = reservedExpr->waited();
    const auto &lhsASTNode   = ast->atAs<AST::DataLoad>(0);

    // Handle different reserved operators
    switch (reservedExpr->op()) {
    case AST::ReservedDataOp::NullThen:
    case AST::ReservedDataOp::ErrorThen: {
        // `a ?? b` / `a !! b`: a branch on the tag of `a` whose first arm is `b` (design
        // principles 3.9). The graph builder knows the types; it adds the arm that takes the
        // value out of `a`.
        const bool onNull = reservedExpr->op() == AST::ReservedDataOp::NullThen;
        res               = tree_->makeBrch(onNull ? BranchKind::NullThen : BranchKind::ErrorThen);
        // The node is located at the operator.
        setOriginFromAst(
            context_,
            res,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.tagged.branch");
        node_ptr_t lhsNode  = visitData(lhsASTNode);
        node_ptr_t rhsNode  = visitData(ast->atAs<AST::DataLoad>(1));
        node_ptr_t execNode = makeExec();
        setOriginFromOrigin(
            context_,
            execNode,
            nodeOrigin(rhsNode),
            camel::source::OriginKind::GctNode,
            "gct.tagged.fallback");
        *execNode << rhsNode;
        node_ptr_t caseNode = tree_->makeCase(CaseType::True);
        *caseNode << execNode;
        *res << lhsNode << caseNode;
    } break;
    case AST::ReservedDataOp::NotNullThen: {
        // The marked receiver of `x ?-> f` is taken apart where the call is lowered (Call,
        // below); it does not occur anywhere else.
        ASSERT(false, "The receiver of '?->' is lowered with its call.");
        res = visitData(lhsASTNode);
    } break;

    case AST::ReservedDataOp::Call: {
        // Handle the "Call" operator
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = tree_->makeLink(argsNode->size());
        node_ptr_t calleeNode  = visitData(lhsASTNode);
        *res << calleeNode;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
        };
        std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(calleeNode)};
        int32_t argSlot                                      = 0;
        // `x ?-> f`: the receiver is marked (NotNullThen). The call becomes the arm of a
        // branch on the tag of `x`, taken when `x` has not failed, and receives the value
        // taken out of `x` under a reserved name (design principles 3.8).
        node_ptr_t passedNode        = nullptr;
        AST::node_ptr_t passOperator = nullptr;
        for (auto &argNode : *argsNode) {
            node_ptr_t gctArg   = nullptr;
            const auto &argData = argNode->loadAs<AST::DataLoad>();
            if (argData->dataType() == AST::DataType::ReservedExpr &&
                argNode->loadAs<AST::ReservedExprLoad>()->op() ==
                    AST::ReservedDataOp::NotNullThen) {
                passedNode   = visitData(argNode->atAs<AST::DataLoad>(0));
                passOperator = argNode;
                gctArg       = tree_->makeDRef(kPassedValueName);
                setOriginFromOrigin(
                    context_,
                    gctArg,
                    nodeOrigin(passedNode),
                    camel::source::OriginKind::GctNode,
                    "gct.tagged.passed");
            } else {
                gctArg = visitData(argNode);
            }
            *res << gctArg;
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(gctArg),
                argSlot++,
                "arg"));
        }
        if (kwargsNode->size() > 0) {
            // Keyword arguments are not supported
            diags_->of(SemanticDiag::FeatureNotSupported)
                .at(ast->load()->tokenRange())
                .commit("kwargs");
            throw BuildAbortException();
        }
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.call");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
        if (passedNode != nullptr) {
            node_ptr_t execNode = makeExec();
            setOriginFromAst(
                context_,
                execNode,
                ast,
                camel::source::OriginKind::GctNode,
                "gct.tagged.call");
            *execNode << res;
            node_ptr_t caseNode = tree_->makeCase(CaseType::Else);
            *caseNode << execNode;
            node_ptr_t brchNode = tree_->makeBrch(BranchKind::ErrorPass);
            // Located at the operator, as the marked receiver is.
            setOriginFromAst(
                context_,
                brchNode,
                passOperator,
                camel::source::OriginKind::GctNode,
                "gct.tagged.branch");
            *brchNode << passedNode << caseNode;
            res = brchNode;
        }
    } break;

    case AST::ReservedDataOp::Instantiate: {
        res = tree_->makeTypeApplication();
        *res << visitData(lhsASTNode);
        for (const auto &argument : *ast->atAs<AST::RepeatedLoad>(1)) {
            if (argument->type() == AST::LoadType::NamedType) {
                // `A = int`: the name as a constant, then the type (design principles 5.9).
                const auto &named = argument->loadAs<AST::NamedTypeLoad>();
                *res << tree_->makeData(std::make_shared<StringData>(named->getRef().toString()));
                *res << tree_->makeType(visitType(argument->at(0)), ImplMark::Graph);
                continue;
            }
            Type *type = visitType(argument);
            *res << tree_->makeType(type, ImplMark::Graph);
        }
        setOriginFromAst(
            context_,
            res,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.type_application");
    } break;

    case AST::ReservedDataOp::Bind: {
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = tree_->makeWith(argsNode->size());
        node_ptr_t calleeNode  = visitData(lhsASTNode);
        *res << calleeNode;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
        };
        std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(calleeNode)};
        int32_t argSlot                                      = 0;
        for (auto &argNode : *argsNode) {
            node_ptr_t gctArg = visitData(argNode);
            *res << gctArg;
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::GenericArgument,
                nodeOrigin(gctArg),
                argSlot++,
                "withArg"));
        }
        if (kwargsNode->size() > 0) {
            diags_->of(SemanticDiag::FeatureNotSupported)
                .at(ast->load()->tokenRange())
                .commit("kwargs");
            throw BuildAbortException();
        }
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.bind");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
    } break;

    case AST::ReservedDataOp::Comp: {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t opNode      = tree_->makeDRef("__cmp__");
        res                    = tree_->makeLink(2);
        auto compAstBundle     = astSemantic(context_, ast);
        setOriginFromOrigin(
            context_,
            opNode,
            findSemanticOrigin(compAstBundle, camel::source::SemanticRole::Operator),
            camel::source::OriginKind::GctNode,
            "gct.comp.operator");
        node_ptr_t lhsNode = visitData(lhsASTNode);
        node_ptr_t rhsNode = visitData(rhsASTNode);
        *res << opNode << lhsNode << rhsNode;
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.comp");
        registerGctSemantic(
            context_,
            res,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    nodeOrigin(opNode),
                    -1,
                    "operator"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            },
            {nodeOrigin(opNode), nodeOrigin(lhsNode), nodeOrigin(rhsNode)});
    } break;

    case AST::ReservedDataOp::As: {
        const auto &rhsASTNode = ast->atAs<AST::TypeLoad>(1);
        Type *dstType          = nullptr;
        {
            TypePositionGuard position(*this, TypePosition::CastTarget);
            dstType = visitType(rhsASTNode);
        }
        // `[1, null] as (int?)[]`: the literal takes the form of its elements from the type.
        bool enteredLiteral  = false;
        const auto &dataNode = visitDataInto(lhsASTNode, dstType, enteredLiteral);
        if (enteredLiteral) {
            res = dataNode;
            break;
        }
        // A conversion to a type written with a type variable is decided when the generic
        // function is instantiated, for a literal as for any other value.
        // A conversion into `T?` / `T!` is a CAST node for a literal too: the value is put
        // into the form when the program runs.
        // A conversion into a Map or a Set builds the collection when the program runs (a
        // CAST), for a literal too; what can be said about the literal is said here.
        const bool intoCollection = asCollectionTypeOrPrototype(dstType) != nullptr;
        if (intoCollection && dataNode->type() == NodeKind::DATA) {
            rejectDuplicateLiteralKeys(dataNode->data(), dstType, ast);
        }
        if (dataNode->type() == NodeKind::DATA && !containsTypeVar(dstType) &&
            asTagged(dstType) == nullptr && !intoCollection) {
            const auto &data = dataNode->data();
            if (dstType->castSafetyFrom(data->type()) != CastSafety::Safe) {
                reportLiteralCastFailure(diags_, ast->load()->tokenRange(), data, dstType);
                throw BuildAbortException();
            }
            auto convertedData = data->convertTo(dstType);
            if (convertedData) {
                res = tree_->makeData(convertedData);
            } else {
                reportLiteralCastFailure(diags_, ast->load()->tokenRange(), data, dstType);
                throw BuildAbortException();
            }
        } else {
            res = tree_->makeCast(dstType);
            *res << dataNode;
            setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.cast");
            registerGctSemantic(
                context_,
                res,
                ast,
                {
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(dataNode),
                        -1,
                        "value"),
                    semanticPart(
                        camel::source::SemanticRole::ReturnType,
                        findSemanticOrigin(
                            astSemantic(context_, ast),
                            camel::source::SemanticRole::ReturnType),
                        -1,
                        "type"),
                },
                {nodeOrigin(dataNode)});
        }
    } break;

    case AST::ReservedDataOp::Is: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("IS Operator");
        res = visitData(lhsASTNode);
    } break;

    case AST::ReservedDataOp::Access: {
        const auto &refASTNode  = ast->atAs<AST::RefDataLoad>(1);
        const auto &refDataLoad = refASTNode->loadAs<AST::RefDataLoad>();
        const auto &ref         = refDataLoad->ref();
        try {
            size_t index = std::stoul(ref.ident());
            res          = tree_->makeAccs(index);
        } catch (const std::exception &) {
            // Not an index, treat as named access
            res = tree_->makeAccs(ref);
        }
        node_ptr_t targetNode = visitData(lhsASTNode);
        *res << targetNode;
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.access");
        registerGctSemantic(
            context_,
            res,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::Receiver,
                    nodeOrigin(targetNode),
                    -1,
                    "target"),
                semanticPart(
                    camel::source::SemanticRole::MemberName,
                    findSemanticOrigin(
                        astSemantic(context_, ast),
                        camel::source::SemanticRole::MemberName),
                    -1,
                    ref.ident()),
            },
            {nodeOrigin(targetNode)});
    } break;

    default:
        ASSERT(false, "Unknown reserved operation");
        res = nullptr;
    }

    // If the expression is "waited", wrap the result in a WaitLoad node
    if (waited) {
        node_ptr_t waitNode = tree_->makeWait();
        *waitNode << res; // Wait for the result of the reserved expression
        setOriginFromAst(context_, waitNode, ast, camel::source::OriginKind::GctNode, "gct.wait");
        registerGctSemantic(
            context_,
            waitNode,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::ValueProducer,
                    nodeOrigin(res),
                    -1,
                    "inner"),
            },
            {nodeOrigin(res)});
        res = waitNode;
    }
    LEAVE("ReservedExpr");
    return res;
}
/*
IfExpr() : Data cond, StmtBlock then, StmtBlock? else ;
*/
node_ptr_t Builder::visitIfExpr(const AST::node_ptr_t &ast) {
    ENTER("IfExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for IfExpr");

    // Create a branch node to represent the conditional expression
    node_ptr_t brchNode = tree_->makeBrch();
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.if");

    // Whether this `if` is a statement whose value nothing takes: its arms are then lowered
    // as arms of an unused branch (see ValueUse).
    const bool valueUnused = std::exchange(valueUse_.unusedCtrl, nullptr) == ast.get();
    // Design principles 3.7: an `if` without `else` has no value, so it can only stand as
    // a statement. (As the last statement of a block it gives the block no value either;
    // what then expects one reports the missing value by the type.)
    const bool isStatement = std::exchange(valueUse_.stmtExpr, nullptr) == ast.get();
    // The AST gives an `if` without `else` an empty block in its place, so an `else` that
    // is written and empty is the same thing here and in the GIR builder (buildBranch).
    const auto &elseBlock = ast->optAtAs<AST::StmtBlockLoad>(2);
    if (!isStatement && (!elseBlock || elseBlock->atAs<AST::RepeatedLoad>(0)->size() == 0)) {
        diags_->of(SemanticDiag::IfWithoutElseAsValue).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    }
    const ValueUseScope valueUseScope(*this);

    // condition
    // Process the condition expression and attach it to the branch node
    node_ptr_t condNode = visitData(ast->atAs<AST::DataLoad>(0));
    *brchNode << condNode;

    // Process the "then" block and attach it as the "True" case
    const auto &thenBlock = ast->atAs<AST::StmtBlockLoad>(1);
    enterArmOf(ast, valueUnused);
    node_ptr_t thenNode = visitStmtBlock(thenBlock);
    node_ptr_t trueCase = tree_->makeCase(CaseType::True);
    setOriginFromOrigin(
        context_,
        trueCase,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchTarget,
            0),
        camel::source::OriginKind::GctNode,
        "gct.if.then");
    *trueCase << thenNode;
    *brchNode << trueCase;

    // Process the optional "else" block and attach it as the "Else" case
    const auto &elseNode = ast->optAtAs<AST::StmtBlockLoad>(2);
    node_ptr_t elseCase  = tree_->makeCase(CaseType::Else);
    setOriginFromOrigin(
        context_,
        elseCase,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchTarget,
            1),
        camel::source::OriginKind::GctNode,
        "gct.if.else");
    *brchNode << elseCase;
    if (elseNode) {
        // If an "else" block exists, process and attach it
        enterArmOf(ast, valueUnused);
        node_ptr_t elseBlock = visitStmtBlock(elseNode);
        *elseCase << elseBlock;
    } else {
        // empty else block
        // If no "else" block exists, attach an empty execution block
        *elseCase << makeExec();
    }
    registerGctSemantic(
        context_,
        brchNode,
        ast,
        {
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(condNode),
                -1,
                "condition"),
            semanticPart(
                camel::source::SemanticRole::BranchTarget,
                nodeOrigin(trueCase),
                0,
                "then"),
            semanticPart(
                camel::source::SemanticRole::BranchTarget,
                nodeOrigin(elseCase),
                1,
                "else"),
        },
        {nodeOrigin(condNode), nodeOrigin(trueCase), nodeOrigin(elseCase)});
    LEAVE("IfExpr");
    return brchNode;
}

/*
LoopExpr(state, binder) : Repeated Data inits, Optional Data sequence, Optional Data condition,
                          StmtBlock body ;
A loop (design principles 3.10) becomes one LOOP node (camel/compile/gct/node.h,
Node::loopKind): the first values and the sequence are expressions where the loop is; the
condition and the body are scopes of their own, the way the arms of an `if` are, each with
the numeric permissions in force here (7.5: a loop body written in a block or a function
under @numeric is part of it). The names of the state and of the element are declared by the
graph builder, which gives the condition and the body their own graphs.
*/
node_ptr_t Builder::visitLoopExpr(const AST::node_ptr_t &ast) {
    ENTER("LoopExpr");
    const auto &load    = ast->loadAs<AST::LoopExprLoad>();
    const auto &condAst = ast->optAtAs<AST::DataLoad>(2);
    const bool hasFor   = load->hasFor();
    const auto kind = hasFor
                          ? (condAst ? camel::core::LoopKind::ForWhile : camel::core::LoopKind::For)
                          : camel::core::LoopKind::While;
    std::vector<std::string> state;
    for (const Reference &name : load->state()) {
        state.push_back(name.toString());
    }
    node_ptr_t loopNode =
        tree_->makeLoop(kind, state, hasFor ? load->binder().toString() : std::string());
    setOriginFromAst(context_, loopNode, ast, camel::source::OriginKind::GctNode, "gct.loop");
    const ValueUseScope valueUseScope(*this);
    valueUse_ = {};

    // The first values, then the sequence: evaluated once, where the loop is.
    for (const auto &init : *ast->atAs<AST::RepeatedLoad>(0)) {
        *loopNode << visitData(init);
    }
    if (hasFor) {
        *loopNode << visitData(ast->optAtAs<AST::DataLoad>(1));
    }
    // The condition: a scope of its own whose one statement is the condition.
    if (condAst) {
        node_ptr_t condExec = makeExec();
        setOriginFromAst(
            context_,
            condExec,
            condAst,
            camel::source::OriginKind::GctNode,
            "gct.loop.condition");
        *condExec << visitData(condAst);
        *loopNode << condExec;
    }
    // The body: its value is the next state, so a `return` in it ends the body with that
    // value, as in an arm whose value is used.
    enterArmOf(ast, false);
    *loopNode << visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(3));
    registerGctSemantic(context_, loopNode, ast);
    LEAVE("LoopExpr");
    return loopNode;
}

/*

*/
node_ptr_t Builder::visitMatchExpr(const AST::node_ptr_t &ast) {
    ENTER("MatchExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchExpr");

    // The subject is any expression, evaluated once before the cases; a name among them is
    // read here, once, and the arms read the value through the branch.
    const auto &subjectAst = ast->atAs<AST::DataLoad>(0);
    const bool valueUnused = std::exchange(valueUse_.unusedCtrl, nullptr) == ast.get();
    const ValueUseScope valueUseScope(*this);

    node_ptr_t subjectNode = visitData(subjectAst);
    node_ptr_t brchNode    = tree_->makeBrch();
    setOriginFromOrigin(
        context_,
        subjectNode,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchCondition),
        camel::source::OriginKind::GctNode,
        "gct.match.scrutinee");
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.match");
    *brchNode << subjectNode;
    const node_ptr_t dRefNode = subjectNode;

    std::vector<camel::source::SemanticPart> parts = {
        semanticPart(
            camel::source::SemanticRole::BranchCondition,
            nodeOrigin(dRefNode),
            -1,
            "scrutinee"),
    };
    std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(dRefNode)};

    for (const auto &aCaseNode : *ast->atAs<AST::RepeatedLoad>(1)) {
        // Handle a value case
        const auto &caseLoadType = aCaseNode->front()->type();
        node_ptr_t gCaseNode     = nullptr;

        if (caseLoadType == AST::LoadType::Data &&
            aCaseNode->front()->loadAs<AST::DataLoad>()->dataType() == AST::DataType::EnumPattern) {
            // `case Shape::Rect { w, h }`: the variant is written by its name and the fields
            // the pattern binds; the graph builder resolves it against the enum matched.
            const auto &pattern = aCaseNode->front()->loadAs<AST::EnumPatternLoad>();
            std::vector<std::string> fields, bindings;
            for (const AST::EnumPatternField &field : pattern->fields()) {
                fields.push_back(field.name);
                bindings.push_back(field.binding);
            }
            gCaseNode = tree_->makeEnumCase(
                pattern->ref().toString(),
                fields,
                bindings,
                pattern->structShape());
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        } else if (
            caseLoadType == AST::LoadType::Data &&
            aCaseNode->front()->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Ref) {
            // `case v` / `case error e`: the fixed patterns over a `T?` / `T!` (design
            // principles 3.8). The graph builder, which knows the scrutinee's type, binds
            // the name in the arm or reports the pattern.
            const auto &pattern    = aCaseNode->front();
            const bool isError     = pattern->size() > 0;
            const std::string name = (isError ? pattern->front() : pattern)
                                         ->loadAs<AST::RefDataLoad>()
                                         ->ref()
                                         .toString();
            gCaseNode = tree_->makeCase(isError ? CaseType::Error : CaseType::Bind, name);
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        } else if (caseLoadType != AST::LoadType::Null) {
            gCaseNode               = tree_->makeCase(CaseType::Value);
            node_ptr_t caseDataNode = visitData(aCaseNode->atAs<AST::DataLoad>(0));
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseDataNode << caseExprNode;
            mergedInputs.push_back(nodeOrigin(caseDataNode));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::CaseValue,
                nodeOrigin(caseDataNode),
                static_cast<int32_t>(parts.size()),
                "caseValue"));
        } else {
            // Handle the "else" case
            gCaseNode = tree_->makeCase(CaseType::Else);
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        }

        setOriginFromOrigin(
            context_,
            gCaseNode,
            aCaseNode->load()->origin(),
            camel::source::OriginKind::GctNode,
            "gct.match.case");
        mergedInputs.push_back(nodeOrigin(gCaseNode));
        parts.push_back(semanticPart(
            camel::source::SemanticRole::BranchTarget,
            nodeOrigin(gCaseNode),
            static_cast<int32_t>(parts.size()),
            "case"));

        // Attach the case node to the branch node
        *brchNode << gCaseNode;
    }
    registerGctSemantic(context_, brchNode, ast, std::move(parts), std::move(mergedInputs));
    LEAVE("MatchExpr");
    return brchNode;
}

/*


*/
node_ptr_t Builder::visitTryExpr(const AST::node_ptr_t &ast) {
    ENTER("TryExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for TryExpr");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("TryExpr");
    throw BuildAbortException();
    LEAVE("TryExpr");
    return nullptr;
}

template <typename T> inline data_ptr_t makeDataFromLiteral(const T &value) {
    return tt::as_shared<Data>(make_shared<PrimaryData<T>>(value));
}

/*
Literal(Value value) ;
enum LiteralType {
    String,
    FString,
    Integer,
    Real,
    Boolean,
    Null,
};
*/
node_ptr_t Builder::visitLiteral(const AST::node_ptr_t &ast) {
    ENTER("Literal");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for Literal");

    // Extract the literal load and its value
    const auto &literal  = ast->loadAs<AST::LiteralLoad>();
    const Literal &value = literal->value();
    const auto &str      = value.data();
    data_ptr_t data;

    // Handle different literal types
    switch (value.type()) {
    case LiteralType::String: {
        // Decode escaped characters in the string and create a StringData object
        std::string decoded = decodeEscapes(str);
        // A string is always valid UTF-8 (design principles 11.1); `'\xE4'` alone is not.
        // Write the character itself or `\u{4E2D}`; a byte sequence is a `Bytes` value.
        if (!camel::utf8::isValid(decoded)) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("string (not valid UTF-8; use \\u{...} or the character itself)", str);
            throw BuildAbortException();
        }
        data = tt::as_shared<Data>(make_shared<StringData>(decoded));
    } break;
    case LiteralType::FString: {
        // FString (formatted string) is not supported
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("FString");
        throw BuildAbortException();
    } break;
    case LiteralType::Integer: {
        // Exact arithmetic. A literal that an int cannot hold used to wrap around
        // (`9223372036854775808` was the smallest int, as was `1e400`).
        const IntegerLiteral parsed = parseIntegerLiteral(str);
        if (parsed.status == IntegerLiteralStatus::OutOfRange) {
            diags_->of(SemanticDiag::LiteralOutOfRange)
                .at(ast->load()->tokenRange())
                .commit(str, "int", "-9223372036854775808 to 9223372036854775807");
            throw BuildAbortException();
        }
        if (parsed.status == IntegerLiteralStatus::Invalid) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("integer", str);
            throw BuildAbortException();
        }
        if (parsed.inexact) {
            // `1e-2` is the int 0. Whether such a literal should be a float or an error is
            // not decided; until then the dropped fraction is at least said.
            std::string asReal = str;
            if (const size_t e = asReal.find_first_of("eE"); e != std::string::npos) {
                asReal.insert(e, ".0");
            }
            diags_->of(SemanticDiag::IntegerLiteralTruncated)
                .at(ast->load()->tokenRange())
                .commit(str, std::to_string(parsed.value), asReal);
        }
        data = makeDataFromLiteral(parsed.value);
    } break;
    case LiteralType::Real: {
        // The double nearest to the number written. One too large for a double was an
        // exception that reached the user as bare text.
        try {
            data = makeDataFromLiteral(parseNumber<double>(str));
        } catch (const std::overflow_error &) {
            diags_->of(SemanticDiag::LiteralOutOfRange)
                .at(ast->load()->tokenRange())
                .commit(str, "float", "at most about 1.8e308 in magnitude");
            throw BuildAbortException();
        } catch (const std::invalid_argument &) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("real", str);
            throw BuildAbortException();
        }
    } break;
    case LiteralType::Boolean: {
        // Handle boolean literals ("true" or "false")
        if (str == "true") {
            data = makeDataFromLiteral(true);
        } else if (str == "false") {
            data = makeDataFromLiteral(false);
        } else {
            // Invalid boolean literal
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("boolean", str);
            throw BuildAbortException();
        }
    } break;
    case LiteralType::Null: {
        data = Data::null();
    } break;
    default: {
        ASSERT(false, "Unknown literal type");
        throw BuildAbortException();
    }
    }

    // Create a DataLoad node to represent the literal and return it
    node_ptr_t res = tree_->makeData(data);
    LEAVE("Literal");
    return res;
}

/*
ArrayData() : Data* data ;
*/
node_ptr_t Builder::visitArrayData(const AST::node_ptr_t &ast) {
    ENTER("ArrayData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ArrayData");
    if (ast->size() > 1) {
        node_ptr_t res = visitComprehension(ast);
        LEAVE("ArrayData");
        return res;
    }

    ArrayDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);

    // `[1, null, 3]`: a `null` among other elements makes the array one of `T?` (design
    // principles 3.8), whose elements are put into that form when the program runs. Which
    // `T` is known to the GIR builder only, so every element is left to it as a computed one
    // (visitDataNode joins their types and adds the CASTs).
    const auto &items   = ast->atAs<AST::RepeatedLoad>(0);
    bool hasNullLiteral = false;
    for (const auto &item : *items) {
        hasNullLiteral |= item->type() == AST::LoadType::Data &&
                          item->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Literal &&
                          item->loadAs<AST::LiteralLoad>()->value().type() == LiteralType::Null;
    }
    const bool allComputed = hasNullLiteral && items->size() > 1;

    for (const auto &item : *items) {
        node_ptr_t dataNode = visitData(item);
        if (allComputed) {
            auto [refNode, refData] = makeRefData(dataNode);
            *execNode << refNode;
            dangling = true;
            factory.add(refData);
            continue;
        }
        auto [data, _] = extractData(dataNode, execNode, dangling);
        try {
            factory.add(data);
        } catch (Diagnostic &d) {
            // The element that does not fit the others: the data layer knows the types, the
            // position is known here.
            d.range = item->load()->tokenRange();
            diags_->add(std::move(d));
            throw BuildAbortException();
        }
    }

    auto arrayData = factory.build();
    node_ptr_t res = tree_->makeData(arrayData);

    if (dangling) {
        *execNode << res;
        res = execNode;
    }

    LEAVE("ArrayData");
    return res;
}

node_ptr_t Builder::visitComprehension(const AST::node_ptr_t &ast) {
    const AST::node_ptr_t &elementAst = ast->at(1);
    const AST::node_ptr_t &nameAst    = ast->at(2);
    const AST::node_ptr_t &sourceAst  = ast->at(3);
    const AST::node_ptr_t conditionAst =
        ast->at(4)->empty() ? AST::node_ptr_t{} : ast->at(4)->at(0);
    const Reference nameRef = nameAst->loadAs<AST::RefDataLoad>()->ref();
    if (!validateIdent(nameRef.ident())) {
        diags_->of(SemanticDiag::ReservedIdentifier).at(nameAst->load()->tokenRange()).commit();
        throw BuildAbortException();
    }
    const auto place = [&](const node_ptr_t &node, const AST::node_ptr_t &at, const char *what) {
        setOriginFromAst(context_, node, at, camel::source::OriginKind::GctNode, what);
        return node;
    };
    // The function `(x) => body`: what a lambda with an expression body is, with the type of
    // `x` left to the call that receives it (a type variable no program can write).
    const auto elementFunction = [&](const AST::node_ptr_t &body) {
        FunctionTypeFactory factory;
        factory.addNormArg(
            nameRef.ident(),
            TypeVarType::create("$element" + std::to_string(idIndex_++), true),
            false);
        FunctionType *type  = factory.buildOpen(); // a function being defined: see openSignature_
        node_ptr_t typeNode = place(tree_->makeType(type, type->implMark()), body, "gct.func.type");
        const ValueUseScope valueUseScope(*this);
        valueUse_           = {};
        node_ptr_t bodyNode = place(makeExec(false), body, "gct.block");
        *bodyNode << visitData(body);
        node_ptr_t funcNode = place(tree_->makeFunc(std::string()), body, "gct.func");
        *funcNode << typeNode << bodyNode;
        return funcNode;
    };
    // `value->op<function>`.
    const auto apply = [&](const char *op, const node_ptr_t &function, const node_ptr_t &value) {
        node_ptr_t withNode = place(tree_->makeWith(1), ast, "gct.with");
        *withNode << place(tree_->makeDRef(op), ast, "gct.dref") << function;
        node_ptr_t linkNode = place(tree_->makeLink(1), ast, "gct.link");
        *linkNode << withNode << value;
        return linkNode;
    };
    node_ptr_t result = visitData(sourceAst);
    if (conditionAst) {
        result = apply("$builtin::filter", elementFunction(conditionAst), result);
    }
    // The two operators themselves, not what the names `filter` and `map` stand for where
    // the comprehension is written: a function of the program with one of those names must
    // not change what a comprehension means (the GIR builder resolves the reserved prefix
    // in the built-in module, visitDRefNode).
    return apply("$builtin::map", elementFunction(elementAst), result);
}

namespace {

/// The element type of the array type `type` when a literal written where `type` is expected
/// has elements to put into `T?` / `T!` (directly, or in arrays nested in it); else nullptr.
Type *taggedElementTarget(Type *type) {
    if (type == nullptr || type->code() != TypeCode::Array || containsTypeVar(type)) {
        return nullptr;
    }
    Type *elem = tt::as_ptr<ArrayType>(type)->elemType();
    return asTagged(elem) != nullptr || taggedElementTarget(elem) != nullptr ? elem : nullptr;
}

} // namespace

node_ptr_t Builder::declaredValue(const node_ptr_t &value, Type *type, const AST::node_ptr_t &ast) {
    // A literal of the declared type, converted now (`let x: float = 1`). A declared `T?` /
    // `T!` and a type written with a type variable (decided when the generic function is
    // instantiated) are left to the CAST, and so is a function type, which a literal cannot
    // have.
    if (value->type() == NodeKind::DATA && asTagged(type) == nullptr && !containsTypeVar(type)) {
        if (data_ptr_t converted = value->data()->convertTo(type)) {
            return tree_->makeData(converted);
        }
        reportLiteralCastFailure(diags_, ast->load()->tokenRange(), value->data(), type);
        throw BuildAbortException();
    }
    // Any other expression: an annotation CAST (see the declaration). An expression of the
    // declared type used to be refused here as "dynamic type casting" (`var acc: V = p`
    // with `p: V`); converting a value to its own type does nothing (design principles 5.1).
    node_ptr_t castNode = tree_->makeCast(type, true);
    *castNode << value;
    setOriginFromAst(context_, castNode, ast, camel::source::OriginKind::GctNode, "gct.cast");
    return castNode;
}

node_ptr_t Builder::visitDataInto(const AST::node_ptr_t &ast, Type *type, bool &entered) {
    entered        = false;
    Type *elemType = taggedElementTarget(type);
    if (elemType == nullptr || ast->type() != AST::LoadType::Data ||
        ast->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Array ||
        ast->atAs<AST::RepeatedLoad>(0)->size() == 0) {
        return visitData(ast);
    }
    // Design principles 3.8: the elements of an array literal are a position where a value
    // enters `T?` / `T!` when the element type is known, as it is here. Each element is the
    // value under a CAST to the element type (the conversion an annotated `let` writes), so
    // the array is built from computed elements of exactly that type.
    entered = true;
    ArrayDataFactory factory;
    node_ptr_t execNode = makeExec(false, true);
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(item, elemType, nested);
        if (asTagged(elemType) != nullptr) {
            node_ptr_t castNode = tree_->makeCast(elemType, true);
            *castNode << dataNode;
            setOriginFromAst(
                context_,
                castNode,
                item,
                camel::source::OriginKind::GctNode,
                "gct.cast");
            dataNode = castNode;
        } else if (!nested && dataNode->type() == NodeKind::DATA) {
            // An inner literal without elements to put into a form (`[]`): the literal of
            // the element type.
            const auto &data = dataNode->data();
            if (auto converted = data->convertTo(elemType)) {
                dataNode = tree_->makeData(converted);
            }
        }
        auto [refNode, refData] = makeRefData(dataNode);
        *execNode << refNode;
        factory.add(refData);
    }
    *execNode << tree_->makeData(factory.build());
    return execNode;
}

/*
StructData() : NamedData* dataList ;
*/
node_ptr_t Builder::visitStructData(const AST::node_ptr_t &ast) {
    ENTER("StructData");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StructData");

    StructDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    std::unordered_set<string> fieldNames;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name    = namedPair->ref().ident();
        if (!fieldNames.insert(name).second) {
            // As in a struct type. The later value used to be dropped, silently.
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedPair->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        node_ptr_t dataNode = visitData(child->atAs<AST::DataLoad>(0));
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        factory.add(name, data);
    }

    auto structData = factory.build();
    node_ptr_t res  = tree_->makeData(structData);

    // If there are dangling nodes, attach them to the execution node
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("StructData");
    return res;
}

/*
TupleData() : Data* data ;
*/
node_ptr_t Builder::visitTupleData(const AST::node_ptr_t &ast) {
    ENTER("TupleData");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for TupleData");

    TupleDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        factory.add(data);
    }

    auto tupleData = factory.build();
    node_ptr_t res = tree_->makeData(tupleData);

    // If there are dangling nodes, attach them to the execution node
    if (dangling) {
        *execNode << res;
        res = execNode;
    }

    LEAVE("TupleData");
    return res;
}

/*
FuncData(Ref ref) : FuncType funcType, StmtBlock body ;
*/
node_ptr_t Builder::visitFuncData(const AST::node_ptr_t &ast) {
    ENTER("FuncData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for FuncData");
    const auto &funcData = ast->loadAs<AST::FuncDataLoad>();
    // The type variables a declaration binds are in scope in the rest of its signature and in
    // its body, and nowhere else.
    const bool isDecl = std::exchange(funcDeclPending_, false);
    struct TypeScopeGuard {
        Builder &self;
        scope_ptr_t<Reference, Type *> outer;
        std::vector<std::string> outerVars;
        ~TypeScopeGuard() {
            self.typeScope_        = outer;
            self.declaredTypeVars_ = std::move(outerVars);
        }
    } typeScopeGuard{*this, typeScope_, std::exchange(declaredTypeVars_, {})};
    pushScope();
    declSignature_ = isDecl;
    openSignature_ = true;
    FunctionType *funcType =
        tt::as_ptr<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    // A parameter of a reserved name would stand in front of the operator of that name.
    for (size_t i = 0; i < funcType->argNamesCount(); ++i) {
        if (!validateIdent(std::string(funcType->argNameAt(i)))) {
            diags_->of(SemanticDiag::ReservedIdentifier)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit();
            throw BuildAbortException();
        }
    }
    if (!declaredTypeVars_.empty()) {
        // A variable must survive type construction in an argument or result position.
        std::vector<std::string> bindable;
        for (Type *param : funcType->withTypesSpan()) {
            collectBindableTypeVars(param, bindable);
        }
        for (Type *param : funcType->normTypesSpan()) {
            collectBindableTypeVars(param, bindable);
        }
        if (funcType->hasExitType()) {
            collectBindableTypeVars(funcType->exitType(), bindable);
        }
        for (const std::string &name : declaredTypeVars_) {
            if (std::find(bindable.begin(), bindable.end(), name) == bindable.end()) {
                diags_->of(SemanticDiag::TypeVariableNotBindable)
                    .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                    .commit(
                        name,
                        funcData->ref().toString(),
                        "its 'typeas' is inside a union, or is the argument of a type function "
                        "that does not use it");
                throw BuildAbortException();
            }
        }
    }
    node_ptr_t typeNode = tree_->makeType(funcType, funcType->implMark());
    // The body of a function or lambda is a body of its own: its value is the function's
    // result, and its exits are the function's, wherever the function is written.
    const ValueUseScope valueUseScope(*this);
    valueUse_            = {};
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    node_ptr_t funcNode  = tree_->makeFunc(funcData->ref().toString());
    *funcNode << typeNode << stmtsNode;
    setOriginFromAst(context_, typeNode, ast, camel::source::OriginKind::GctNode, "gct.func.type");
    setOriginFromAst(context_, funcNode, ast, camel::source::OriginKind::GctNode, "gct.func");
    registerGctSemantic(
        context_,
        funcNode,
        ast,
        {
            semanticPart(
                camel::source::SemanticRole::FuncName,
                findSemanticOrigin(
                    astSemantic(context_, ast),
                    camel::source::SemanticRole::FuncName),
                -1,
                funcData->ref().toString()),
            semanticPart(camel::source::SemanticRole::ReturnType, nodeOrigin(typeNode), -1, "type"),
            semanticPart(
                camel::source::SemanticRole::ValueProducer,
                nodeOrigin(stmtsNode),
                -1,
                "body"),
        },
        {nodeOrigin(typeNode), nodeOrigin(stmtsNode)});
    LEAVE("FuncData");
    return funcNode;
}

/*
RefData(Ref ref) ;
*/
node_ptr_t Builder::visitRefData(const AST::node_ptr_t &ast) {
    ENTER("RefData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for RefData");
    const auto &refData = ast->loadAs<AST::RefDataLoad>();
    node_ptr_t refNode  = tree_->makeDRef(refData->ref());
    LEAVE("RefData");
    return refNode;
}

/*
Type(TypeType type) :=
    NullableType | TypeExpr | ArrayType | StructType | TupleType
    | FuncType | SpecType | UnitType | InferType | DataType | RefType ;
*/
Type *Builder::visitType(const AST::node_ptr_t &ast) {
    ENTER("Type");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for visitType");

    // Extract the type load from the AST node
    const auto &type = ast->loadAs<AST::TypeLoad>();
    Type *res;
    // Whether this type is the base of `Name<...>`, the one place a type function may stand.
    const bool asTypeFunctionBase = std::exchange(typeFunctionBase_, false);

    // Handle different type categories based on the TypeType enumeration
    switch (type->typeType()) {
    case AST::TypeType::Null:
        res = visitNullableType(ast);
        break;
    case AST::TypeType::Expr:
        res = visitTypeExpr(ast);
        break;
    case AST::TypeType::Array:
        res = visitArrayType(ast);
        break;
    case AST::TypeType::Struct:
        res = visitStructType(ast);
        break;
    case AST::TypeType::Tuple:
        res = visitTupleType(ast);
        break;
    case AST::TypeType::Func:
        res = visitFuncType(ast);
        break;
    case AST::TypeType::Spec:
        res = visitSpecType(ast);
        break;
    case AST::TypeType::Unit:
        res = visitUnitType(ast);
        break;
    case AST::TypeType::Infer:
        res = visitInferType(ast);
        break;
    case AST::TypeType::Data:
        res = visitDataType(ast);
        break;
    case AST::TypeType::Ref:
        res = visitRefType(ast);
        break;
    case AST::TypeType::Enum:
        // An enum is declared, not written where a value of it is expected: its variants are
        // names of the module (`type Shape = enum {...}`), and a use is that name.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .at(ast->load()->tokenRange())
            .commit(
                "enum { ... }",
                "an enum is declared in a type declaration, 'type Shape = enum { ... }', and "
                "its type is written by the name it declares");
        throw BuildAbortException();
    default:
        throw std::runtime_error("Unknown type type");
    }

    // `Map` and `Set` are always written with their arguments.
    if (const CollectionType *coll = res ? asCollectionTypeOrPrototype(res) : nullptr;
        coll != nullptr && !coll->complete() && !asTypeFunctionBase) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(
                coll->toString(),
                coll->isMap() ? "it is written Map<K, V>, with a key type and a value type"
                              : "it is written Set<T>, with an element type");
        throw BuildAbortException();
    }

    if (res != nullptr && res->code() == TypeCode::TypeFunc && !asTypeFunctionBase) {
        const auto *typeFunc = static_cast<TypeFunctionType *>(res);
        diags_->of(SemanticDiag::TypeFunctionArguments)
            .at(ast->load()->tokenRange())
            .commit(
                typeFunc->toString(),
                std::format(
                    "it takes {} type argument(s) and is used without any",
                    typeFunc->params().size()));
        throw BuildAbortException();
    }

    if (auto *recorder = context_ ? context_->semanticRecorder() : nullptr) {
        recorder->typeExpression(ast->load()->origin(), res);
    }
    LEAVE("Type");
    return res;
}

/*
NullableType : Type type ;
*/
Type *Builder::visitNullableType(const AST::node_ptr_t &ast) {
    ENTER("NullableType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for NullableType");
    Type *payload       = visitType(ast->atAs<AST::TypeLoad>(0));
    const bool fallible = ast->loadAs<AST::NullableTypeLoad>()->fallible();
    // The same mark twice (`T??`, `T!!`) adds nothing a program could tell apart; `T?!` and
    // `T!?` are allowed (design principles 3.8).
    if (TupleType *inner = asTagged(payload); inner && inner->isFallible() == fallible) {
        diags_->of(SemanticDiag::TaggedTypeNesting)
            .at(ast->load()->tokenRange())
            .commit(payload->toString() + (fallible ? "!" : "?"), inner->payloadType()->toString());
        throw BuildAbortException();
    }
    Type *type = fallible ? TupleType::fallible(payload) : TupleType::nullable(payload);
    LEAVE("NullableType");
    return type;
}

/*
TypeExpr(TypeOp op) := Type lhs, Type rhs ;
enum TypeOp {
    Union,
    Inter,
    Diff,
    KeyUnion,
    KeyInter,
    KeyDiff,
    ErrorThen,
    Specialize, // Type type, (Type | Data)* args
    TypeOf,
    TypeAs
}
*/
Type *Builder::visitTypeExpr(const AST::node_ptr_t &ast) {
    ENTER("TypeExpr");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TypeExpr");

    Type *res;
    const auto &typeExpr = ast->loadAs<AST::TypeExprLoad>();
    AST::TypeOp op       = typeExpr->op();

    // Handle different type operations based on the TypeOp enumeration
    switch (op) {
    case AST::TypeOp::Union: {
        // `A | B` is retired: a value has one type, and a value that is one of several named
        // cases is an enum (design principles 5.3). The AST keeps the form so that the
        // program is told so, instead of failing to parse.
        diags_->of(SemanticDiag::UnionTypeRetired).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    } break;

    case AST::TypeOp::Inter: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::Inter");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::Diff: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::Diff");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::KeyUnion: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::KeyUnion");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::KeyInter: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::KeyInter");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::KeyDiff: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::KeyDiff");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::ErrorThen: {
        diags_->of(SemanticDiag::ReservedTypeQuestionRetired)
            .at(ast->load()->tokenRange())
            .commit();
        throw BuildAbortException();
    } break;

    case AST::TypeOp::Specialize: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::Specialize (a.k.a. Generics or T<U, V, ...>)");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::TypeOf: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::TypeOf");
        throw BuildAbortException();
    } break;

    case AST::TypeOp::TypeAs: {
        // `typeas T`: the type found at this position of an argument type becomes `T`.
        const Reference &ref = ast->atAs<AST::TypeLoad>(0)->loadAs<AST::RefTypeLoad>()->ref();
        const string name    = ref.toString();
        if (!typeasAllowed_) {
            diags_->of(SemanticDiag::TypeVariableNotAllowed)
                .at(ast->load()->tokenRange())
                .commit(
                    name,
                    inDeclReturnType_ ? "a return type has no argument to take the type from"
                                      : "this is not a parameter of a function declaration");
            throw BuildAbortException();
        }
        if (typeScope_->get(ref).has_value() || module_->getImportedType(ref).has_value()) {
            diags_->of(SemanticDiag::TypeVariableRedeclared)
                .at(ast->load()->tokenRange())
                .commit(name);
            throw BuildAbortException();
        }
        typeScope_->insert(ref, TypeVarType::create(name, false));
        declaredTypeVars_.push_back(name);
        res = TypeVarType::create(name, true);
    } break;

    default:
        ASSERT(false, "Unknown TypeOp");
    }

    LEAVE("TypeExpr");
    return res;
}

/*
ArrayType(siz dim) : Type type ;
*/
Type *Builder::visitArrayType(const AST::node_ptr_t &ast) {
    ENTER("ArrayType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for ArrayType");
    // `T[][]` reaches here as one node of two dimensions: an array of arrays of T. One array
    // type per dimension; a single one made `T[][]` mean `T[]`.
    const auto &arrayTypeLoad = ast->loadAs<AST::ArrayTypeLoad>();
    Type *type                = visitType(ast->atAs<AST::TypeLoad>(0));
    type                      = ArrayType::create(type);
    for (size_t dim = 1; dim < arrayTypeLoad->dims(); ++dim) {
        type = ArrayType::create(type);
    }
    LEAVE("ArrayType");
    return type;
}

/*
StructType() : NamedType* types ;
*/
Type *Builder::visitStructType(const AST::node_ptr_t &ast) {
    ENTER("StructType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for StructType");

    StructTypeFactory factory;
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedType = child->loadAs<AST::NamedTypeLoad>();
        const string &name    = namedType->getRef().ident();
        Type *type            = visitType(child->atAs<AST::TypeLoad>(0));

        if (!factory.add(name, type)) {
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedType->tokenRange()).commit(name);
            throw BuildAbortException();
        }
    }
    LEAVE("StructType");
    return factory.build();
}
/*
TupleType() : Type* types ;
*/
Type *Builder::visitTupleType(const AST::node_ptr_t &ast) {
    ENTER("TupleType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TupleType");

    // Create a vector to store the types of the tuple elements
    vector<Type *> types;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        Type *type = visitType(child);
        types.push_back(type);
    }
    // Create a TupleType object using the collected types
    Type *tupleType = TupleType::create(types);
    LEAVE("TupleType");
    return tupleType;
}

/*
FuncType(Modifier[] modifiers, ImplMark impl, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
Type *Builder::visitFuncType(const AST::node_ptr_t &ast) {
    ENTER("FuncType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for FuncType");

    // Extract the function type load from the AST node
    auto const &typeLoad = ast->loadAs<AST::FuncTypeLoad>();

    // A declaration binds variables in its parameters or return type (design principles
    // 5.9). Return-only variables come from the expected type or explicit ::<T> arguments.
    const bool signature = std::exchange(declSignature_, false);
    const bool open      = std::exchange(openSignature_, false);
    struct FlagGuard {
        bool &allowed, &inReturn;
        bool outerAllowed, outerInReturn;
        ~FlagGuard() {
            allowed  = outerAllowed;
            inReturn = outerInReturn;
        }
    } flagGuard{typeasAllowed_, inDeclReturnType_, typeasAllowed_, inDeclReturnType_};
    if (signature) {
        typeasAllowed_    = true;
        inDeclReturnType_ = false;
    }

    FunctionTypeFactory factory;
    factory.setImplMark(typeLoad->implMark());
    factory.setModifiers(typeLoad->modifiers());

    std::unordered_set<string> paramNames;

    // Process "with" parameters (context parameters)
    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        bool isVar            = paramLoad->isVar();
        if (isVar && !typeLoad->modifiers().sync()) {
            // Variable parameters are not allowed in asynchronous functions
            diags_->of(SemanticDiag::VarParamInAsyncFunction).at(paramLoad->tokenRange()).commit();
        }
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.plain()) {
            // Parameter references must be unqualified
            diags_->of(SemanticDiag::ParamRefMustBeUnqualified)
                .at(paramLoad->tokenRange())
                .commit(paramRef.toString());
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        Type *type         = nullptr;
        {
            TypePositionGuard position(*this, TypePosition::WithParameter);
            type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        }
        data_ptr_t data = nullptr;

        // Process optional default values for parameters
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                // Default parameter values must be static
                diags_->of(SemanticDiag::ParamDataMustBeStatic)
                    .at(dataNode->load()->tokenRange())
                    .commit(paramRef.toString());
                throw BuildAbortException();
            }
            // Default parameter values are not supported
            diags_->of(SemanticDiag::FeatureNotSupported)
                .at(dataNode->load()->tokenRange())
                .commit("default parameter values");
            throw BuildAbortException();
        }

        if (!paramNames.insert(name).second) {
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        factory.addWithArg(name, type, isVar);
    }

    // Process normal parameters
    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(1)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        bool isVar            = paramLoad->isVar();
        if (isVar && !typeLoad->modifiers().sync()) {
            // variable parameters are not allowed in asynchronous functions
            diags_->of(SemanticDiag::VarParamInAsyncFunction).at(paramLoad->tokenRange()).commit();
        }
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.plain()) {
            // Parameter references must be unqualified
            diags_->of(SemanticDiag::ParamRefMustBeUnqualified)
                .at(paramLoad->tokenRange())
                .commit(paramRef.toString());
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        Type *type         = nullptr;
        {
            TypePositionGuard position(*this, TypePosition::Parameter);
            type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        }
        data_ptr_t data = nullptr;

        // Process optional default values for parameters
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                // Default parameter values must be static
                diags_->of(SemanticDiag::ParamDataMustBeStatic)
                    .at(dataNode->load()->tokenRange())
                    .commit(paramRef.toString());
                throw BuildAbortException();
            }
            // Default parameter values are not supported
            diags_->of(SemanticDiag::FeatureNotSupported)
                .at(dataNode->load()->tokenRange())
                .commit("default parameter values");
            throw BuildAbortException();
        }

        if (!paramNames.insert(name).second) {
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        factory.addNormArg(name, type, isVar);
    }

    // The return type may name existing variables or bind return-only ones.
    const auto &exitTypeNode = ast->optAtAs<AST::TypeLoad>(2);
    if (exitTypeNode) {
        if (signature) {
            typeasAllowed_    = true;
            inDeclReturnType_ = true;
        }
        TypePositionGuard position(*this, TypePosition::ReturnType);
        factory.setExitType(visitType(exitTypeNode));
    }

    LEAVE("FuncType");
    return open ? static_cast<Type *>(factory.buildOpen()) : factory.build();
}

/*
UnitType(Ref ref) : Type type ;
*/
Type *Builder::visitUnitType(const AST::node_ptr_t &ast) {
    ENTER("UnitType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("UnitType");
    throw BuildAbortException();
    LEAVE("UnitType");
    return nullptr;
}

/*
InferType(Ref ref) ;
*/
Type *Builder::visitInferType(const AST::node_ptr_t &ast) {
    ENTER("InferType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("InferType");
    throw BuildAbortException();
    LEAVE("InferType");
    return nullptr;
}

/*
DataType() : Data data ;
*/
Type *Builder::visitDataType(const AST::node_ptr_t &ast) {
    ENTER("DataType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("DataType");
    throw BuildAbortException();
    LEAVE("DataType");
    return nullptr;
}

// DESIGN-QUESTION(map-duplicate-keys): in an array literal written under `as Map<...>`, two
// equal constant keys are an error. Entries that are not constants (they refer to a name) are
// not looked at; at run time the later entry of two equal keys wins. A set takes duplicates.
void Builder::rejectDuplicateLiteralKeys(
    const data_ptr_t &data, Type *targetType, const AST::node_ptr_t &ast) {
    const CollectionType *coll = asCollectionTypeOrPrototype(targetType);
    const auto array           = std::dynamic_pointer_cast<ArrayData>(data);
    if (coll == nullptr || !coll->isMap() || !array) {
        return;
    }
    const auto holes = array->holes();
    // Candidates by their text, so that a long literal is not compared pair by pair.
    std::unordered_map<std::string, std::vector<data_ptr_t>> seen;
    for (size_t i = 0; i < array->raw().size(); ++i) {
        if (std::find(holes.begin(), holes.end(), i) != holes.end()) {
            continue;
        }
        const auto entry = std::dynamic_pointer_cast<TupleData>(array->raw()[i]);
        if (!entry || !entry->resolved() || entry->raw().size() != 2 || !entry->raw()[0]) {
            continue;
        }
        const data_ptr_t &key             = entry->raw()[0];
        std::vector<data_ptr_t> &sameText = seen[key->toString()];
        for (const data_ptr_t &earlier : sameText) {
            if (earlier->equals(key)) {
                diags_->of(SemanticDiag::DuplicateCollectionKey)
                    .at(ast->load()->tokenRange())
                    .commit(key->toString(), targetType->toString());
                throw BuildAbortException();
            }
        }
        sameText.push_back(key);
    }
}

/*
SpecType : Type base, RepeatedLoad typeOrData ;
*/
Type *Builder::visitSpecType(const AST::node_ptr_t &ast) {
    ENTER("SpecType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for SpecType");
    ASSERT(ast->size() >= 1, "SpecType must have base type");
    typeFunctionBase_ = ast->size() >= 2;
    Type *base        = visitType(ast->atAs<AST::TypeLoad>(0));
    if (ast->size() < 2) {
        LEAVE("SpecType");
        return base;
    }
    // The arguments in source order: types, and constants written `const <data>`. What a
    // constant means is the type's business (OtherType::cloneWithArguments); an argument the
    // type does not accept is an error here, never dropped.
    std::vector<TypeArgument> arguments;
    const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
    for (const auto &child : *argsNode) {
        if (child->type() == AST::LoadType::Type) {
            arguments.push_back(TypeArgument{.type = visitType(child), .data = nullptr});
            continue;
        }
        const data_ptr_t data = extractStaticDataFromNode(visitData(child));
        if (!data) {
            diags_->of(SemanticDiag::InvalidTypeArgument)
                .at(child->load()->tokenRange())
                .commit(base->toString(), "a 'const' argument must be a compile-time constant");
            throw BuildAbortException();
        }
        arguments.push_back(TypeArgument{.type = nullptr, .data = data});
    }
    if (base->code() == TypeCode::TypeFunc) {
        // A type declared with parameters: the arguments replace them in its body.
        const auto *typeFunc = static_cast<TypeFunctionType *>(base);
        type_vec_t types;
        for (const TypeArgument &argument : arguments) {
            if (argument.type == nullptr) {
                diags_->of(SemanticDiag::TypeFunctionArguments)
                    .at(ast->load()->tokenRange())
                    .commit(
                        typeFunc->toString(),
                        "its parameters are types, and a constant ('const ...') was given");
                throw BuildAbortException();
            }
            types.push_back(argument.type);
        }
        if (types.size() != typeFunc->params().size()) {
            diags_->of(SemanticDiag::TypeFunctionArguments)
                .at(ast->load()->tokenRange())
                .commit(
                    typeFunc->toString(),
                    std::format(
                        "it takes {} type argument(s) and {} were given",
                        typeFunc->params().size(),
                        types.size()));
            throw BuildAbortException();
        }
        LEAVE("SpecType");
        return typeFunc->apply(types);
    }
    if (arguments.empty()) {
        LEAVE("SpecType");
        return base;
    }
    if (const CollectionType *coll = asCollectionTypeOrPrototype(base);
        coll != nullptr && !arguments.empty() && arguments.front().type != nullptr) {
        // The key of a map, the element of a set (DESIGN-QUESTION(map-key-types)).
        Type *keyType = arguments.front().type;
        if (std::string problem = collectionKeyProblem(keyType); !problem.empty()) {
            diags_->of(SemanticDiag::CollectionKeyNotAllowed)
                .at(ast->load()->tokenRange())
                .commit(keyType->toString(), coll->toString(), problem);
            throw BuildAbortException();
        }
    }
    if (base->isOtherType() && !static_cast<OtherType *>(base)->typeArguments()) {
        // What the arguments of a module type mean is defined by the type, which sees
        // concrete types only. A type whose arguments are plain types (Map, Set) takes
        // variables like an array or a tuple does.
        for (const TypeArgument &argument : arguments) {
            if (argument.type != nullptr && containsTypeVar(argument.type)) {
                diags_->of(SemanticDiag::InvalidTypeArgument)
                    .at(ast->load()->tokenRange())
                    .commit(
                        base->toString(),
                        "a type variable cannot be an argument of a module type");
                throw BuildAbortException();
            }
        }
    }
    if (!base->isOtherType()) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(base->toString(), "this type takes no arguments");
        throw BuildAbortException();
    }
    std::string error;
    Type *res = static_cast<OtherType *>(base)->cloneWithArguments(arguments, error);
    if (res == nullptr) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(base->toString(), error.empty() ? std::string("no reason given") : error);
        throw BuildAbortException();
    }
    LEAVE("SpecType");
    return res;
}

/*
RefType(Ref ref) ;
*/
Type *Builder::visitRefType(const AST::node_ptr_t &ast) {
    ENTER("RefType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->load()->type() == AST::LoadType::Type, "Expected TypeLoad type for RefType");

    // Extract the reference type load from the AST node
    auto const &typeLoad = ast->loadAs<AST::RefTypeLoad>();
    const Reference &ref = typeLoad->ref();

    Type *type = lookupType(ref, ast);
    LEAVE("RefType");
    return type;
}

bool Builder::usableNamespace(const Reference &ref) const {
    std::string written = ref.toString();
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0;) {
        std::string candidate;
        for (size_t i = 0; i < depth; ++i) {
            candidate += namespacePath_[i] + "::";
        }
        if (ownNamespaces_.contains(candidate + written)) {
            return true;
        }
    }
    return module_->hasImportedPrefix(ref);
}

void Builder::rejectTypeCycle(
    const std::shared_ptr<camel::core::module::Module> &source, const Reference &remote,
    const Reference &ref, const AST::node_ptr_t &ast) {
    const auto key = std::make_pair(
        static_cast<const camel::core::module::Module *>(source.get()),
        remote.toString());
    if (context_->onReportedTypeCycle(key.first, key.second)) {
        throw BuildAbortException(); // reported where the cycle closed
    }
    const auto &inProgress = context_->typeDeclarationsInProgress();
    const auto start       = std::ranges::find(inProgress, key);
    if (start == inProgress.end()) {
        return; // not on a cycle: the name is reported as not resolved
    }
    // From the declaration that is needed again to the one that needs it, then that one again:
    // `a::A -> b::B -> a::A`.
    const auto shown = [](const std::pair<const camel::core::module::Module *, std::string> &d) {
        const std::string &module =
            d.first->declaredName().empty() ? d.first->name() : d.first->declaredName();
        return module + "::" + d.second;
    };
    std::string chain;
    for (auto it = start; it != inProgress.end(); ++it) {
        chain += shown(*it) + " -> ";
        context_->markTypeCycle(it->first, it->second);
    }
    chain += shown(key);
    diags_->of(SemanticDiag::ModuleTypeCycle)
        .at(ast->load()->tokenRange())
        .commit(ref.toString(), chain);
    throw BuildAbortException();
}

Type *Builder::lookupType(const Reference &ref, const AST::node_ptr_t &ast) {
    const std::string written = ref.toString();
    // The module's own name may qualify its names (design principles 10.2).
    std::string own = written;
    for (const std::string &self : {module_->name(), module_->declaredName()}) {
        if (!self.empty() && own.starts_with(self + "::")) {
            own = own.substr(self.size() + 2);
            break;
        }
    }
    // Layer 1 and 2: the types of the enclosing scopes and of the module, from the innermost
    // namespace out. The built-in type names (`int`, `Map`, ...) are at the top of the scopes.
    std::optional<Type *> found;
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && !found;) {
        std::vector<std::string> parts(namespacePath_.begin(), namespacePath_.begin() + depth);
        const Reference candidate(own);
        parts.insert(parts.end(), candidate.paths().begin(), candidate.paths().end());
        parts.push_back(candidate.ident());
        found = typeScope_->get(Reference(parts));
    }
    if (found) {
        // An imported type named `Map` or `Set` comes before the built-in of that name.
        if (isCollectionPrototype(found.value())) {
            if (const auto imported = module_->getImportedType(ref); imported.has_value()) {
                return imported.value();
            }
        }
        return found.value();
    }
    // Layer 3: imported by name, or through a module or namespace prefix.
    if (const auto imported = module_->getImportedType(ref, ImportLayer::Named)) {
        return imported.value();
    }
    if (const auto source = module_->importedSource(ref, ImportLayer::Named)) {
        // Imported, and not declared (yet): perhaps the types form a cycle across modules.
        rejectTypeCycle(source->first, source->second, ref, ast);
    }
    // Layer 4: through a `using namespace` in force; two that give different types are an
    // error where the name is used.
    std::vector<std::pair<std::string, Type *>> viaUsing;
    for (const auto &frame : usingFrames_) {
        for (const Reference &using_ : frame) {
            const std::string prefixed = using_.toString() + "::" + written;
            Type *type                 = nullptr;
            // The full name of the candidate, for AmbiguousName (design principles 10.2).
            std::string shown = prefixed;
            for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && type == nullptr;) {
                std::string candidate;
                for (size_t i = 0; i < depth; ++i) {
                    candidate += namespacePath_[i] + "::";
                }
                if (auto own = typeScope_->get(Reference(candidate + prefixed))) {
                    type  = own.value();
                    shown = module_->qualifiedName(Reference(candidate + prefixed));
                }
            }
            if (type == nullptr) {
                if (auto imported =
                        module_->getImportedType(Reference(prefixed), ImportLayer::Named)) {
                    type = imported.value();
                    if (const auto source =
                            module_->importedSource(Reference(prefixed), ImportLayer::Named)) {
                        shown = source->first->qualifiedName(source->second);
                    }
                }
            }
            if (type != nullptr && std::ranges::none_of(viaUsing, [&](const auto &have) {
                    return have.second == type;
                })) {
                viaUsing.emplace_back(shown, type);
            }
        }
    }
    if (viaUsing.size() == 1) {
        return viaUsing.front().second;
    }
    if (viaUsing.size() > 1) {
        std::string sources;
        for (const auto &[name, type] : viaUsing) {
            sources += (sources.empty() ? "" : ", ") + name;
        }
        diags_->of(SemanticDiag::AmbiguousName)
            .at(ast->load()->tokenRange())
            .commit(written, sources);
        throw BuildAbortException();
    }
    // Layer 5: what the built-in module and the modules of operators bring along.
    if (const auto ambient = module_->getImportedType(ref, ImportLayer::Ambient)) {
        return ambient.value();
    }
    if (wholeImportFailed_ || poisonedTypes_.contains(written)) {
        throw BuildAbortException();
    }
    if (ref.plain() && ref.ident() == "any") {
        // `any` is no longer a type: say so rather than that the name is unknown.
        reportAnyType(ast);
    }
    diags_->of(SemanticDiag::UnresolvedTypeReference).at(ast->load()->tokenRange()).commit(written);
    throw BuildAbortException();
}
} // namespace camel::compile::gct
