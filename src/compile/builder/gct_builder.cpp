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
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gct_builder.h"

#include "utils/escape.h"
#include "utils/scope.h"
#include "utils/token.h"
#include "utils/type.h"

#define DEBUG_LEVEL -1

using namespace std;

namespace GraphConstructTree {

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(
        std::dynamic_pointer_cast<Load>(std::make_shared<LoadType>(std::forward<Args>(args)...)));
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
    typeScope_->insert(Reference("int"), Type::Int());
    typeScope_->insert(Reference("i32"), Type::Int());
    typeScope_->insert(Reference("long"), Type::Long());
    typeScope_->insert(Reference("i64"), Type::Long());
    typeScope_->insert(Reference("float"), Type::Float());
    typeScope_->insert(Reference("f32"), Type::Float());
    typeScope_->insert(Reference("double"), Type::Double());
    typeScope_->insert(Reference("f64"), Type::Double());
    typeScope_->insert(Reference("real"), Type::Double());
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
        *root_ << exportNode;
    }

    LEAVE("Module");
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

    // Attempt to import the module
    const module_ptr_t &mod = context_->importModule(path, module_->name());
    if (!mod) {
        diags_->of(SemanticDiag::ModuleNotFound).at(load->tokenRange()).commit(path);
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
                const auto &ref      = refs[i];
                const auto &dataNode = dataNodes->atAs<AST::DataLoad>(i);
                // Validate the identifier
                if (!validateIdent(ref.ident())) {
                    diags_->of(SemanticDiag::ReservedIdentifier)
                        .at(dataDeclLoad->tokenRange())
                        .commit();
                    throw BuildAbortException();
                }
                // Ensure the data node is of type Data
                ASSERT(
                    dataNode->type() == AST::LoadType::Data,
                    "Tuple unpacking requires Data type nodes");
                // Create a reference node and link the data node to it
                node_ptr_t nRefNode = createNodeAs<NRefLoad>(ref.ident());
                *nRefNode << visitData(dataNode);
                res = nRefNode;
            }
        } else {
            if (dataNodes->size() == 1) {
                // If there is only one data node, process it as a reference and access node
                const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(0);
                node_ptr_t dataNode     = visitData(dataASTNode);
                const string id         = std::to_string(idIndex_++);
                node_ptr_t nRefNode     = createNodeAs<NRefLoad>(id);
                node_ptr_t dRefNode     = createNodeAs<DRefLoad>(id);
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
    type_ptr_t type;
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
            // Case 2: Multiple data nodes
            auto tupleData      = make_shared<TupleData>();
            node_ptr_t dataNode = createNodeAs<DataLoad>(tupleData);
            bool dangling       = false;
            node_ptr_t execNode = createNodeAs<ExecLoad>();

            // Process each data node and add it to the tuple
            for (const auto &item : *dataNodes) {
                node_ptr_t dataNode = visitData(item);
                auto [data, _]      = extractData(dataNode, execNode, dangling);
                tupleData->emplace(data);
            }

            // If there are dangling nodes, attach them to the execution node
            if (dangling) {
                *execNode << dataNode;
                dataNode = execNode;
            }

            // Attach the processed tuple data to the exit node
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

    // Create a LinkLoad node to link the operator and the operand
    node_ptr_t linkNode = createNodeAs<LinkLoad>(1);
    *linkNode << opNode << visitData(dataASTNode);

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

    // Create a LinkLoad node to link the operator and the operands
    node_ptr_t res = createNodeAs<LinkLoad>(2);
    *res << opNode << visitData(lhsASTNode);

    // Handle the right-hand side (RHS) or indices for specific operators
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        auto indices = ast->atAs<AST::RepeatedLoad>(1);
        for (const auto &index : *indices) {
            *res << visitData(index);
        }
    } else {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        *res << visitData(rhsASTNode);
    }

    // If the expression is marked as "waited", wrap it in a WaitLoad node
    if (waited) {
        node_ptr_t waitNode = createNodeAs<WaitLoad>();
        *waitNode << res;
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
        *res << visitData(lhsASTNode);
        for (auto &argNode : *argsNode) {
            *res << visitData(argNode);
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
    } break;

    case AST::ReservedDataOp::Bind: {
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = createNodeAs<WithLoad>(argsNode->size());
        const auto &linkLoad   = res->loadAs<WithLoad>();
        *res << visitData(lhsASTNode);
        for (auto &argNode : *argsNode) {
            *res << visitData(argNode);
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
    } break;

    case AST::ReservedDataOp::Comp: {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t opNode      = createNodeAs<DRefLoad>("__cmp__");
        res                    = createNodeAs<LinkLoad>(2);
        *res << opNode << visitData(lhsASTNode) << visitData(rhsASTNode);
    } break;

    case AST::ReservedDataOp::As: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("AS Operator");
        throw BuildAbortException();
    } break;

    case AST::ReservedDataOp::Is: {
        diags_->of(SemanticDiag::FeatureNotSupported)
            .at(ast->load()->tokenRange())
            .commit("IS Operator");
        throw BuildAbortException();
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
        *res << visitData(lhsASTNode);
    } break;

    default:
        ASSERT(false, "Unknown reserved operation");
        res = nullptr;
    }

    // If the expression is "waited", wrap the result in a WaitLoad node
    if (waited) {
        node_ptr_t waitNode = createNodeAs<WaitLoad>();
        *waitNode << res; // Wait for the result of the reserved expression
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

    // condition
    // Process the condition expression and attach it to the branch node
    *brchNode << visitData(ast->atAs<AST::DataLoad>(0));

    // Process the "then" block and attach it as the "True" case
    const auto &thenBlock = ast->atAs<AST::StmtBlockLoad>(1);
    node_ptr_t thenNode   = visitStmtBlock(thenBlock);
    node_ptr_t trueCase   = createNodeAs<CaseLoad>(CaseLoad::CaseType::True);
    *trueCase << thenNode;
    *brchNode << trueCase;

    // Process the optional "else" block and attach it as the "Else" case
    const auto &elseNode = ast->optAtAs<AST::StmtBlockLoad>(2);
    node_ptr_t elseCase  = createNodeAs<CaseLoad>(CaseLoad::CaseType::Else);
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
    *brchNode << dRefNode;

    for (const auto &aCaseNode : *ast->atAs<AST::RepeatedLoad>(0)) {
        // Handle a value case
        const auto &caseLoadType = aCaseNode->front()->type();
        node_ptr_t gCaseNode     = nullptr;

        if (caseLoadType != AST::LoadType::Null) {
            gCaseNode               = createNodeAs<CaseLoad>(CaseLoad::CaseType::Value);
            node_ptr_t caseDataNode = visitData(aCaseNode->atAs<AST::DataLoad>(0));
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseDataNode << caseExprNode;
        } else {
            // Handle the "else" case
            gCaseNode               = createNodeAs<CaseLoad>(CaseLoad::CaseType::Else);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        }

        // Attach the case node to the branch node
        *brchNode << gCaseNode;
    }
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
        data = makeDataFromLiteral(parseNumber<int32_t>(str));
    } break;
    case LiteralType::Real: {
        // Parse the string as a floating-point number and create a data object
        data = makeDataFromLiteral(parseNumber<float>(str));
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
    // Ensure the AST node is of the expected type (Data)
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ArrayData");

    // Create an empty ArrayData object and wrap it in a DataLoad node
    auto arrayData = ArrayData::from(ArrayType::create(), {});
    node_ptr_t res = createNodeAs<DataLoad>(arrayData);

    bool dangling       = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        arrayData->emplace(data);
    }

    // If there are dangling nodes, attach them to the execution node
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

    // Create an empty StructData object and wrap it in a DataLoad node
    auto structData = std::make_shared<StructData>();
    node_ptr_t res  = createNodeAs<DataLoad>(structData);

    bool dangling       = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name    = namedPair->ref().ident();
        node_ptr_t dataNode   = visitData(child->atAs<AST::DataLoad>(0));
        auto [data, _]        = extractData(dataNode, execNode, dangling);
        structData->emplace(name, data);
    }

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

    // Create an empty TupleData object and wrap it in a DataLoad node
    auto tupleData = make_shared<TupleData>();
    node_ptr_t res = createNodeAs<DataLoad>(tupleData);

    bool dangling       = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _]      = extractData(dataNode, execNode, dangling);
        tupleData->emplace(data);
    }

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
    func_type_ptr_t funcType =
        tt::as_shared<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    node_ptr_t typeNode  = createNodeAs<TypeLoad>(funcType, funcType->implMark());
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    node_ptr_t funcNode  = createNodeAs<FuncLoad>(funcData->ref().ident());
    *funcNode << typeNode << stmtsNode;
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
type_ptr_t Builder::visitType(const AST::node_ptr_t &ast) {
    ENTER("Type");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for visitType");

    // Extract the type load from the AST node
    const auto &type = ast->loadAs<AST::TypeLoad>();
    type_ptr_t res;

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
type_ptr_t Builder::visitNullableType(const AST::node_ptr_t &ast) {
    ENTER("NullableType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for NullableType");
    type_ptr_t type = visitType(ast->atAs<AST::TypeLoad>(0));
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
type_ptr_t Builder::visitTypeExpr(const AST::node_ptr_t &ast) {
    ENTER("TypeExpr");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TypeExpr");

    type_ptr_t res;
    const auto &typeExpr = ast->loadAs<AST::TypeExprLoad>();
    AST::TypeOp op       = typeExpr->op();

    // Handle different type operations based on the TypeOp enumeration
    switch (op) {
    case AST::TypeOp::Union: {
        // Handle union types (e.g., T | U)
        const auto &lhsType = visitType(ast->atAs<AST::TypeLoad>(0));
        const auto &rhsType = visitType(ast->atAs<AST::TypeLoad>(1));
        res                 = make_shared<UnionType>(lhsType, rhsType);
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
type_ptr_t Builder::visitArrayType(const AST::node_ptr_t &ast) {
    ENTER("ArrayType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for ArrayType");
    // TODO: 这里的dims信息暂时没用到
    // const auto &arrayTypeLoad = ast->loadAs<AST::ArrayTypeLoad>();
    type_ptr_t type       = visitType(ast->atAs<AST::TypeLoad>(0));
    const auto &arrayType = ArrayType::create(type);
    LEAVE("ArrayType");
    return arrayType;
}

/*
StructType() : NamedType* types ;
*/
type_ptr_t Builder::visitStructType(const AST::node_ptr_t &ast) {
    ENTER("StructType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for StructType");

    // Create an empty StructType object
    auto res = make_shared<StructType>();

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedType = child->loadAs<AST::NamedTypeLoad>();
        const string &name    = namedType->getRef().ident();
        type_ptr_t type       = visitType(child->atAs<AST::TypeLoad>(0));

        // Attempt to add the field to the StructType object
        data_ptr_t data = nullptr;
        // If the field name is a duplicate, log a diagnostic and throw an exception
        if (!res->add(name, type)) {
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedType->tokenRange()).commit(name);
            throw BuildAbortException();
        }
    }
    LEAVE("StructType");
    return res;
}
/*
TupleType() : Type* types ;
*/
type_ptr_t Builder::visitTupleType(const AST::node_ptr_t &ast) {
    ENTER("TupleType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TupleType");

    // Create a vector to store the types of the tuple elements
    vector<type_ptr_t> types;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        type_ptr_t type = visitType(child);
        types.push_back(type);
    }
    // Create a TupleType object using the collected types
    type_ptr_t tupleType = TupleType::create(types);
    LEAVE("TupleType");
    return tupleType;
}

/*
FuncType(Modifier[] modifiers, ImplMark impl, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
type_ptr_t Builder::visitFuncType(const AST::node_ptr_t &ast) {
    ENTER("FuncType");
    // Ensure the AST node is of the expected type (Type)
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for FuncType");

    // Extract the function type load from the AST node
    auto const &typeLoad = ast->loadAs<AST::FuncTypeLoad>();

    // Initialize the exit type (return type) as nullptr
    type_ptr_t exitType = nullptr;

    // Process the optional exit type node
    const auto &exitTypeNode = ast->optAtAs<AST::TypeLoad>(2);
    if (exitTypeNode) {
        exitType = visitType(exitTypeNode);
    }

    // Create a FunctionType object and set its properties
    func_type_ptr_t funcType = make_shared<FunctionType>();
    funcType->setExitType(exitType);
    funcType->setImplMark(typeLoad->implMark());
    funcType->setModifiers(typeLoad->modifiers());

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
        type_ptr_t type    = visitType(paramPair->atAs<AST::TypeLoad>(0));
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

        // Add the parameter to the function type
        bool success = funcType->addWithArg(name, type, isVar);
        if (!success) {
            // Duplicate parameter names are not allowed
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
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
        type_ptr_t type    = visitType(paramPair->atAs<AST::TypeLoad>(0));
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

        // Add the parameter to the function type
        bool success = funcType->addNormArg(name, type, isVar);
        if (!success) {
            // Duplicate parameter names are not allowed
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
    }

    LEAVE("FuncType");
    return funcType;
}

/*
UnitType(Ref ref) : Type type ;
*/
type_ptr_t Builder::visitUnitType(const AST::node_ptr_t &ast) {
    ENTER("UnitType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("UnitType");
    throw BuildAbortException();
    LEAVE("UnitType");
    return nullptr;
}

/*
InferType(Ref ref) ;
*/
type_ptr_t Builder::visitInferType(const AST::node_ptr_t &ast) {
    ENTER("InferType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("InferType");
    throw BuildAbortException();
    LEAVE("InferType");
    return nullptr;
}

/*
DataType() : Data data ;
*/
type_ptr_t Builder::visitDataType(const AST::node_ptr_t &ast) {
    ENTER("DataType");
    diags_->of(SemanticDiag::FeatureNotSupported).at(ast->load()->tokenRange()).commit("DataType");
    throw BuildAbortException();
    LEAVE("DataType");
    return nullptr;
}

/*
RefType(Ref ref) ;
*/
type_ptr_t Builder::visitRefType(const AST::node_ptr_t &ast) {
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
} // namespace GraphConstructTree
