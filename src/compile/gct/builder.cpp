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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"

#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/type/other.h"
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

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(
        std::dynamic_pointer_cast<Load>(std::make_shared<LoadType>(std::forward<Args>(args)...)));
}

inline camel::source::origin_id_t nodeOrigin(const node_ptr_t &node) {
    return node ? node->load()->origin() : camel::source::kInvalidOriginId;
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
    node->load()->setOrigin(sourceContext->deriveOrigin(
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
    // GCT 是 AST 的有损 lowering：
    // 一个 AST 节点可能被改写成别的语义形态，但这里仍沿主派生链继承 AST 的 primarySpan，
    // 从而保证语义诊断与后续 GIR/debugger 至少能回到“最主要的源码责任区间”。
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
    node->load()->setOrigin(sourceContext->deriveOrigin(
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
    if (node->type() == LoadType::DATA) {
        return node->loadAs<DataLoad>()->data();
    } else if (node->type() == LoadType::DREF) {
        return make_shared<RefData>(node->loadAs<DRefLoad>()->ref());
    } else {
        return nullptr;
    }
}

pair<node_ptr_t, data_ptr_t> Builder::makeRefData(const node_ptr_t &expr) {
    const string id    = std::to_string(idIndex_++);
    node_ptr_t refNode = createNodeAs<NRefLoad>(id);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(id);
    return make_pair(refNode, data);
}

pair<data_ptr_t, bool> Builder::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticDataFromNode(node);
    if (data) {
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
    typeScope_->insert(Reference("any"), Type::Any());
    typeScope_->insert(Reference("void"), Type::Void());
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
node_ptr_t Builder::visitModule(const AST::node_ptr_t &ast) {
    ENTER("Module");
    // Ensure the AST node is of the expected type
    ASSERT(ast->type() == AST::LoadType::Module, "Expected ModuleLoad type");

    // Extract different parts of the module node
    auto importNodes   = ast->atAs<AST::RepeatedLoad>(0);
    auto exportOptNode = ast->atAs<AST::OptionalLoad>(1);
    auto stmtNodes     = ast->atAs<AST::RepeatedLoad>(2);

    // Process import declarations
    for (const auto &import : *importNodes) {
        visitImport(import);
    }

    vector<node_ptr_t> decls;
    vector<node_ptr_t> stmts;

    root_ = createNodeAs<ExecLoad>();

    for (auto &stmt : *stmtNodes) {
        try {
            node_ptr_t node = visitStmt(stmt);
            if (node->type() == GCT::LoadType::DECL) {
                decls.push_back(node);
            } else if (node->type() == GCT::LoadType::FUNC) {
                // Handle function declarations
                const auto &funcLoad = node->loadAs<FuncLoad>();
                node_ptr_t declNode  = createNodeAs<DeclLoad>(funcLoad->name(), true);
                node_ptr_t typeNode  = node->atAs<TypeLoad>(0);
                *declNode << typeNode->clone();
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
                            funcLoad->name()),
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
            *root_ << decl;
        }
    }

    // Add collected statements to the root node
    for (const auto &stmt : stmts) {
        *root_ << stmt;
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
        *root_ << exportNode;
    }

    LEAVE("Module");
    setOriginFromAst(context_, root_, ast, camel::source::OriginKind::GctNode, "gct.module");
    registerGctSemantic(context_, root_, ast);
    return root_;
}

/*
ImportDecl(string path, Ref[] refs, Ref as) ;
*/
void_ptr_t Builder::visitImport(const AST::node_ptr_t &ast) {
    ENTER("ImportDecl");
    // Ensure the AST node is of the expected type
    ASSERT(ast->type() == AST::LoadType::Import, "Expected ImportLoad type");

    // Extract the import load details
    const auto &load = ast->loadAs<AST::ImportLoad>();
    const auto &path = load->getPath();
    const auto &refs = load->getRefs();

    // Attempt to import the module (importModule throws ModuleNotFound with detail on failure)
    const module_ptr_t &mod = context_->importModule(path, module_->name());
    if (!mod) {
        diags_->of(SemanticDiag::ModuleNotFound)
            .at(load->tokenRange())
            .commit(path, "import returned empty.");
        throw BuildAbortException();
    }

    // Import all references if none are specified
    if (refs.empty()) {
        module_->importAllRefsFromMod(mod);
    } else {
        // Import specific references
        for (const Reference &ref : refs) {
            module_->markImportedRefFromMod(ref, mod);
        }
    }

    LEAVE("ImportDecl");
    return nullptr;
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
    node_ptr_t res = createNodeAs<ExptLoad>(refs);

    LEAVE("ExportDecl");
    return res;
}

node_ptr_t Builder::visitStmt(const AST::node_ptr_t &ast) {
    ENTER("Stmt");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type");
    const auto &stmt = ast->loadAs<AST::StmtLoad>();
    node_ptr_t stmtNode;

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
    case AST::StmtType::Name:
        stmtNode = visitNameDecl(ast);
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

    // Create an execution node as the initial result
    node_ptr_t res = createNodeAs<ExecLoad>();

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
                node_ptr_t dataNode     = visitData(dataASTNode);

                // Validate the identifier
                if (!validateIdent(ref.ident())) {
                    diags_->of(SemanticDiag::ReservedIdentifier)
                        .at(dataDeclLoad->tokenRange())
                        .commit();
                    throw BuildAbortException();
                }

                if (i < typeNodes->size()) {
                    const auto &typeASTNode = typeNodes->atAs<AST::TypeLoad>(i);
                    Type *type              = visitType(typeASTNode);
                    // 先尝试做静态类型转换
                    if (dataNode->type() == LoadType::DATA) {
                        const auto &dataLoad = dataNode->loadAs<DataLoad>();
                        const auto &data     = dataLoad->data();
                        auto convertedData   = data->convertTo(type);
                        if (convertedData) {
                            dataNode = createNodeAs<DataLoad>(convertedData);
                        } else {
                            diags_->of(SemanticDiag::LiteralStaticCastFailed)
                                .at(ast->load()->tokenRange())
                                .commit(data->toString(), type->toString());
                            throw BuildAbortException();
                        }
                    } else {
                        diags_->of(SemanticDiag::FeatureNotSupported)
                            .at(ast->load()->tokenRange())
                            .commit("dynamic type casting");
                        throw BuildAbortException();
                    }
                }

                // Ensure the data node is of type Data
                ASSERT(
                    dataASTNode->type() == AST::LoadType::Data,
                    "Tuple unpacking requires Data type nodes");
                // Create a reference node and link the data node to it
                node_ptr_t nRefNode = createNodeAs<NRefLoad>(ref.ident());
                *nRefNode << dataNode;
                res = nRefNode;
            }
        } else {
            if (dataNodes->size() == 1) {
                // If there is only one data node, process it as a reference and access node
                const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(0);
                node_ptr_t dataNode     = visitData(dataASTNode);

                if (typeNodes->size() > 0) {
                    const auto &typeASTNode = typeNodes->atAs<AST::TypeLoad>(0);
                    Type *type              = visitType(typeASTNode);
                    // 先尝试做静态类型转换
                    if (dataNode->type() == LoadType::DATA) {
                        const auto &dataLoad = dataNode->loadAs<DataLoad>();
                        const auto &data     = dataLoad->data();
                        auto convertedData   = data->convertTo(type);
                        if (convertedData) {
                            dataNode = createNodeAs<DataLoad>(convertedData);
                        } else {
                            diags_->of(SemanticDiag::LiteralStaticCastFailed)
                                .at(ast->load()->tokenRange())
                                .commit(data->toString(), type->toString());
                            throw BuildAbortException();
                        }
                    } else {
                        diags_->of(SemanticDiag::FeatureNotSupported)
                            .at(ast->load()->tokenRange())
                            .commit("dynamic type casting");
                        throw BuildAbortException();
                    }
                }

                const string id     = std::to_string(idIndex_++);
                node_ptr_t nRefNode = createNodeAs<NRefLoad>(id);
                node_ptr_t dRefNode = createNodeAs<DRefLoad>(id);
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
                    node_ptr_t nRefNode = createNodeAs<NRefLoad>(ref.ident());
                    node_ptr_t accsNode = createNodeAs<AccsLoad>(i);
                    *accsNode << dRefNode->clone();
                    *nRefNode << accsNode;
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
        node_ptr_t variNode = createNodeAs<VariLoad>();
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

/*
FuncDecl(Ref ref) : FuncData ;
*/
node_ptr_t Builder::visitFuncDecl(const AST::node_ptr_t &ast) {
    ENTER("FuncDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for FuncDecl");
    const auto &funcDataNode = ast->atAs<AST::FuncDataLoad>(0);
    const auto &funcLoad     = ast->loadAs<AST::FuncDeclLoad>();
    node_ptr_t funcNode      = visitFuncData(funcDataNode);
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
    type                 = visitType(typeNode);
    const auto &typeLoad = ast->loadAs<AST::TypeDeclLoad>();
    typeScope_->insert(typeLoad->ref(), type);
    node_ptr_t declNode = createNodeAs<DeclLoad>(typeLoad->ref(), false);
    *declNode << createNodeAs<TypeLoad>(type, typeLoad->implMark(), typeLoad->uri());
    LEAVE("TypeDecl");
    return declNode;
}

/*
NameDecl(Ref ref, Ref alias) ;
*/
node_ptr_t Builder::visitNameDecl(const AST::node_ptr_t &ast) {
    ENTER("NameDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for NameDecl");
    diags_->of(SemanticDiag::FeatureNotSupported)
        .at(ast->load()->tokenRange())
        .commit("Name Redeclaration");
    throw BuildAbortException();
    LEAVE("NameDecl");
    return nullptr;
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

    // Create an ExitLoad node to represent the exit statement
    node_ptr_t exitNode = createNodeAs<ExitLoad>();

    // Retrieve the data nodes associated with the exit statement
    const auto &dataNodes = ast->atAs<AST::RepeatedLoad>(0);

    // If there are data nodes, process them
    if (dataNodes->size() > 0) {
        // Case 1: Single data node
        if (dataNodes->size() == 1) {
            const auto &dataNode = dataNodes->front();
            node_ptr_t d         = visitData(dataNode);
            *exitNode << visitData(dataNode);
        } else {
            // Case 2: Multiple data nodes — 用 Factory 一次构建，避免重复计算 Type
            TupleDataFactory tupleFactory;
            bool dangling       = false;
            node_ptr_t execNode = createNodeAs<ExecLoad>();

            for (const auto &item : *dataNodes) {
                node_ptr_t dataNode = visitData(item);
                auto [data, _]      = extractData(dataNode, execNode, dangling);
                tupleFactory.add(data);
            }

            auto tupleData      = tupleFactory.build();
            node_ptr_t dataNode = createNodeAs<DataLoad>(tupleData);

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

/*
StmtBlock(bool sync) : Stmt* stmts ;
*/
node_ptr_t Builder::visitStmtBlock(const AST::node_ptr_t &ast) {
    ENTER("StmtBlock");
    // Ensure the AST node is of the expected type (Stmt)
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for StmtBlock");

    // Extract the statement block load and create an ExecLoad node
    // The ExecLoad node represents the execution of the statement block
    const auto &stmtBlock = ast->loadAs<AST::StmtBlockLoad>();
    node_ptr_t execNode   = createNodeAs<ExecLoad>(stmtBlock->synced());

    // Push a new scope for the statement block
    pushScope();

    for (const auto &stmt : *ast->atAs<AST::RepeatedLoad>(0)) {
        // Ensure each item in the block is a statement
        ASSERT(stmt->type() == AST::LoadType::Stmt, "Expected a statement in StmtBlock");
        // Process the statement and attach it to the ExecLoad node
        *execNode << visitStmt(stmt);
    }

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
    const auto &data = ast->loadAs<AST::DataLoad>();
    node_ptr_t dataNode;

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

    node_ptr_t opNode;

    // Determine the unary operator and create the corresponding node
    switch (unaryExpr->op()) {
    case AST::UnaryDataOp::Not: {
        opNode = createNodeAs<DRefLoad>("__not__");
    } break;
    case AST::UnaryDataOp::Neg: {
        opNode = createNodeAs<DRefLoad>("__neg__");
    } break;
    case AST::UnaryDataOp::Inv: {
        opNode = createNodeAs<DRefLoad>("__inv__");
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
    node_ptr_t linkNode    = createNodeAs<LinkLoad>(1);
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

    node_ptr_t opNode;

    // Determine the binary operator and create the corresponding node
    switch (binaryExpr->op()) {
    case AST::BinaryDataOp::Assign: {
        opNode = createNodeAs<DRefLoad>("__assn__");
    } break;
    case AST::BinaryDataOp::AssignAdd: {
        opNode = createNodeAs<DRefLoad>("__assn_add__");
    } break;
    case AST::BinaryDataOp::AssignSub: {
        opNode = createNodeAs<DRefLoad>("__assn_sub__");
    } break;
    case AST::BinaryDataOp::AssignMul: {
        opNode = createNodeAs<DRefLoad>("__assn_mul__");
    } break;
    case AST::BinaryDataOp::AssignDiv: {
        opNode = createNodeAs<DRefLoad>("__assn_div__");
    } break;
    case AST::BinaryDataOp::AssignMod: {
        opNode = createNodeAs<DRefLoad>("__assn_mod__");
    } break;
    case AST::BinaryDataOp::AssignMat: {
        opNode = createNodeAs<DRefLoad>("__assn_mat__");
    } break;
    case AST::BinaryDataOp::AssignExp: {
        opNode = createNodeAs<DRefLoad>("__assn_pow__");
    } break;
    case AST::BinaryDataOp::AssignAnd: {
        opNode = createNodeAs<DRefLoad>("__assn_and__");
    } break;
    case AST::BinaryDataOp::AssignOr: {
        opNode = createNodeAs<DRefLoad>("__assn_or__");
    } break;
    case AST::BinaryDataOp::Or: {
        opNode = createNodeAs<DRefLoad>("__or__");
    } break;
    case AST::BinaryDataOp::And: {
        opNode = createNodeAs<DRefLoad>("__and__");
    } break;
    case AST::BinaryDataOp::Eq: {
        opNode = createNodeAs<DRefLoad>("__eq__");
    } break;
    case AST::BinaryDataOp::Neq: {
        opNode = createNodeAs<DRefLoad>("__neq__");
    } break;
    case AST::BinaryDataOp::StrictEq: {
        opNode = createNodeAs<DRefLoad>("__strict_eq__");
    } break;
    case AST::BinaryDataOp::StrictNeq: {
        opNode = createNodeAs<DRefLoad>("__strict_neq__");
    } break;
    case AST::BinaryDataOp::Less: {
        opNode = createNodeAs<DRefLoad>("__lt__");
    } break;
    case AST::BinaryDataOp::LessEq: {
        opNode = createNodeAs<DRefLoad>("__le__");
    } break;
    case AST::BinaryDataOp::Greater: {
        opNode = createNodeAs<DRefLoad>("__gt__");
    } break;
    case AST::BinaryDataOp::GreaterEq: {
        opNode = createNodeAs<DRefLoad>("__ge__");
    } break;
    case AST::BinaryDataOp::Add: {
        opNode = createNodeAs<DRefLoad>("__add__");
    } break;
    case AST::BinaryDataOp::Sub: {
        opNode = createNodeAs<DRefLoad>("__sub__");
    } break;
    case AST::BinaryDataOp::Mul: {
        opNode = createNodeAs<DRefLoad>("__mul__");
    } break;
    case AST::BinaryDataOp::Div: {
        opNode = createNodeAs<DRefLoad>("__div__");
    } break;
    case AST::BinaryDataOp::Mod: {
        opNode = createNodeAs<DRefLoad>("__mod__");
    } break;
    case AST::BinaryDataOp::Mat: {
        opNode = createNodeAs<DRefLoad>("__mat__");
    } break;
    case AST::BinaryDataOp::Pow: {
        opNode = createNodeAs<DRefLoad>("__pow__");
    } break;
    case AST::BinaryDataOp::Index: {
        opNode = createNodeAs<DRefLoad>("__idx__");
    } break;
    default:
        // If the operator is unknown, assert and return nullptr
        ASSERT(false, "Unknown binary operation");
        return nullptr;
    }
    auto binaryAstBundle = astSemantic(context_, ast);
    setOriginFromOrigin(
        context_,
        opNode,
        findSemanticOrigin(binaryAstBundle, camel::source::SemanticRole::Operator),
        camel::source::OriginKind::GctNode,
        "gct.binary.operator");

    // Create a LinkLoad node to link the operator and the operands
    node_ptr_t res     = createNodeAs<LinkLoad>(2);
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
        node_ptr_t waitNode = createNodeAs<WaitLoad>();
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

    node_ptr_t res;
    const auto &reservedExpr = ast->loadAs<AST::ReservedExprLoad>();
    bool waited              = reservedExpr->waited();
    const auto &lhsASTNode   = ast->atAs<AST::DataLoad>(0);

    // Handle different reserved operators
    switch (reservedExpr->op()) {
    case AST::ReservedDataOp::NullThen: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("NullThen Operator");
        res = visitData(lhsASTNode);
    } break;
    case AST::ReservedDataOp::ErrorThen: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("ErrorThen Operator");
        res = visitData(lhsASTNode);
    } break;
    case AST::ReservedDataOp::NotNullThen: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("NotNullThen Operator");
        res = visitData(lhsASTNode);
    } break;

    case AST::ReservedDataOp::Call: {
        // Handle the "Call" operator
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = createNodeAs<LinkLoad>(argsNode->size());
        const auto &linkLoad   = res->loadAs<LinkLoad>();
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
        // for (const auto &kwargNode : *kwargsNode) {
        //     const auto &namedData = kwargNode->loadAs<AST::NamedDataLoad>();
        //     const auto &dataNode = kwargNode->atAs<AST::DataLoad>(0);
        //     const auto &kwargName = namedData->ref().ident();
        //     linkLoad->addKwarg(kwargName);
        //     *res << visitData(dataNode);
        // }
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.call");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
    } break;

    case AST::ReservedDataOp::Bind: {
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = createNodeAs<WithLoad>(argsNode->size());
        const auto &linkLoad   = res->loadAs<WithLoad>();
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
        // for (const auto &kwargNode : *kwargsNode) {
        //     const auto &namedData = kwargNode->loadAs<AST::NamedDataLoad>();
        //     const auto &dataNode = kwargNode->atAs<AST::DataLoad>(0);
        //     const auto &kwargName = namedData->ref().ident();
        //     linkLoad->addKwarg(kwargName);
        //     *res << visitData(dataNode);
        // }
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.bind");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
    } break;

    case AST::ReservedDataOp::Comp: {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t opNode      = createNodeAs<DRefLoad>("__cmp__");
        res                    = createNodeAs<LinkLoad>(2);
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
        const auto &dataNode   = visitData(lhsASTNode);
        const auto &dstType    = visitType(rhsASTNode);
        if (dataNode->type() == LoadType::DATA) {
            const auto &dataLoad = dataNode->loadAs<DataLoad>();
            const auto &data     = dataLoad->data();
            if (dstType->castSafetyFrom(data->type()) != CastSafety::Safe) {
                diags_->of(SemanticDiag::LiteralStaticCastFailed)
                    .at(ast->load()->tokenRange())
                    .commit(data->toString(), dstType->toString());
                throw BuildAbortException();
            }
            auto convertedData = data->convertTo(dstType);
            if (convertedData) {
                res = createNodeAs<DataLoad>(convertedData);
            } else {
                diags_->of(SemanticDiag::LiteralStaticCastFailed)
                    .at(ast->load()->tokenRange())
                    .commit(data->toString(), dstType->toString());
                throw BuildAbortException();
            }
        } else {
            res = createNodeAs<CastLoad>(dstType);
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
            res          = createNodeAs<AccsLoad>(index);
        } catch (const std::exception &) {
            // Not an index, treat as named access
            res = createNodeAs<AccsLoad>(ref);
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
        node_ptr_t waitNode = createNodeAs<WaitLoad>();
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
    node_ptr_t brchNode = createNodeAs<BrchLoad>();
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.if");

    // condition
    // Process the condition expression and attach it to the branch node
    node_ptr_t condNode = visitData(ast->atAs<AST::DataLoad>(0));
    *brchNode << condNode;

    // Process the "then" block and attach it as the "True" case
    const auto &thenBlock = ast->atAs<AST::StmtBlockLoad>(1);
    node_ptr_t thenNode   = visitStmtBlock(thenBlock);
    node_ptr_t trueCase   = createNodeAs<CaseLoad>(CaseLoad::CaseType::True);
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
    node_ptr_t elseCase  = createNodeAs<CaseLoad>(CaseLoad::CaseType::Else);
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
        node_ptr_t elseBlock = visitStmtBlock(elseNode);
        *elseCase << elseBlock;
    } else {
        // empty else block
        // If no "else" block exists, attach an empty execution block
        *elseCase << createNodeAs<ExecLoad>();
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

*/
node_ptr_t Builder::visitMatchExpr(const AST::node_ptr_t &ast) {
    ENTER("MatchExpr");
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchExpr");

    // Extract the match expression load and the reference for the condition
    const auto &matchExprLoad = ast->loadAs<AST::MatchExprLoad>();
    const Reference &matchRef = matchExprLoad->ref();

    // Create a reference node for the condition and a branch node for the match expression
    node_ptr_t dRefNode = createNodeAs<DRefLoad>(matchRef); // condition node
    node_ptr_t brchNode = createNodeAs<BrchLoad>();
    setOriginFromOrigin(
        context_,
        dRefNode,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchCondition),
        camel::source::OriginKind::GctNode,
        "gct.match.scrutinee");
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.match");
    *brchNode << dRefNode;

    std::vector<camel::source::SemanticPart> parts = {
        semanticPart(
            camel::source::SemanticRole::BranchCondition,
            nodeOrigin(dRefNode),
            -1,
            "scrutinee"),
    };
    std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(dRefNode)};

    for (const auto &aCaseNode : *ast->atAs<AST::RepeatedLoad>(0)) {
        // Handle a value case
        const auto &caseLoadType = aCaseNode->front()->type();
        node_ptr_t gCaseNode     = nullptr;

        if (caseLoadType != AST::LoadType::Null) {
            gCaseNode               = createNodeAs<CaseLoad>(CaseLoad::CaseType::Value);
            node_ptr_t caseDataNode = visitData(aCaseNode->atAs<AST::DataLoad>(0));
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
            gCaseNode               = createNodeAs<CaseLoad>(CaseLoad::CaseType::Else);
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
        data                = tt::as_shared<Data>(make_shared<StringData>(decoded));
    } break;
    case LiteralType::FString: {
        // FString (formatted string) is not supported
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("FString");
        throw BuildAbortException();
    } break;
    case LiteralType::Integer: {
        // Parse the string as an integer and create a data object
        data = makeDataFromLiteral(parseNumber<int64_t>(str));
    } break;
    case LiteralType::Real: {
        // Parse the string as a floating-point number and create a data object
        data = makeDataFromLiteral(parseNumber<double>(str));
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
    node_ptr_t res = createNodeAs<DataLoad>(data);
    LEAVE("Literal");
    return res;
}

/*
ArrayData() : Data* data ;
*/
node_ptr_t Builder::visitArrayData(const AST::node_ptr_t &ast) {
    ENTER("ArrayData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ArrayData");

    ArrayDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        factory.add(data);
    }

    auto arrayData = factory.build();
    node_ptr_t res = createNodeAs<DataLoad>(arrayData);

    if (dangling) {
        *execNode << res;
        res = execNode;
    }

    LEAVE("ArrayData");
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
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name    = namedPair->ref().ident();
        node_ptr_t dataNode   = visitData(child->atAs<AST::DataLoad>(0));
        auto [data, _]        = extractData(dataNode, execNode, dangling);
        factory.add(name, data);
    }

    auto structData = factory.build();
    node_ptr_t res  = createNodeAs<DataLoad>(structData);

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
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        factory.add(data);
    }

    auto tupleData = factory.build();
    node_ptr_t res = createNodeAs<DataLoad>(tupleData);

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
    FunctionType *funcType =
        tt::as_ptr<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    node_ptr_t typeNode  = createNodeAs<TypeLoad>(funcType, funcType->implMark());
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    node_ptr_t funcNode  = createNodeAs<FuncLoad>(funcData->ref().ident());
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
                funcData->ref().ident()),
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
    node_ptr_t refNode  = createNodeAs<DRefLoad>(refData->ref());
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
    default:
        throw std::runtime_error("Unknown type type");
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
    Type *type = visitType(ast->atAs<AST::TypeLoad>(0));
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
        // Handle union types (e.g., T | U)
        const auto &lhsType = visitType(ast->atAs<AST::TypeLoad>(0));
        const auto &rhsType = visitType(ast->atAs<AST::TypeLoad>(1));
        res                 = UnionType::create(lhsType, rhsType);
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
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::ErrorThen (a.k.a. T ? U)");
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
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("TypeOp::TypeAs");
        throw BuildAbortException();
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
    // TODO: 这里的dims信息暂时没用到
    // const auto &arrayTypeLoad = ast->loadAs<AST::ArrayTypeLoad>();
    Type *type            = visitType(ast->atAs<AST::TypeLoad>(0));
    const auto &arrayType = ArrayType::create(type);
    LEAVE("ArrayType");
    return arrayType;
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

    // Initialize the exit type (return type) as nullptr
    Type *exitType = nullptr;

    // Process the optional exit type node
    const auto &exitTypeNode = ast->optAtAs<AST::TypeLoad>(2);
    if (exitTypeNode) {
        exitType = visitType(exitTypeNode);
    }

    FunctionTypeFactory factory;
    factory.setExitType(exitType);
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
        Type *type         = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data    = nullptr;

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
        Type *type         = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data    = nullptr;

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

    LEAVE("FuncType");
    return factory.build();
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

/*
SpecType : Type base, RepeatedLoad typeOrData ;
*/
Type *Builder::visitSpecType(const AST::node_ptr_t &ast) {
    ENTER("SpecType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for SpecType");
    ASSERT(ast->size() >= 1, "SpecType must have base type");
    Type *base = visitType(ast->atAs<AST::TypeLoad>(0));
    if (ast->size() < 2) {
        LEAVE("SpecType");
        return base;
    }
    type_vec_t typeArgs;
    const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
    for (const auto &child : *argsNode) {
        if (child->type() == AST::LoadType::Type) {
            typeArgs.push_back(visitType(child));
        }
    }
    if (typeArgs.empty()) {
        LEAVE("SpecType");
        return base;
    }
    if (!base->isOtherType()) {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("Only OtherType is allowed to have generic params");
        throw BuildAbortException();
    }
    Type *res = static_cast<OtherType *>(base)->cloneWithParams(
        std::span<Type *const>(typeArgs.data(), typeArgs.size()));
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

    // Attempt to resolve the type from the current scope
    const auto &type = typeScope_->get(ref);

    if (type) {
        // If the type is found in the current scope, return it
        LEAVE("RefType");
        return type.value();
    }

    // Attempt to resolve the type from imported modules
    const auto &importedType = module_->getImportedType(ref);

    if (!importedType.has_value()) {
        // If the type cannot be resolved, log a diagnostic and throw an exception
        diags_->of(SemanticDiag::UnresolvedTypeReference)
            .at(ast->load()->tokenRange())
            .commit(ref.toString());
        throw BuildAbortException();
    }

    LEAVE("RefType");
    return importedType.value();
}
} // namespace camel::compile::gct
