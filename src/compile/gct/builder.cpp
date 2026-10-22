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
 * Updated: Oct. 22, 2026
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
#include "camel/core/tags.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"
#include "camel/parse/ast/type.h"
#include "camel/utils/escape.h"
#include "camel/utils/number_text.h"
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
    typeScope_->insert(Reference("bool"), Type::Bool());
    typeScope_->insert(Reference("string"), Type::String());
    // The type of binary data (design principles 11.1), a built-in name like `string`.
    typeScope_->insert(Reference("Bytes"), camel::core::type::BytesType::get());
    typeScope_->insert(Reference("void"), Type::Void());
    // The built-in collection types, written `Map<K, V>` and `Set<T>` (design principles 11.3).
    typeScope_->insert(Reference("Map"), MapType::prototype());
    typeScope_->insert(Reference("Set"), SetType::prototype());
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
// Defined with the literal builder below; the subscript builder makes the constants a slice
// did not write (start 0, step 1) out of it.
template <typename T> inline data_ptr_t makeDataFromLiteral(const T &value);

// A primitive type whose values are integers (`i32`, `int`): a float literal is not converted
// to one by itself (design principles 5.1, "数值类型不做隐式转换").
static bool isIntegerPrimitive(Type *type) {
    return type != nullptr && type->isPrimitive() &&
           (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64);
}

// A literal that could not be converted to the type its position asks for: out of the range
// of a narrower numeric type (`let b: i32 = 3000000000`), a decimal literal in an integer type
// (`let x: int = 1.9`), or a value of another type. One diagnostic says which literal, which
// type, and why (handover H24).
//
// A text where a number is expected is not a range problem and not a text that is not a number
// either: there is no implicit text-to-number conversion (design principles 5.1), so it is a
// type mismatch, reported with the diagnostic the same position gives a variable of type
// `string` (DeclaredTypeMismatch in a declared position, DynamicCastForbidden for an explicit
// `as`).
//
// `written` is the element's text as it is in the source, when the caller can read it (design
// principles 5.1: the message quotes the source's spelling); empty falls back to the value.
// `explicitCast` says the literal stands under an `as` conversion rather than in a declared
// position.
template <typename Diags, typename Range>
static void reportLiteralCastFailure(
    Diags &diags, const Range &range, const data_ptr_t &data, Type *type,
    const std::string &written = {}, bool explicitCast = false) {
    if (data->type()->code() == TypeCode::String || type->code() == TypeCode::String) {
        // Either way round (`'1.5'` where a float is expected, `42` where a text is expected):
        // a value and a text are not converted implicitly (design principles 5.1).
        if (explicitCast) {
            diags->of(SemanticDiag::DynamicCastForbidden)
                .at(range)
                .commit(data->type()->toString(), type->toString());
        } else {
            diags->of(SemanticDiag::DeclaredTypeMismatch)
                .at(range)
                .commit(data->type()->toString(), type->toString());
        }
        return;
    }
    std::string reason;
    if (const std::string notFit = literalNotFitReason(data, type); !notFit.empty()) {
        reason = notFit;
    } else if (data->type()->isPrimitive() && isIntegerPrimitive(type)) {
        // A numeric type that is not an integer keeps its fraction (`1.9` -> `int`).
        reason = "the value would be truncated";
    } else {
        reason = "the value cannot become that type";
    }
    diags->of(SemanticDiag::LiteralNotRepresentable)
        .at(range)
        .commit(written.empty() ? literalValueText(data) : written, type->toString(), reason);
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
                if (node->nativeDeclaration()) {
                    // The declaration keeps what makes it one: no body, so the graph builder
                    // makes an operator rule of it instead of a graph (handover H19 step 1),
                    // and its tag uses go with it (`#op<'module:function'>`; design principles
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
    case AST::StmtType::Tag:
        // A tag declaration has no node: it is registered when the module is entered
        // (visitTagDecl, visitModule), so that a use written before it reads it.
        LEAVE("Stmt");
        return nullptr;
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
/*
tagDecl : 'export'? 'tag' identDef angledParams? 'on' tagPositions ('read_by' tagReaders)?
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
    // A program may not declare a tag with the name of a core permission or of `strict`: `#reassociate`
    // in the program would still mean the core's permission, so the declaration would never be read
    // (design principles 6.2, 7.4, log 2026-10-21).
    if (tag.ident == "finite_math" || tag.ident == "strict" ||
        camel::core::sem::parsePermission(tag.ident).has_value()) {
        diags_->of(SemanticDiag::TagShadowsPermission)
            .at(ast->load()->tokenRange())
            .commit(tag.ident);
    }
    tag.name = tag.module.empty() ? tag.ident : tag.module + "::" + tag.ident;
    tag.on     = static_cast<camel::core::tag::Positions>(decl->on());
    for (const auto &param : decl->params()) {
        tag.params.push_back({param.name, param.type});
    }
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
    // The tag uses written before the declaration are in a repeated child of their own; they
    // are kept beside the node (Tree::addTagUses), not in it.
    for (size_t i = 0; i + 1 < ast->size(); ++i) {
        const auto &child = ast->at(i);
        if (child->type() != AST::LoadType::Repeated ||
            child->loadAs<AST::RepeatedLoad>()->nodeName() != "FuncAnno") {
            continue;
        }
        for (const auto &annoAst : *child) {
            ASSERT(annoAst->type() == AST::LoadType::Data, "FuncAnno should lower to AST Data.");
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
    if (typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Enum) {
        // The name is in scope before the variants are read, so that a variant field can hold
        // the enum itself (`type Tree = enum { Node(Tree, Tree) }`).
        type = declareEnumType(typeNode, typeLoad.get());
    } else if (typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Struct) {
        // `type Name<params> = { field: T, ... }` declares a named struct type (design
        // principles 5.2). A name used where a value of a struct type is expected declares
        // nothing: `type Alias = Other` stays an alias for the type `Other` (structural).
        if (typeLoad->params().empty()) {
            // A type that mentions itself where a value of it would have to be held: a cycle
            // that can never be built (design principles 5.2). Recursion through an array is
            // allowed; through a map or a set it is legal by the rule but has no representation
            // yet, so it is reported rather than left unresolved.
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
                    diags_->of(SemanticDiag::FeatureNotSupported)
                        .at(ast->load()->tokenRange())
                        .commit(std::format(
                            "a type that mentions itself through a map, a set or an enum ('{}')",
                            name));
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
                // Legal by the rule, but the representation cannot hold it yet (see the
                // handover report H23): say so instead of leaving the name unresolved.
                diags_->of(SemanticDiag::FeatureNotSupported)
                    .at(ast->load()->tokenRange())
                    .commit(
                        std::format(
                            "a type that mentions itself through an array or another collection "
                            "('{}')",
                            name));
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
    EnumDecl *declaration                = EnumDecl::create(
        name, decl->params(), module_ ? module_->name() : std::string());
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
    StructDecl *declaration              = StructDecl::create(
        name, decl->params(), module_ ? module_->name() : std::string());
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
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedType->tokenRange()).commit(fieldName);
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
        result = function;
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

/// An EXEC of the statement block.
node_ptr_t Builder::makeExec(bool synced, bool operands) {
    return tree_->makeExec(synced, operands);
}

/*
StmtBlock(bool sync) : Stmt* stmts ;
*/
node_ptr_t Builder::visitDecoratedBlock(const AST::node_ptr_t &ast) {
    ENTER("DecoratedBlock");
    node_ptr_t execNode = visitStmtBlock(ast, false);
    FunctionTypeFactory factory; // no parameters; the result is the block's value
    FunctionType *type  = factory.buildOpen();
    node_ptr_t typeNode = tree_->makeType(type);
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
    // The block's children after the statement list are its decorators (`FuncAnno`) and the
    // tags written before it (`#tag { ... }`); only a decorator list makes it decorated.
    const bool decorated =
        decorators && ast->size() > 1 && ast->at(1)->type() == AST::LoadType::Repeated &&
        ast->at(1)->loadAs<AST::RepeatedLoad>()->nodeName() == "FuncAnno";
    if (decorated) {
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
    // Numeric permissions are permission tags, resolved by the graph builder where they are
    // written (design principles 7.2, 7.5): the statement block carries none of its own.
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
    // The tags written before the block (`#kernel { ... }`): they hold for the statements
    // written in it (design principles 6.2).
    attachTagUses(execNode, ast);
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
    case AST::DataType::TypeMatch:
        dataNode = visitMatchType(ast);
        break;
    case AST::DataType::StaticFor:
        dataNode = visitStaticFor(ast);
        break;
    case AST::DataType::StaticLoop:
        dataNode = visitStaticLoop(ast);
        break;
    case AST::DataType::StaticAccess:
        dataNode = visitStaticAccess(ast);
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
    case AST::DataType::SpreadArg:
        dataNode = visitSpreadArg(ast);
        break;
    case AST::DataType::StructComp:
        dataNode = visitStaticComprehension(ast, true);
        break;
    case AST::DataType::TupleComp:
        dataNode = visitStaticComprehension(ast, false);
        break;
    case AST::DataType::Func:
        dataNode = visitFuncData(ast);
        break;
    case AST::DataType::Ref:
        dataNode = visitRefData(ast);
        break;
    case AST::DataType::ConstAs:
        // Only the expression of a `const` type argument can hold one, and visitSpecType reads
        // it before visiting the expression as data.
        diags_->of(SemanticDiag::ConstasNotAllowed)
            .at(ast->load()->tokenRange())
            .commit(ast->loadAs<AST::ConstAsLoad>()->ref().toString());
        throw BuildAbortException();
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

    // A dimension of a subscript that slices is not an element index (design principles 5.4):
    // `t[a:b]` calls the slice operator, `t[i]` the index operator. The dimensions are counted
    // because a slice of a dimension other than the first is a view with strides, which needs
    // the multidimensional array representation of H17.
    AST::node_ptr_t sliceDimension = nullptr;
    size_t subscriptDimensions     = 0;
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        for (const auto &item : *ast->atAs<AST::RepeatedLoad>(1)) {
            ++subscriptDimensions;
            if (item->type() == AST::LoadType::Data &&
                item->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Slice) {
                sliceDimension = item;
            }
        }
    }

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
    case AST::BinaryDataOp::Index: {
        // `t[a:b]` is the slice operator, `t[i]` the index operator; the table gives `[]` the
        // index operator, so the slice is named here (design principles 5.4).
        opNode = tree_->makeDRef(sliceDimension != nullptr ? "__slice__" : "__idx__");
    } break;
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
        auto indices = ast->atAs<AST::RepeatedLoad>(1);
        if (sliceDimension != nullptr) {
            if (subscriptDimensions > 1) {
                // `t[:, 0]`, `t[a:b, c]`: the result takes every element of the dimension that
                // is not sliced at the coordinate the index gives, so it is a view with
                // strides (design principles 5.4). That needs the multidimensional array
                // representation H17 brings; until then the form is a located error rather
                // than a copy that would read like the view but not share its block.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .at(indices->load()->tokenRange())
                    .commit(
                        "a subscript that slices a dimension other than the first",
                        "it is a view with strides, which needs the multidimensional array "
                        "representation of H17; until then take the rows with the function "
                        "'take', or index one dimension at a time");
                throw BuildAbortException();
            }
            // `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]`: the three parts of the dimension
            // (an unwritten one a null load) and whether the start and the end were written at
            // all (`t[a:]` runs to the end; a negative step starts at the last element when the
            // start is left out). A part that was not written is passed as the value it stands
            // for, 0 for the start and 1 for the step; the end is read only when written. The
            // last argument is a two-bit mask (bit 0 end, bit 1 start), so a written start with
            // an unwritten end is told apart from the reverse.
            node_ptr_t bound[3] = {nullptr, nullptr, nullptr};
            bool written[3]     = {false, false, false};
            for (size_t i = 0; i < 3; ++i) {
                const auto &part = sliceDimension->at(i);
                written[i]       = part->type() != AST::LoadType::Null;
                if (written[i]) {
                    bound[i] = visitData(part);
                }
            }
            const auto intConstant = [this](int64_t value) {
                return tree_->makeData(makeDataFromLiteral<int64_t>(value));
            };
            bound[0]                  = written[0] ? bound[0] : intConstant(0);
            bound[1]                  = written[1] ? bound[1] : intConstant(0);
            bound[2]                  = written[2] ? bound[2] : intConstant(1);
            node_ptr_t endWrittenNode =
                intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0));
            for (size_t i = 0; i < 3; ++i) {
                *res << bound[i];
                if (written[i]) {
                    mergedInputs.push_back(nodeOrigin(bound[i]));
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::IndexExpr,
                        nodeOrigin(bound[i]),
                        static_cast<int32_t>(i),
                        i == 0 ? "start" : (i == 1 ? "end" : "step")));
                }
            }
            *res << endWrittenNode;
        } else {
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
        // `..(1, 2)` is the two arguments written out: a tuple literal is expanded here,
        // where its elements are still AST nodes. Any other `..e` keeps its marker for GIR,
        // which expands a `..ts` rest binding or a tuple comprehension.
        const auto spreadLiteral = [](const AST::node_ptr_t &arg) -> AST::node_ptr_t {
            if (arg->type() != AST::LoadType::Data ||
                arg->loadAs<AST::DataLoad>()->dataType() != AST::DataType::SpreadArg) {
                return nullptr;
            }
            const auto &operand = arg->atAs<AST::DataLoad>(0);
            if (operand->type() != AST::LoadType::Data ||
                operand->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Tuple) {
                return nullptr;
            }
            return operand;
        };
        size_t argCount = 0;
        for (const auto &argNode : *argsNode) {
            const AST::node_ptr_t literal = spreadLiteral(argNode);
            argCount += literal != nullptr ? literal->atAs<AST::RepeatedLoad>(0)->size() : 1;
        }
        res                   = tree_->makeLink(argCount);
        node_ptr_t calleeNode = visitData(lhsASTNode);
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
        const auto appendArgument = [&](node_ptr_t gctArg) {
            *res << gctArg;
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(gctArg),
                argSlot++,
                "arg"));
        };
        for (size_t ai = 0; ai < argsNode->size(); ++ai) {
            const auto &argNode = argsNode->at(ai);
            if (const AST::node_ptr_t literal = spreadLiteral(argNode); literal != nullptr) {
                if (ai + 1 != argsNode->size()) {
                    // `..e` stands for every argument it leaves: nothing can follow it.
                    const auto &operand = argNode->atAs<AST::DataLoad>(0);
                    diags_->of(SemanticDiag::RestArgumentNotLast)
                        .atOrigin(operand->load()->origin())
                        .commit(
                            operand->type() == AST::LoadType::Data &&
                                    operand->loadAs<AST::DataLoad>()->dataType() ==
                                        AST::DataType::Ref
                                ? operand->loadAs<AST::RefDataLoad>()->ref().toString()
                                : std::string("expression"));
                    throw BuildAbortException();
                }
                for (const auto &element : *literal->atAs<AST::RepeatedLoad>(0)) {
                    appendArgument(visitData(element));
                }
                continue;
            }
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
                *res << tree_->makeType(visitType(argument->at(0)));
                continue;
            }
            Type *type = visitType(argument);
            *res << tree_->makeType(type);
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
        // `cmp` is the compiler's own macro operator (oper::MacroCmp): it is referred to by the
        // id the table fixed at start-up, not by the name of the built-in module's entity.
        const oper_group_ptr_t cmpGroup =
            context_->importModule("")->nativeOperatorGroup(camel::core::oper::MacroCmp);
        ASSERT(cmpGroup != nullptr, "The built-in __cmp__ is missing.");
        node_ptr_t opNode  = tree_->makeDRef(camel::core::oper::MacroCmp, cmpGroup->name());
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
                reportLiteralCastFailure(
                    diags_, ast->load()->tokenRange(), data, dstType, {}, /*explicitCast=*/true);
                throw BuildAbortException();
            }
            auto convertedData = data->convertTo(dstType);
            if (convertedData) {
                res = tree_->makeData(convertedData);
            } else {
                reportLiteralCastFailure(
                    diags_, ast->load()->tokenRange(), data, dstType, {}, /*explicitCast=*/true);
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
condition and the body are scopes of their own, the way the arms of an `if` are; a loop body
written in a block or a function under a permission tag is part of it. The names of the state
and of the element are declared by the
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
matchTypeExpr
    : MATCH TYPE typeExpr '{' typeCase* '}'
    ;
*/
node_ptr_t Builder::visitMatchType(const AST::node_ptr_t &ast) {
    ENTER("MatchType");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchType");
    node_ptr_t matchNode = tree_->makeTypeMatch();
    setOriginFromAst(context_, matchNode, ast, camel::source::OriginKind::GctNode, "gct.match.type");

    const auto &typeAst    = ast->atAs<AST::TypeLoad>(0);
    Type *subject          = visitType(typeAst);
    node_ptr_t typeNode    = tree_->makeType(subject);
    setOriginFromAst(
        context_, typeNode, typeAst, camel::source::OriginKind::GctNode, "gct.match.type.subject");
    *matchNode << typeNode;

    for (const auto &caseAst : *ast->atAs<AST::RepeatedLoad>(1)) {
        const auto &caseLoad = caseAst->loadAs<AST::TypeCaseLoad>();
        uint16_t patterns    = 0;
        for (const AST::TypeCaseKind kind : caseLoad->patterns()) {
            patterns |= typeCaseBit(static_cast<GCT::TypeCaseKind>(kind));
        }
        node_ptr_t caseNode = tree_->makeTypeCase(patterns);
        setOriginFromAst(
            context_, caseNode, caseAst, camel::source::OriginKind::GctNode, "gct.match.type.case");
        node_ptr_t bodyNode = visitStmtBlock(caseAst->atAs<AST::StmtBlockLoad>(0));
        *caseNode << bodyNode;
        *matchNode << caseNode;
    }
    LEAVE("MatchType");
    return matchNode;
}

/*
staticForExpr
    : STATIC staticForHeader '=>' blockExpr
    ;
staticForHeader
    : FOR '(' identDef (',' identDef)? ')' IN ('fields' typeExpr | 'indices' dataExpr)
    ;
The walked shape is child 0: a TYPE for `fields`, the operand value for `indices` (H51 step 6).
*/
node_ptr_t Builder::visitStaticFor(const AST::node_ptr_t &ast) {
    ENTER("StaticFor");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticFor");
    const auto &load = ast->loadAs<AST::StaticForLoad>();

    node_ptr_t forNode =
        tree_->makeStaticFor(load->valueName(), load->typeName(), load->byIndices());
    setOriginFromAst(
        context_, forNode, ast, camel::source::OriginKind::GctNode, "gct.static.for");

    if (load->byIndices()) {
        // `indices ts` walks a tuple value: the operand is an ordinary expression whose type is
        // read when the expansion happens, so it is built as one here.
        *forNode << visitData(ast->at(0));
    } else {
        const auto &typeAst = ast->atAs<AST::TypeLoad>(0);
        Type *subject       = visitType(typeAst);
        node_ptr_t typeNode = tree_->makeType(subject);
        setOriginFromAst(
            context_, typeNode, typeAst, camel::source::OriginKind::GctNode,
            "gct.static.for.type");
        *forNode << typeNode;
    }

    // `F` names the type of one field in the body only. It is an ordinary type variable of the
    // enclosing function's instance: the graph builder binds it to the field type per round.
    pushScope();
    if (!load->byIndices()) {
        typeScope_->insert(
            Reference(load->typeName()), TypeVarType::create(load->typeName(), false));
    }
    node_ptr_t bodyNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    popScope();
    *forNode << bodyNode;

    LEAVE("StaticFor");
    return forNode;
}

/*
staticLoopExpr
    : STATIC LOOP loopState (',' loopState)* staticForHeader DO blockExpr
The AST node holds the first values of the state (a Repeated Data), the walked shape, and the
body (StaticLoopLoad). The GCT node holds the shape first, then the first values, then the EXEC
body. `F` is an ordinary type variable of the enclosing function's instance, bound per field
by the graph builder; `n` (or `j` for `indices`) and the state names are bound per round.
*/
node_ptr_t Builder::visitStaticLoop(const AST::node_ptr_t &ast) {
    ENTER("StaticLoop");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticLoop");
    const auto &load = ast->loadAs<AST::StaticLoopLoad>();

    std::vector<std::string> state;
    state.reserve(load->state().size());
    for (const Reference &name : load->state()) {
        state.push_back(name.toString());
    }
    node_ptr_t loopNode =
        tree_->makeStaticLoop(state, load->valueName(), load->typeName(), load->byIndices());
    setOriginFromAst(context_, loopNode, ast, camel::source::OriginKind::GctNode, "gct.static.loop");

    const ValueUseScope valueUseScope(*this);
    valueUse_ = {};

    if (load->byIndices()) {
        *loopNode << visitData(ast->at(1));
    } else {
        const auto &typeAst = ast->atAs<AST::TypeLoad>(1);
        Type *subject       = visitType(typeAst);
        node_ptr_t typeNode = tree_->makeType(subject);
        setOriginFromAst(
            context_, typeNode, typeAst, camel::source::OriginKind::GctNode,
            "gct.static.loop.type");
        *loopNode << typeNode;
    }

    // The first values, evaluated once where the loop is, before the names of the head exist.
    for (const auto &init : *ast->atAs<AST::RepeatedLoad>(0)) {
        *loopNode << visitData(init);
    }

    // `F` names the type of one field in the body only (as in `static for`).
    pushScope();
    if (!load->byIndices()) {
        typeScope_->insert(
            Reference(load->typeName()), TypeVarType::create(load->typeName(), false));
    }
    node_ptr_t bodyNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(2));
    popScope();
    *loopNode << bodyNode;

    registerGctSemantic(context_, loopNode, ast);
    LEAVE("StaticLoop");
    return loopNode;
}

node_ptr_t Builder::visitStaticAccess(const AST::node_ptr_t &ast) {
    ENTER("StaticAccess");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticAccess");
    node_ptr_t accessNode = tree_->makeStaticAccess();
    setOriginFromAst(
        context_, accessNode, ast, camel::source::OriginKind::GctNode, "gct.static.access");
    node_ptr_t targetNode = visitData(ast->at(0));
    node_ptr_t keyNode    = visitData(ast->at(1));
    *accessNode << targetNode << keyNode;
    LEAVE("StaticAccess");
    return accessNode;
}

/*
structData : '{' (pairedValues ','?)? '}' | '{' staticForExpr '}'
tupleData  : '(' dataList? ','? ')' | '(' staticForExpr ')'
The comprehension alternatives are their own AST nodes (StructComp / TupleComp): a generator and
nothing else. The GCT node holds the shape and the generator; the rounds are packed in GIR.
*/
node_ptr_t Builder::visitStaticComprehension(const AST::node_ptr_t &ast, bool isStruct) {
    ENTER("StaticComprehension");
    node_ptr_t compNode = tree_->makeComprehension(isStruct);
    setOriginFromAst(
        context_, compNode, ast, camel::source::OriginKind::GctNode, "gct.static.comprehension");
    ASSERT(ast->size() == 1, "A static comprehension has its generator as its only child.");
    *compNode << visitStaticFor(ast->at(0));
    LEAVE("StaticComprehension");
    return compNode;
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
    // The value the literal was written as, exactly (design principles 5.1). The type it
    // takes is decided by its context; this is what the context converts or folds.
    std::optional<literal::Value> exactValue;

    // Handle different literal types
    switch (value.type()) {
    case LiteralType::String: {
        // Decode escaped characters in the string and create a StringData object. A formatted
        // string's pattern is decoded with `doubleBraces`: a brace that an escape produces is a
        // literal brace of that pattern, and `format` must see it doubled (H89).
        //
        // Every escape form is decoded here, so an unknown escape is reported here too, once per
        // spelling, in the lexical catalogue: an octal escape is simply unknown (H89), and so is
        // `\q`. The text keeps the pair as written; a warning does not stop the program.
        std::vector<std::string> unknownEscapes;
        std::string decoded = decodeEscapes(str, literal->fromFormattedString(), &unknownEscapes);
        for (const std::string &escape : unknownEscapes) {
            diags_->of(LexicalDiag::InvalidEscapeSequence)
                .at(ast->load()->tokenRange())
                .commit(escape);
        }
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
        // Unreachable: the AST builder lowers every formatted string to the same
        // `'...'->format<...>` tree (design principles 11.5; AST Builder::visitFStringData),
        // and a pattern refuses one instead of producing a formatted-string literal. There is
        // therefore no source text that can put a FString literal in the GCT.
        ASSERT(
            false,
            "A formatted-string literal reached the GCT: the AST builder no longer lowers "
            "every formatted string to a format call.");
        // A debug build aborts above; a release build reports the compiler defect instead of
        // blaming the program for a feature it never wrote.
        diags_->of(InternalDiag::UnknownInternalError)
            .at(ast->load()->tokenRange())
            .commit(
                "a formatted-string literal reached the GCT; the front end should have lowered "
                "it to a format call");
        throw BuildAbortException();
    } break;
    case LiteralType::Integer: {
        // Exact arithmetic. A literal that an int cannot hold used to wrap around
        // (`9223372036854775808` was the smallest int, as was `1e400`). The value is read by
        // the same whole-number-token rule an entry argument is (camel/utils/number_text.h):
        // one grammar, one parser for the two places a number is written as text.
        int64_t value = 0;
        switch (camel::utils::parseNumberToken<int64_t>(str, value)) {
        case camel::utils::NumberTextStatus::Ok:
            break;
        case camel::utils::NumberTextStatus::OutOfRange:
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(
                    str, "int", "out of range (-9223372036854775808 to 9223372036854775807)");
            throw BuildAbortException();
        default:
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("integer", str);
            throw BuildAbortException();
        }
        data       = makeDataFromLiteral(value);
        exactValue = literal::Value::fromIntegerText(str);
    } break;
    case LiteralType::Real: {
        // The double nearest to the number written, read by the same whole-number-token rule an
        // entry argument is (camel/utils/number_text.h). One too large for a double was an
        // exception that reached the user as bare text.
        double value = 0.0;
        switch (camel::utils::parseNumberToken<double>(str, value)) {
        case camel::utils::NumberTextStatus::Ok:
            break;
        case camel::utils::NumberTextStatus::Underflow:
            // A nonzero number too small to tell from zero is a value the type cannot hold, not
            // zero (H15i; design principles 5.1): the literal is refused with that reason.
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(str, "float", "too small to be represented (the nearest value is zero)");
            throw BuildAbortException();
        case camel::utils::NumberTextStatus::OutOfRange:
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(str, "float", "out of range (at most about 1.8e308 in magnitude)");
            throw BuildAbortException();
        default:
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("real", str);
            throw BuildAbortException();
        }
        data       = makeDataFromLiteral(value);
        exactValue = literal::Value::fromRealText(str);
    } break;
    case LiteralType::Imaginary: {
        // `4i`: the value is imaginary, the type is the default c128 until the context says
        // otherwise (design principles 5.1).
        exactValue = literal::Value::fromImaginaryText(str);
        if (!exactValue.has_value()) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("imaginary number", str);
            throw BuildAbortException();
        }
        data = *exactValue->toData(Type::Complex128());
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
    if (exactValue.has_value()) {
        tree_->setLiteralValue(res, std::move(*exactValue));
    }
    if (value.type() == LiteralType::String && literal->fromFormattedString()) {
        // The pattern of a formatted string: a format specification that does not fit its
        // argument is reported at the interpolation (the GIR builder knows the types).
        res->setFromFormattedString();
    }
    LEAVE("Literal");
    return res;
}

/*
ArrayData() : Data* data ;
*/
/// The kind of the number literal written directly as an element of an array literal; nothing
/// for any other element (a name, a call, a nested literal, a string).
static std::optional<literal::Kind> writtenNumberKind(const AST::node_ptr_t &item) {
    if (item == nullptr || item->type() != AST::LoadType::Data) {
        return std::nullopt;
    }
    if (item->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Literal) {
        return std::nullopt;
    }
    switch (item->loadAs<AST::LiteralLoad>()->value().type()) {
    case LiteralType::Integer:
        return literal::Kind::Integer;
    case LiteralType::Real:
        return literal::Kind::Float;
    case LiteralType::Imaginary:
        return literal::Kind::Imaginary;
    default:
        return std::nullopt;
    }
}

/// An element of a composite literal as it was written: the value it was written as, when it
/// is a numeric literal (design principles 5.1), and the position it was written at, which is
/// where the element is reported when the type of its position cannot hold it. A nested
/// composite, a name and a computed element have no value of their own, but they have a
/// position, and so do the elements inside them.
static Tree::LiteralElement writtenElement(const node_ptr_t &node) {
    Tree::LiteralElement element;
    if (node == nullptr) {
        return element;
    }
    element.origin = node->origin();
    if (node->type() == NodeKind::DATA) {
        if (const literal::Value *exact = Tree::of(node)->literalValueOf(node)) {
            element.value = *exact;
        }
    }
    return element;
}

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

    // With no context to say which type they have, the number literals of one array take the
    // widest kind among them (design principles 5.1; as in Go): `[1, 2.0]` is a `float[]`.
    // The order of the kinds is integer < decimal < imaginary: a complex element widens every
    // other number to c128, the default complex type.
    bool hasInteger = false, hasReal = false, hasImaginary = false;
    for (const auto &item : *items) {
        if (const std::optional<literal::Kind> kind = writtenNumberKind(item);
            kind.has_value()) {
            hasInteger |= *kind == literal::Kind::Integer;
            hasReal |= *kind == literal::Kind::Float;
            hasImaginary |= *kind == literal::Kind::Imaginary;
        }
    }
    const bool widenToFloat = hasInteger && hasReal && !hasImaginary;
    // The values the elements were written as and their positions, in the order the data holds
    // them: the elements keep their exact values and their places until GIR decides the type
    // of the array (design principles 5.1).
    std::vector<Tree::LiteralElement> literalElements;
    literalElements.reserve(items->size());

    for (const auto &item : *items) {
        node_ptr_t dataNode = visitData(item);
        if (hasImaginary && writtenNumberKind(item) != literal::Kind::Imaginary) {
            // An integer or decimal literal beside an imaginary one is a complex value.
            dataNode = literalNodeInType(dataNode, Type::Complex128(), item);
        } else if (widenToFloat && writtenNumberKind(item) == literal::Kind::Integer) {
            // An integer literal beside a decimal one is a value of the float type, exactly.
            dataNode = literalNodeInType(dataNode, Type::Float(), item);
        }
        literalElements.push_back(writtenElement(dataNode));
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
    if (!dangling) {
        // Every element is a constant: the array is one literal, and the values its elements
        // were written as are kept for the type it may take later (GIR).
        tree_->setLiteralElements(arrayData, std::move(literalElements));
    }
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
        node_ptr_t typeNode = place(tree_->makeType(type), body, "gct.func.type");
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

namespace {

/// The source text the origin of a literal covers (`1.0e39`), when it is short and on one
/// line; empty otherwise. A diagnostic that quotes a literal quotes what was written.
std::string sourceTextAt(
    const camel::source::source_context_ptr_t &context, camel::source::origin_id_t origin) {
    const auto *sourceContext = context.get();
    const auto *record = sourceContext != nullptr && origin != camel::source::kInvalidOriginId
                             ? sourceContext->origin(origin)
                             : nullptr;
    const auto *span = record != nullptr ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file = span != nullptr && span->valid()
                           ? sourceContext->sourceFile(span->fileId)
                           : nullptr;
    if (file == nullptr || span->endOffset < span->startOffset ||
        span->endOffset > file->content.size()) {
        return {};
    }
    std::string text =
        file->content.substr(span->startOffset, span->endOffset - span->startOffset);
    if (text.size() > 60 || text.find('\n') != std::string::npos) {
        return {};
    }
    return text;
}

} // namespace

bool Builder::rejectTextInLiteral(const data_ptr_t &data, Type *type, camel::source::origin_id_t where) {
    if (data == nullptr || type == nullptr || type->assignableFrom(data->type())) {
        return false;
    }
    // A text where the position expects another type is a type mismatch, not a literal that
    // does not fit: there is no implicit text-to-number (or text-to-anything) conversion
    // (design principles 5.1). The element's own position is where it is written, as H27c
    // reports an element whose value does not fit (see the GIR builder's literalElementInType).
    if (data->type()->code() == TypeCode::String) {
        diags_->of(SemanticDiag::DeclaredTypeMismatch)
            .atOrigin(where)
            .commit("string", type->toString());
        return true;
    }
    if (!isComposite(data->type()->code())) {
        return false;
    }
    const auto *written = tree_->literalElementsOf(data);
    const auto elementAt = [&](size_t index, camel::source::origin_id_t fallback) {
        const Tree::LiteralElement *element =
            written != nullptr && index < written->size() ? &(*written)[index] : nullptr;
        return element != nullptr && element->origin != camel::source::kInvalidOriginId
                   ? element->origin
                   : fallback;
    };
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        auto *structType = tt::as_ptr<StructType>(type);
        const auto object = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return false;
        }
        size_t index = 0;
        for (const auto &[name, fieldValue] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return false;
            }
            const camel::source::origin_id_t fieldWhere = elementAt(index, where);
            ++index;
            if (rejectTextInLiteral(fieldValue, structType->typeAt(*fieldIndex), fieldWhere)) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Tuple && data->type()->code() == TypeCode::Tuple) {
        auto *tupleType    = tt::as_ptr<TupleType>(type);
        const auto object  = std::dynamic_pointer_cast<TupleData>(data);
        if (tupleType == nullptr || object == nullptr ||
            object->raw().size() != tupleType->size()) {
            return false;
        }
        for (size_t index = 0; index < object->raw().size(); ++index) {
            if (rejectTextInLiteral(
                    object->raw()[index], tupleType->typeAt(index), elementAt(index, where))) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        auto *arrayType      = tt::as_ptr<ArrayType>(type);
        const auto object    = std::dynamic_pointer_cast<ArrayData>(data);
        Type *elementType    = arrayType != nullptr ? arrayType->elemType() : nullptr;
        if (elementType == nullptr || object == nullptr) {
            return false;
        }
        for (size_t index = 0; index < object->raw().size(); ++index) {
            if (rejectTextInLiteral(
                    object->raw()[index], elementType, elementAt(index, where))) {
                return true;
            }
        }
    }
    return false;
}

node_ptr_t Builder::declaredValue(const node_ptr_t &value, Type *type, const AST::node_ptr_t &ast) {
    // A literal of the declared type, converted now (`let x: float = 1`). A declared `T?` /
    // `T!` and a type written with a type variable (decided when the generic function is
    // instantiated) are left to the CAST, and so is a function type, which a literal cannot
    // have.
    if (value->type() == NodeKind::DATA && asTagged(type) == nullptr && !containsTypeVar(type)) {
        // The value the literal was written as, exactly (design principles 5.1): `let x: f32 =
        // 0.1` is the f32 nearest to one tenth, not the same double rounded twice, and `let x:
        // float = 1` is 1.0 (an integer literal takes the float type its context asks for).
        if (const literal::Value *exact = Tree::of(value)->literalValueOf(value);
            exact != nullptr &&
            (type->isPrimitive() || camel::core::type::isComplex(type->code())) &&
            (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64 ||
             type->code() == TypeCode::Float32 || type->code() == TypeCode::Float64 ||
             camel::core::type::isComplex(type->code()))) {
            if (auto converted = exact->toData(type); converted.has_value()) {
                return tree_->makeData(*converted);
            }
            if (exact->hasImaginaryPart() && !camel::core::type::isComplex(type->code())) {
                // A complex literal where a real type is declared: no implicit conversion
                // (design principles 5.1). `re(z)` and `im(z)` are the explicit operations.
                const camel::source::source_context_ptr_t sourceContext =
                    context_ != nullptr ? context_->sourceContext() : nullptr;
                std::string written = sourceTextAt(sourceContext, value->origin());
                if (written.empty()) {
                    written = exact->toString();
                }
                diags_->of(SemanticDiag::LiteralNotRepresentable)
                    .at(ast->load()->tokenRange())
                    .commit(
                        written, type->toString(),
                        "a complex value does not become a real one; use re() or im()");
                throw BuildAbortException();
            }
            if (exact->kind() == literal::Kind::Float && isIntegerPrimitive(type)) {
                // A float literal where an integer is asked for: not a range problem, but a
                // conversion the language does not do (`1.0 as int` is an error too).
                reportLiteralCastFailure(diags_, ast->load()->tokenRange(), value->data(), type);
                throw BuildAbortException();
            }
            const camel::source::source_context_ptr_t sourceContext =
                context_ != nullptr ? context_->sourceContext() : nullptr;
            std::string written = sourceTextAt(sourceContext, value->origin());
            if (written.empty()) {
                written = exact->kind() == literal::Kind::Float ? exact->toFloatText()
                                                               : exact->toString();
            }
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(
                    written,
                    type->toString(),
                    exact->underflows(type)
                        ? "too small to be represented (the nearest value is zero)"
                        : "out of range (" + literal::rangeText(type) + ")");
            throw BuildAbortException();
        }
        // An unresolved literal (`[n, m]`, `{ a: x, b: 2.0 }`: some elements are values) takes
        // the declared type through the annotation CAST below, where its elements are made the
        // element (field) type — they are positions with an expected type too (design
        // principles 5.1). Converting its data now would keep the type its values give it.
        if (!isUnresolvedLiteral(value->data())) {
            // A text field of a composite literal is reported at the field itself, as a type
            // mismatch; a lone text value keeps the declaration's position, as H15g reports it
            // (`let x: int = '42'` is a mismatch at the declaration), and anything else the
            // conversion cannot take is reported for the literal as a whole below.
            if (isComposite(value->data()->type()->code()) &&
                rejectTextInLiteral(value->data(), type, value->origin())) {
                throw BuildAbortException();
            }
            if (data_ptr_t converted = value->data()->convertTo(type)) {
                return tree_->makeData(converted);
            }
            reportLiteralCastFailure(diags_, ast->load()->tokenRange(), value->data(), type);
            throw BuildAbortException();
        }
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
    if (type != nullptr && elemType == nullptr && !containsTypeVar(type) &&
        asTagged(type) == nullptr && ast->type() == AST::LoadType::Data) {
        // Design principles 5.1: the elements of an array literal and the fields of a struct
        // literal are positions with an expected type, like a declaration's. They are built
        // with the type the position gives them (`let xs: float[] = [1, 2.0]`, `let p: P = {
        // a: 1, b: 2.0 }`), so a literal element is converted now, exactly, and an `int`
        // array is never stored where a `float[]` was declared.
        const auto &load = ast->loadAs<AST::DataLoad>();
        if (load->dataType() == AST::DataType::Array) {
            const auto &items = *ast->atAs<AST::RepeatedLoad>(0);
            if (type->code() == TypeCode::Array) {
                if (items.empty()) {
                    // `[]` in a position of type `T[]` is the empty array of `T` (design
                    // principles 5.1, 5.2): the elements it does not have are the only thing
                    // its type leaves open, so it takes the type of the position
                    // (`[] as int[]`, a recursive type's array field, a loop state).
                    entered = true;
                    return tree_->makeData(
                        tt::as_shared<Data>(
                            ArrayData::create(tt::as_ptr<ArrayType>(type)->elemType())));
                }
                return visitArrayInto(ast, type, entered);
            }
        } else if (load->dataType() == AST::DataType::Struct && type->code() == TypeCode::Struct) {
            return visitStructInto(ast, type, entered);
        }
    }
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

node_ptr_t Builder::literalNodeInType(
    const node_ptr_t &node, Type *type, const AST::node_ptr_t &at) {
    if (node == nullptr || type == nullptr || node->type() != NodeKind::DATA) {
        return node;
    }
    const data_ptr_t &data = node->data();
    if (type->equals(data->type())) {
        return node;
    }
    // The value the literal was written as, exactly (design principles 5.1): an integer
    // literal becomes the float type its position asks for without being rounded twice.
    const literal::Value *exact = Tree::of(node)->literalValueOf(node);
    if (exact == nullptr || !type->isPrimitive()) {
        return node;
    }
    // The element is quoted as it is written (`1e40`, not the canonical `1e+40`), as a literal
    // in any other position with an expected type is (design principles 5.1).
    const std::string written = sourceTextAt(
        context_ != nullptr ? context_->sourceContext() : nullptr,
        at->load()->origin());
    if (!literal::kindEnters(*exact, type)) {
        // A decimal literal where an integer is asked for: the fraction would be lost.
        reportLiteralCastFailure(diags_, at->load()->tokenRange(), data, type, written);
        throw BuildAbortException();
    }
    if (auto converted = exact->toData(type); converted.has_value()) {
        node_ptr_t res = tree_->makeData(*converted);
        // The converted literal is the same value written: later positions round it again from
        // the exact value, never from the double it became here (design principles 5.1).
        tree_->setLiteralValue(res, *exact);
        setOriginFromAst(context_, res, at, camel::source::OriginKind::GctNode, "gct.data");
        return res;
    }
    reportLiteralCastFailure(diags_, at->load()->tokenRange(), data, type, written);
    throw BuildAbortException();
}

node_ptr_t Builder::visitArrayInto(const AST::node_ptr_t &ast, Type *arrayType, bool &entered) {
    ENTER("ArrayData");
    entered        = false;
    Type *elemType = tt::as_ptr<ArrayType>(arrayType)->elemType();
    if (elemType == nullptr || containsTypeVar(elemType)) {
        LEAVE("ArrayData");
        return visitData(ast);
    }
    ArrayDataFactory factory;
    // The position names the element type: the literal is that array even where an element is
    // a computed value (`[[a, b]]` for an `int[][]`), so the type is not left to the elements
    // to fix (and a nested literal is not read as the type of an outer position, H24).
    factory.setElemType(elemType);
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    // Each element's position, as the ordinary build keeps it (H27c/H15h).
    std::vector<Tree::LiteralElement> literalElements;
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(item, elemType, nested);
        if (!nested) {
            dataNode = literalNodeInType(dataNode, elemType, item);
        }
        literalElements.push_back(writtenElement(dataNode));
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
    const data_ptr_t built = factory.build();
    if (!dangling) {
        tree_->setLiteralElements(built, std::move(literalElements));
    }
    node_ptr_t res         = tree_->makeData(built);
    // The literal is the value of the type asked for when the two types are the same. When
    // they are not (a named struct type, say), the caller still applies the declared value or
    // the annotation CAST, which attaches that type as it does for any value.
    entered = arrayType->equals(built->type());
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("ArrayData");
    return res;
}

node_ptr_t Builder::visitStructInto(const AST::node_ptr_t &ast, Type *structType, bool &entered) {
    ENTER("StructData");
    entered            = false;
    auto *sType        = tt::as_ptr<StructType>(structType);
    const auto &fields = *ast->atAs<AST::RepeatedLoad>(0);
    // A literal that does not write exactly the fields of the type is built as usual and
    // reported by the conversion that finds it of another type.
    if (fields.size() != sType->size()) {
        LEAVE("StructData");
        return visitData(ast);
    }
    for (const auto &child : fields) {
        const auto &named = child->loadAs<AST::NamedDataLoad>();
        if (!sType->findField(named->ref().ident()).has_value()) {
            LEAVE("StructData");
            return visitData(ast);
        }
    }
    StructDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    // The fields are built in the type's order (the declaration's order for a named type),
    // not the order they are written in: a literal at a position of a struct type is a value
    // of that type (design principles 5.2), so `{ y: 1, x: 2 }` at `Point` is a `Point` whose
    // fields are `x, y`. Each field keeps the position it was written at, for diagnostics
    // (H27c/H15h): a field the field type cannot take is reported at the field, not at the
    // literal.
    std::unordered_map<std::string, Tree::LiteralElement> literalElementsByName;
    for (size_t i = 0; i < sType->size(); ++i) {
        const std::string name(sType->fieldName(i));
        const AST::node_ptr_t *written = nullptr;
        for (const auto &child : fields) {
            if (child->loadAs<AST::NamedDataLoad>()->ref().ident() == name) {
                written = &child;
                break;
            }
        }
        ASSERT(written != nullptr, "A struct literal field was checked to exist by name.");
        Type *fieldType      = sType->typeAt(i);
        const auto &valueAst = (*written)->atAs<AST::DataLoad>(0);
        bool nested          = false;
        node_ptr_t fieldNode = visitDataInto(valueAst, fieldType, nested);
        if (asTagged(fieldType) != nullptr) {
            // A field of `T?` / `T!`: the value enters the form, as it does at any position
            // whose type is written (design principles 3.8).
            node_ptr_t castNode = tree_->makeCast(fieldType, true);
            *castNode << fieldNode;
            setOriginFromAst(
                context_,
                castNode,
                valueAst,
                camel::source::OriginKind::GctNode,
                "gct.cast");
            fieldNode = castNode;
        } else if (!nested) {
            fieldNode = literalNodeInType(fieldNode, fieldType, valueAst);
        }
        literalElementsByName.emplace(name, writtenElement(fieldNode));
        auto [data, fieldDangling] = extractData(fieldNode, execNode, dangling);
        if (fieldDangling) {
            dangling = true;
        }
        if (!fieldDangling &&
            (data == nullptr || data->type() == nullptr ||
             !fieldType->assignableFrom(data->type()))) {
            // A field written as a variable keeps its own type (design principles 5.1): the
            // literal is of an unnamed struct type whose field is of another type, and the
            // conversion at the position it is written in reports it, as it would without
            // building the literal in the position's type.
            LEAVE("StructData");
            return visitData(ast);
        }
        factory.add(name, data);
    }
    const data_ptr_t built = factory.buildAs(sType);
    if (built == nullptr) {
        // A field is not there or is another type: the conversion that finds the literal of
        // another type reports it.
        LEAVE("StructData");
        return visitData(ast);
    }
    if (!dangling) {
        // A StructData holds its fields in the type's order, so the elements are recorded in
        // that order whatever order they were written in; a diagnostic then points at the
        // element that failed, not at the literal.
        const auto object = std::dynamic_pointer_cast<StructData>(built);
        std::vector<Tree::LiteralElement> literalElements;
        if (object != nullptr) {
            literalElements.reserve(object->raw().size());
            for (const auto &[name, _] : object->raw()) {
                literalElements.push_back(literalElementsByName.at(name));
            }
        }
        tree_->setLiteralElements(built, std::move(literalElements));
    }
    node_ptr_t res = tree_->makeData(built);
    // The literal is a value of the type the position gives it: its fields are that type's
    // fields, in that type's order.
    entered = true;
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("StructData");
    return res;
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
    // The value each field was written as and its position, keyed by name: a StructData holds its
    // fields in name order, and the conversion walks the elements by index (design principles
    // 5.1; see visitArrayData).
    std::unordered_map<std::string, Tree::LiteralElement> literalElementsByName;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name    = namedPair->ref().ident();
        if (!fieldNames.insert(name).second) {
            // As in a struct type. The later value used to be dropped, silently.
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedPair->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        node_ptr_t dataNode = visitData(child->atAs<AST::DataLoad>(0));
        literalElementsByName.emplace(name, writtenElement(dataNode));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        factory.add(name, data);
    }

    auto structData = factory.build();
    if (!dangling) {
        const auto object = std::dynamic_pointer_cast<StructData>(structData);
        std::vector<Tree::LiteralElement> literalElements;
        if (object != nullptr) {
            literalElements.reserve(object->raw().size());
            for (const auto &[name, _] : object->raw()) {
                literalElements.push_back(literalElementsByName.at(name));
            }
        }
        tree_->setLiteralElements(structData, std::move(literalElements));
    }
    node_ptr_t res = tree_->makeData(structData);

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
    // Each element's position and exact value, as for an array or struct literal (H27c/H15h):
    // a tuple is positional, and an element the element type cannot take is reported at the
    // element.
    std::vector<Tree::LiteralElement> literalElements;

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        literalElements.push_back(writtenElement(dataNode));
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        factory.add(data);
    }

    auto tupleData = factory.build();
    if (!dangling) {
        tree_->setLiteralElements(tupleData, std::move(literalElements));
    }
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
SpreadArg() : Data data ;
A rest argument (design principles 5.5): the operand is a compile-time tuple; the arguments it
stands for are decided when the call is instantiated.
*/
node_ptr_t Builder::visitSpreadArg(const AST::node_ptr_t &ast) {
    ENTER("SpreadArg");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for SpreadArg");
    node_ptr_t operand    = visitData(ast->atAs<AST::DataLoad>(0));
    node_ptr_t spreadNode = tree_->makeSpreadArg();
    *spreadNode << operand;
    setOriginFromAst(context_, spreadNode, ast, camel::source::OriginKind::GctNode, "gct.spread");
    LEAVE("SpreadArg");
    return spreadNode;
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
        bool own;
        scope_ptr_t<Reference, Type *> outer;
        std::vector<std::string> outerVars;
        std::set<std::string> outerAuto;
        std::set<std::string> outerOutput;
        std::unordered_map<std::string, size_t> outerUses;
        ~TypeScopeGuard() {
            if (own) {
                // A nested signature may use the variables of this one: those uses count
                // here (a lambda body is part of the declaration it stands in).
                for (const auto &[name, count] : self.typeVarUses_) {
                    if (!self.autoTypeVars_.contains(name)) {
                        outerUses[name] += count;
                    }
                }
                self.autoTypeVars_ = std::move(outerAuto);
                self.typeVarUses_  = std::move(outerUses);
                self.outputTypeVars_ = std::move(outerOutput);
            }
            self.typeScope_        = outer;
            self.declaredTypeVars_ = std::move(outerVars);
        }
    } typeScopeGuard{
        *this,
        isDecl,
        typeScope_,
        std::exchange(declaredTypeVars_, {}),
        isDecl ? std::move(autoTypeVars_) : std::set<std::string>{},
        isDecl ? std::move(outputTypeVars_) : std::set<std::string>{},
        isDecl ? std::move(typeVarUses_) : std::unordered_map<std::string, size_t>{}};
    if (isDecl) {
        autoTypeVars_.clear();
        typeVarUses_.clear();
    }
    pushScope();
    declSignature_ = isDecl;
    openSignature_ = true;
    FunctionType *funcType =
        tt::as_ptr<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    // The parameters of a lambda whose type was left out (design principles 5.9), filled by GIR
    // from the function type the lambda stands in.
    const std::vector<Tree::UnwrittenParam> unwrittenParams =
        std::exchange(pendingUnwrittenParams_, {});
    // A parameter destructured as a tuple (`((a, b)) => ...`) is bound by GIR when it starts the
    // body: the parameter is one value, and the names take its elements.
    std::vector<Tree::TupleParamBinding> tupleParams;
    {
        size_t index = 0;
        for (size_t group = 0; group < 2; ++group) {
            for (const auto &paramPair :
                 *ast->atAs<AST::FuncTypeLoad>(0)->atAs<AST::RepeatedLoad>(group)) {
                const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
                if (paramLoad->isTupleParam()) {
                    tupleParams.push_back({index, paramLoad->tupleNames()});
                }
                ++index;
            }
        }
    }
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
                        autoTypeVars_.contains(name)
                            ? "it is inside a union, or is the argument of a type function "
                              "that does not use it"
                            : "its 'typeas' is inside a union, or is the argument of a type "
                              "function that does not use it");
                throw BuildAbortException();
            }
        }
    }
    node_ptr_t typeNode = tree_->makeType(funcType);
    // A declaration without a body declares an operator of a C++ module (design principles 6.4):
    // it has a signature and nothing to build, so the FUNC carries its type alone and is marked
    // as a declaration. The graph builder turns it into an operator group instead of a graph
    // (handover H19, step 1).
    const bool nativeDeclaration = ast->size() < 2;
    if (!nativeDeclaration) {
        // A module found beside a `.cmo` is that C++ module's declaration module: the C++
        // module implements the functions, so a body written there is in the wrong file
        // (design principles 6.4; handover H19, step 1).
        auto declaredModule = std::dynamic_pointer_cast<UserDefinedModule>(module_);
        if (declaredModule && declaredModule->declarationModule()) {
            diags_->of(SemanticDiag::DeclarationModuleHasBody)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit(module_->name(), funcData->ref().toString());
            throw BuildAbortException();
        }
    }
    if (nativeDeclaration) {
        if (!funcType->hasExitType()) {
            diags_->of(SemanticDiag::NativeDeclarationIncomplete)
                .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                .commit(funcData->ref().toString());
            throw BuildAbortException();
        }
        node_ptr_t funcNode = tree_->makeFunc(funcData->ref().toString());
        funcNode->setNativeDeclaration();
        *funcNode << typeNode;
        setOriginFromAst(
            context_, typeNode, ast, camel::source::OriginKind::GctNode, "gct.func.type");
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
                semanticPart(
                    camel::source::SemanticRole::ReturnType, nodeOrigin(typeNode), -1, "type"),
            },
            {nodeOrigin(typeNode)});
        LEAVE("FuncData");
        return funcNode;
    }
    // The body of a function or lambda is a body of its own: its value is the function's
    // result, and its exits are the function's, wherever the function is written.
    const ValueUseScope valueUseScope(*this);
    valueUse_            = {};
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    if (isDecl) {
        // A type variable written once is usually a misspelled type name (design principles
        // 5.5). The count includes the occurrence in a parameter type that bound it.
        for (const std::string &name : autoTypeVars_) {
            if (typeVarUses_[name] <= 1 && !outputTypeVars_.contains(name)) {
                diags_->of(SemanticDiag::TypeVariableUsedOnce)
                    .at(ast->atAs<AST::FuncTypeLoad>(0)->load()->tokenRange())
                    .commit(name, funcData->ref().toString());
            }
        }
    }
    node_ptr_t funcNode = tree_->makeFunc(funcData->ref().toString());
    *funcNode << typeNode << stmtsNode;
    if (!unwrittenParams.empty()) {
        tree_->setUnwrittenParams(funcNode, unwrittenParams);
    }
    if (!tupleParams.empty()) {
        tree_->setTupleParams(funcNode, tupleParams);
    }
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
    Inter,
    Diff,
    KeyUnion,
    KeyInter,
    KeyDiff,
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
    // The fields of the struct a type declaration writes are its own: a tag written on one of
    // them belongs to that declaration (visitTypeDecl). A struct written anywhere else has no
    // declaration to hold its field tags, which is reported rather than dropped.
    const node_ptr_t owner = std::exchange(fieldTagOwner_, nullptr);
    size_t index           = 0;
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedType = child->loadAs<AST::NamedTypeLoad>();
        const string &name    = namedType->getRef().ident();
        if (owner != nullptr) {
            tree_->addFieldTagUses(owner, index, tagUsesOf(child));
        } else {
            for (const auto &use : tagUsesOf(child)) {
                diags_->of(SemanticDiag::TagOnUnnamedField).atOrigin(use.origin).commit(
                    namedType->getRef().toString());
            }
        }
        ++index;
        Type *type = visitType(child->atAs<AST::TypeLoad>(0));

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
FuncType(Modifier[] modifiers, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
namespace {

/// The dimension names a parameter type binds with `constas` (design principles 5.4).
void collectDimensionNames(const AST::node_ptr_t &ast, std::set<std::string> &names) {
    if (!ast) {
        return;
    }
    if (ast->type() == AST::LoadType::Data) {
        const auto &data = ast->loadAs<AST::DataLoad>();
        if (data->dataType() == AST::DataType::ConstAs) {
            names.insert(ast->loadAs<AST::ConstAsLoad>()->ref().ident());
            return;
        }
    }
    for (size_t i = 0; i < ast->size(); ++i) {
        collectDimensionNames(ast->at(i), names);
    }
}

} // namespace

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
    // Named dimensions (design principles 5.4). A declaration gets a scope of its own, and only
    // its parameter types bind into it; the return type may use the names the parameters bound.
    // A function type nested in another signature sees those names but binds none of its own.
    struct DimGuard {
        Builder &self;
        std::shared_ptr<std::set<std::string>> names;
        std::shared_ptr<std::set<std::string>> bound;
        bool bind;
        explicit DimGuard(Builder &builder)
            : self(builder), names(builder.dimNames_), bound(builder.boundDims_),
              bind(builder.dimBindAllowed_) {}
        ~DimGuard() {
            self.dimNames_       = names;
            self.boundDims_      = bound;
            self.dimBindAllowed_ = bind;
        }
    } dimGuard(*this);
    if (signature) {
        dimNames_  = std::make_shared<std::set<std::string>>();
        boundDims_ = std::make_shared<std::set<std::string>>();
    }
    dimBindAllowed_ = false;
    if (signature) {
        // A `constas n: int` parameter binds the name `n` for the signature, like the `constas`
        // of a `const [constas n]` shape: the parameter types and the result type may use it.
        // The name is bound before any parameter type is read, so a parameter may write it
        // before the parameter that declares it (the order is checked below).
        for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(1)) {
            const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
            if (paramLoad->isConstas()) {
                dimNames_->insert(paramLoad->getRef().ident());
            }
        }
        // A name is bound by the `constas` occurrence anywhere in the parameters, the way
        // `typeas T` declares a type variable for the whole signature: an extent may write the
        // name before the parameter that binds it.
        for (size_t group = 0; group < 2; ++group) {
            for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(group)) {
                // An untyped rest parameter has no type child to scan for `constas`.
                if (paramPair->size() > 0) {
                    collectDimensionNames(paramPair->atAs<AST::TypeLoad>(0), *dimNames_);
                }
            }
        }
    }

    FunctionTypeFactory factory;

    factory.setModifiers(typeLoad->modifiers());

    std::unordered_set<string> paramNames;

    // The parameters of a lambda that leave their type out (design principles 5.9). The
    // signature stands `void` in their place; the index (with parameters first) and the origin
    // of the pair go to the FUNC node, and GIR, which alone knows the function type the lambda
    // stands in, fills the type or reports the parameter.
    std::vector<Tree::UnwrittenParam> unwrittenParams;
    size_t paramIndex = 0;
    const auto paramTypeOf = [&](const AST::node_ptr_t &paramPair,
                                 const string &name) -> Type * {
        const size_t index              = paramIndex++;
        const AST::node_ptr_t &typeNode = paramPair->at(0);
        const bool omitted =
            typeNode->type() == AST::LoadType::Type &&
            typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Infer;
        if (!omitted) {
            return visitType(typeNode);
        }
        if (!open) {
            // A function type and a declaration write every parameter's type; the parser only
            // leaves one out in a lambda's parameter list.
            diags_->of(SemanticDiag::LambdaParameterWithoutType)
                .atOrigin(typeNode->load()->origin())
                .commit(name, "a function type and a declaration write every parameter's type");
            throw BuildAbortException();
        }
        unwrittenParams.push_back({index, typeNode->load()->origin()});
        return Type::Void();
    };

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
            dimBindAllowed_ = signature;
            type            = paramTypeOf(paramPair, name);
            dimBindAllowed_ = false;
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
    const auto &normParams = *ast->atAs<AST::RepeatedLoad>(1);
    for (size_t pi = 0; pi < normParams.size(); ++pi) {
        const auto &paramPair = normParams[pi];
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.plain()) {
            // Parameter references must be unqualified
            diags_->of(SemanticDiag::ParamRefMustBeUnqualified)
                .at(paramLoad->tokenRange())
                .commit(paramRef.toString());
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        if (paramLoad->isConstas()) {
            // Only a function declaration offers `constas` in its parameter list (the parser
            // admits it nowhere else). A declaration may have any number of them, in any
            // position, and their values are not read by a type necessarily: the body may use
            // them (design principles 5.4, H51 ruling 2).
            if (!signature) {
                ASSERT(false, "A constas parameter outside a function declaration.");
                diags_->of(SemanticDiag::FeatureNotSupported)
                    .at(paramLoad->tokenRange())
                    .commit("a constas parameter outside a function declaration");
                throw BuildAbortException();
            }
            if (paramPair->size() == 0) {
                diags_->of(SemanticDiag::ConstasTypeRequired)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            Type *const type = visitType(paramPair->at(0));
            if (!paramNames.insert(name).second) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            factory.addConstasArg(name, type);
            continue;
        }
        if (paramLoad->isRest()) {
            // A rest parameter is untyped and last (design principles 5.5); it stands in the
            // signature as a `void` parameter that no call passes a value for.
            if (!signature) {
                // The parser admits `..` only in a function declaration's parameter list.
                ASSERT(false, "A rest parameter outside a function declaration.");
                diags_->of(SemanticDiag::FeatureNotSupported)
                    .at(paramLoad->tokenRange())
                    .commit("a rest parameter outside a function declaration");
                throw BuildAbortException();
            }
            if (pi + 1 != normParams.size()) {
                diags_->of(SemanticDiag::RestParameterNotLast)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            if (paramPair->size() > 0) {
                diags_->of(SemanticDiag::RestParameterTypeNotSupported)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            if (!paramNames.insert(name).second) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            factory.addRestArg(name);
            continue;
        }
        bool isVar = paramLoad->isVar();
        if (isVar && !typeLoad->modifiers().sync()) {
            // variable parameters are not allowed in asynchronous functions
            diags_->of(SemanticDiag::VarParamInAsyncFunction).at(paramLoad->tokenRange()).commit();
        }
        Type *type = nullptr;
        {
            dimBindAllowed_ = signature;
            type            = paramTypeOf(paramPair, name);
            dimBindAllowed_ = false;
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
        const bool outerNestedFuncReturn = inNestedFuncReturn_;
        if (!signature) {
            inNestedFuncReturn_ = true;
        }
        dimBindAllowed_ = false;
        factory.setExitType(visitType(exitTypeNode));
        inNestedFuncReturn_ = outerNestedFuncReturn;
    }

    LEAVE("FuncType");
    if (!unwrittenParams.empty()) {
        pendingUnwrittenParams_ = std::move(unwrittenParams);
    }
    FunctionType *built = open ? static_cast<FunctionType *>(factory.buildOpen())
                               : factory.build();
    return built;
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
    // The arguments in source order: types, and constants written `const <data>`. What an
    // argument means is the type's business (`Type::applyArguments`); an argument the type does
    // not accept is an error here, never dropped.
    std::vector<TypeArgument> arguments;
    const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
    for (const auto &child : *argsNode) {
        if (child->type() == AST::LoadType::Type) {
            arguments.push_back(TypeArgument{.type = visitType(child), .data = nullptr});
            continue;
        }
        if (namesDimension(child)) {
            // A shape written over dimension names: the extents stay compile-time forms over
            // the names the signature binds, not a runtime array constant.
            std::shared_ptr<const std::vector<camel::core::type::ExtentForm>> extents =
                visitNamedExtents(child);
            if (!extents) {
                throw BuildAbortException();
            }
            arguments.push_back(
                TypeArgument{.type = nullptr, .data = nullptr, .extents = std::move(extents)});
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
        Type *keyType                          = arguments.front().type;
        camel::source::origin_id_t fieldOrigin = camel::source::kInvalidOriginId;
        std::string fieldLabel;
        if (std::string problem = collectionKeyProblem(keyType, &fieldOrigin, &fieldLabel);
            !problem.empty()) {
            auto diag = diags_->of(SemanticDiag::CollectionKeyNotAllowed)
                            .at(ast->load()->tokenRange());
            if (fieldOrigin != camel::source::kInvalidOriginId) {
                // The enum is a key for what its fields are: say where the field that is not
                // one is declared.
                diag.relatedOrigin(fieldOrigin, fieldLabel + " is declared here");
            }
            diag.commit(keyType->toString(), coll->toString(), problem);
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
    std::string error;
    // What the arguments denote is the type's answer (`Type::applyArguments`): the core array
    // type, a module type and a type that takes no arguments are all asked the same way, so a
    // type the core gains does not need a second path here.
    const Type *res = base->applyArguments(arguments, error);
    if (res == nullptr) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(base->toString(), error.empty() ? std::string("no reason given") : error);
        throw BuildAbortException();
    }
    LEAVE("SpecType");
    return const_cast<Type *>(res);
}

namespace {

using camel::core::type::ExtentForm;

/// `form * factor`: a constant multiple keeps the extent linear.
ExtentForm scaleExtent(const ExtentForm &form, int64_t factor) {
    if (form.isUnknown()) {
        return form;
    }
    std::vector<std::pair<std::string, int64_t>> terms;
    for (const auto &[name, coefficient] : form.terms()) {
        terms.emplace_back(name, coefficient * factor);
    }
    return ExtentForm::linear(form.constant() * factor, std::move(terms));
}

/// `a + sign * b`.
ExtentForm addExtents(const ExtentForm &a, const ExtentForm &b, int64_t sign) {
    if (a.isUnknown() || b.isUnknown()) {
        return ExtentForm::unknown();
    }
    std::vector<std::pair<std::string, int64_t>> terms = a.terms();
    for (const auto &[name, coefficient] : b.terms()) {
        terms.emplace_back(name, coefficient * sign);
    }
    return ExtentForm::linear(a.constant() + sign * b.constant(), std::move(terms));
}

} // namespace

bool Builder::namesDimension(const AST::node_ptr_t &ast) const {
    if (!ast || ast->type() != AST::LoadType::Data) {
        return false;
    }
    const auto &load = ast->loadAs<AST::DataLoad>();
    if (load->dataType() == AST::DataType::ConstAs) {
        return true;
    }
    if (load->dataType() != AST::DataType::Array || ast->size() != 1) {
        return false;
    }
    // An array of extents written with a plain name anywhere is read as a shape over dimension
    // names. That is how a name that no parameter binds (a typo, or one written only in the
    // return type) is reported as a name rather than as a constant the type cannot read.
    std::function<bool(const AST::node_ptr_t &)> names = [&](const AST::node_ptr_t &node) {
        if (!node) {
            return false;
        }
        if (node->type() == AST::LoadType::Data) {
            const auto &data = node->loadAs<AST::DataLoad>();
            if (data->dataType() == AST::DataType::ConstAs) {
                return true;
            }
            if (data->dataType() == AST::DataType::Ref) {
                return node->loadAs<AST::RefDataLoad>()->ref().plain();
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            if (names(node->at(i))) {
                return true;
            }
        }
        return false;
    };
    return names(ast);
}

std::shared_ptr<const std::vector<camel::core::type::ExtentForm>>
Builder::visitNamedExtents(const AST::node_ptr_t &ast) {
    using camel::core::type::ExtentForm;
    const auto &load = ast->loadAs<AST::DataLoad>();
    // `[]` is a scalar tensor's shape; a comprehension is not a shape.
    if (load->dataType() != AST::DataType::Array || ast->size() != 1) {
        diags_->of(SemanticDiag::InvalidExtentExpression).at(ast->load()->tokenRange()).commit();
        return nullptr;
    }
    auto extents      = std::make_shared<std::vector<ExtentForm>>();
    const auto &items = ast->atAs<AST::RepeatedLoad>(0);
    for (const auto &item : *items) {
        extents->push_back(visitExtent(item));
    }
    return extents;
}

camel::core::type::ExtentForm Builder::visitExtent(const AST::node_ptr_t &ast) {
    using camel::core::type::ExtentForm;
    const auto report = [&]() -> ExtentForm {
        diags_->of(SemanticDiag::InvalidExtentExpression).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    };
    if (ast->type() != AST::LoadType::Data) {
        return report();
    }
    const auto &load = ast->loadAs<AST::DataLoad>();
    switch (load->dataType()) {
    case AST::DataType::Literal: {
        const Literal &literal = ast->loadAs<AST::LiteralLoad>()->value();
        if (literal.type() != LiteralType::Integer) {
            return report();
        }
        const IntegerLiteral parsed = parseIntegerLiteral(literal.data());
        if (parsed.status != IntegerLiteralStatus::Ok) {
            return report();
        }
        return ExtentForm::constant(parsed.value);
    }
    case AST::DataType::ConstAs: {
        const Reference &ref   = ast->loadAs<AST::ConstAsLoad>()->ref();
        const std::string name = ref.plain() ? ref.ident() : ref.toString();
        if (!dimBindAllowed_ || boundDims_ == nullptr) {
            diags_->of(SemanticDiag::ConstasNotAllowed).at(ast->load()->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        if (!boundDims_->insert(name).second) {
            diags_->of(SemanticDiag::DimensionNameAlreadyBound)
                .at(ast->load()->tokenRange())
                .commit(name);
            throw BuildAbortException();
        }
        return ExtentForm::linear({{name, 1}});
    }
    case AST::DataType::Ref: {
        const Reference &ref = ast->loadAs<AST::RefDataLoad>()->ref();
        if (ref.plain() && dimNames_ != nullptr && dimNames_->contains(ref.ident())) {
            return ExtentForm::linear({{ref.ident(), 1}});
        }
        diags_->of(SemanticDiag::DimensionNameNotBound)
            .at(ast->load()->tokenRange())
            .commit(ref.toString());
        throw BuildAbortException();
    }
    case AST::DataType::UnaryExpr: {
        const auto &unary = ast->loadAs<AST::UnaryExprLoad>();
        if (unary->op() != AST::UnaryDataOp::Neg) {
            return ExtentForm::unknown();
        }
        return scaleExtent(visitExtent(ast->at(0)), -1);
    }
    case AST::DataType::BinaryExpr: {
        const auto &binary   = ast->loadAs<AST::BinaryExprLoad>();
        const ExtentForm lhs = visitExtent(ast->at(0));
        const ExtentForm rhs = visitExtent(ast->at(1));
        switch (binary->op()) {
        case AST::BinaryDataOp::Add:
            return addExtents(lhs, rhs, 1);
        case AST::BinaryDataOp::Sub:
            return addExtents(lhs, rhs, -1);
        case AST::BinaryDataOp::Mul:
            // Linear over names means a constant multiple only; a product of two named
            // dimensions is not linear and stays unknown (design principles 5.4).
            if (lhs.isConstant() && !rhs.isUnknown()) {
                return scaleExtent(rhs, lhs.constant());
            }
            if (rhs.isConstant() && !lhs.isUnknown()) {
                return scaleExtent(lhs, rhs.constant());
            }
            return ExtentForm::unknown();
        default:
            return ExtentForm::unknown();
        }
    }
    default:
        return report();
    }
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
    std::optional<Reference> foundRef;
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && !found;) {
        std::vector<std::string> parts(namespacePath_.begin(), namespacePath_.begin() + depth);
        const Reference candidate(own);
        parts.insert(parts.end(), candidate.paths().begin(), candidate.paths().end());
        parts.push_back(candidate.ident());
        Reference lookup(parts);
        found = typeScope_->get(lookup);
        if (found) {
            foundRef = std::move(lookup);
        }
    }
    // What the builders report about a use of a type name: the type it denotes and where that
    // type is declared, when the declaration is in the sources (not for a built-in name or a
    // type variable). The language server reads this instead of resolving type names itself.
    const auto report = [&](Type *type, origin_id_t decl) -> Type * {
        if (auto *recorder = context_ ? context_->semanticRecorder() : nullptr) {
            recorder->typeReferenced(ast->load()->origin(), ref.ident(), decl, type);
        }
        return type;
    };
    // Where the module a name is imported from declared it.
    const auto importedOrigin = [&](const Reference &name) -> origin_id_t {
        if (const auto source = module_->importedSource(name, ImportLayer::Named)) {
            return source->first->typeDeclarationOrigin(source->second);
        }
        return 0;
    };
    if (found) {
        // An imported type named `Map` or `Set` comes before the built-in of that name.
        if (isCollectionPrototype(found.value())) {
            if (const auto imported = module_->getImportedType(ref); imported.has_value()) {
                return report(imported.value(), importedOrigin(ref));
            }
        }
        // A type variable bound automatically (design principles 5.5) that is written again:
        // the occurrence count decides whether it is reported as a likely misspelling.
        if (dynamic_cast<TypeVarType *>(found.value()) != nullptr &&
            autoTypeVars_.contains(written)) {
            ++typeVarUses_[written];
        }
        return report(found.value(), module_->typeDeclarationOrigin(*foundRef));
    }
    // The types the module itself exports without a type declaration in this file: a `.cml`
    // beside a `.cmo` is that C++ module's declaration module, so what the C++ module exports
    // is directly visible here, as if it were written at the top of this file (design
    // principles 6.4; handover H94, ruling 1).
    if (const auto exported = module_->getExportedType(Reference(own));
        exported.has_value()) {
        return exported.value();
    }
    // Layer 3: imported by name, or through a module or namespace prefix.
    if (const auto imported = module_->getImportedType(ref, ImportLayer::Named)) {
        return report(imported.value(), importedOrigin(ref));
    }
    if (const auto source = module_->importedSource(ref, ImportLayer::Named)) {
        // Imported, and not declared (yet): perhaps the types form a cycle across modules.
        rejectTypeCycle(source->first, source->second, ref, ast);
    }
    // Layer 4: through a `using namespace` in force; two that give different types are an
    // error where the name is used.
    struct UsedType {
        std::string shown;
        Type *type;
        origin_id_t origin;
    };
    std::vector<UsedType> viaUsing;
    for (const auto &frame : usingFrames_) {
        for (const Reference &using_ : frame) {
            const std::string prefixed = using_.toString() + "::" + written;
            Type *type                 = nullptr;
            origin_id_t origin         = 0;
            // The full name of the candidate, for AmbiguousName (design principles 10.2).
            std::string shown = prefixed;
            for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && type == nullptr;) {
                std::string candidate;
                for (size_t i = 0; i < depth; ++i) {
                    candidate += namespacePath_[i] + "::";
                }
                if (auto own = typeScope_->get(Reference(candidate + prefixed))) {
                    type   = own.value();
                    origin = module_->typeDeclarationOrigin(Reference(candidate + prefixed));
                    shown  = module_->qualifiedName(Reference(candidate + prefixed));
                }
            }
            if (type == nullptr) {
                if (auto imported =
                        module_->getImportedType(Reference(prefixed), ImportLayer::Named)) {
                    type   = imported.value();
                    origin = importedOrigin(Reference(prefixed));
                    if (const auto source =
                            module_->importedSource(Reference(prefixed), ImportLayer::Named)) {
                        shown = source->first->qualifiedName(source->second);
                    }
                }
            }
            if (type != nullptr && std::ranges::none_of(viaUsing, [&](const auto &have) {
                    return have.type == type;
                })) {
                viaUsing.push_back(UsedType{shown, type, origin});
            }
        }
    }
    if (viaUsing.size() == 1) {
        return report(viaUsing.front().type, viaUsing.front().origin);
    }
    if (viaUsing.size() > 1) {
        std::string sources;
        for (const auto &[name, type, origin] : viaUsing) {
            (void)type;
            (void)origin;
            sources += (sources.empty() ? "" : ", ") + name;
        }
        diags_->of(SemanticDiag::AmbiguousName)
            .at(ast->load()->tokenRange())
            .commit(written, sources);
        throw BuildAbortException();
    }
    // Layer 5: what the built-in module and the modules of operators bring along.
    if (const auto ambient = module_->getImportedType(ref, ImportLayer::Ambient)) {
        return report(ambient.value(), 0);
    }
    if (wholeImportFailed_ || poisonedTypes_.contains(written)) {
        throw BuildAbortException();
    }
    // A name that is no type in scope binds a type variable where it first appears in a
    // parameter type (design principles 5.5; the rule of a dimension name in 5.4). The result
    // type cannot bind one: a caller has no argument that could say what it is.
    if (typeasAllowed_ && ref.plain()) {
        if (inDeclReturnType_) {
            diags_->of(SemanticDiag::TypeVariableNotBound)
                .at(ast->load()->tokenRange())
                .commit(written);
            throw BuildAbortException();
        }
        typeScope_->insert(ref, TypeVarType::create(written, false));
        declaredTypeVars_.push_back(written);
        autoTypeVars_.insert(written);
        typeVarUses_[written] = 1;
        if (inNestedFuncReturn_) {
            outputTypeVars_.insert(written);
        }
        return report(TypeVarType::create(written, true), 0);
    }
    diags_->of(SemanticDiag::UnresolvedTypeReference).at(ast->load()->tokenRange()).commit(written);
    throw BuildAbortException();
}
} // namespace camel::compile::gct

