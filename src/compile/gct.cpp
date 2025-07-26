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
 * Updated: Jul. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "gct.h"
#include "common/type/init.h"
#include "utils/token.h"
#include "utils/type.h"

#define DEBUG_LEVEL 0

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

pair<node_ptr_t, data_ptr_t> Constructor::makeRefData(const node_ptr_t &expr) {
    const string id = std::to_string(idIndex_++);
    node_ptr_t refNode = createNodeAs<NRefLoad>(id);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(id);
    return make_pair(refNode, data);
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode) {
    const data_ptr_t data = extractStaticDataFromNode(node);
    if (data) {
        return make_pair(data, false);
    } else {
        auto [refNode, refData] = makeRefData(node);
        *execNode << refNode;
        return make_pair(refData, true);
    }
}

pair<data_ptr_t, bool> Constructor::extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling) {
    auto [refData, dang] = extractData(node, execNode);
    if (dang) {
        dangling = true;
    }
    return make_pair(refData, dang);
}

void Constructor::initInnerTypes() {
    typeScope_->clear();
    typeScope_->insert(Reference("int"), int32TypePtr);
    typeScope_->insert(Reference("float"), floatTypePtr);
    typeScope_->insert(Reference("bool"), boolTypePtr);
    typeScope_->insert(Reference("char"), charTypePtr);
    typeScope_->insert(Reference("string"), stringTypePtr);
    typeScope_->insert(Reference("any"), anyTypePtr);
    typeScope_->insert(Reference("void"), voidTypePtr);
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
node_ptr_t Constructor::visitModule(const AST::node_ptr_t &ast) {
    ENTER("Module");
    ASSERT(ast->type() == AST::LoadType::Module, "Expected ModuleLoad type");
    auto importNodes = ast->atAs<AST::RepeatedLoad>(0);
    auto exportOptNode = ast->atAs<AST::OptionalLoad>(1);
    auto stmtNodes = ast->atAs<AST::RepeatedLoad>(2);

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
                const auto &funcLoad = node->loadAs<FuncLoad>();
                node_ptr_t declNode = createNodeAs<DeclLoad>(funcLoad->name(), true);
                node_ptr_t typeNode = node->atAs<TypeLoad>(0);
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
    if (!decls.empty()) {
        for (const auto &decl : decls) {
            *root_ << decl;
        }
    }
    for (const auto &stmt : stmts) {
        *root_ << stmt;
    }

    if (!exportOptNode->empty()) {
        visitExport(exportOptNode->front());
    }

    LEAVE("Module");
    return root_;
}

/*
ImportDecl(string path, Ref[] refs, Ref as) ;
*/
void_ptr_t Constructor::visitImport(const AST::node_ptr_t &ast) { return nullptr; }

/*
ExportDecl(Ref[] refs) ;
*/
void_ptr_t Constructor::visitExport(const AST::node_ptr_t &ast) { return nullptr; }

node_ptr_t Constructor::visitStmt(const AST::node_ptr_t &ast) {
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
    if (str.length() < 4) {
        return true;
    }
    return !(str.substr(0, 2) == "__" && str.substr(str.length() - 2) == "__");
}

/*
DataDecl(bool isVar, UnpackType type, Ref[] refs) : Type* type, Data* value;
*/
node_ptr_t Constructor::visitDataDecl(const AST::node_ptr_t &ast) {
    ENTER("DataDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for DataDecl");
    const auto &dataDeclLoad = ast->loadAs<AST::DataDeclLoad>();
    bool isVar = dataDeclLoad->isVar();
    const auto &refs = dataDeclLoad->refs();
    const auto &typeNodes = ast->atAs<AST::RepeatedLoad>(0);
    const auto &dataNodes = ast->atAs<AST::RepeatedLoad>(1);

    node_ptr_t res = createNodeAs<ExecLoad>();

    switch (dataDeclLoad->unpackType()) {
    case AST::UnpackType::Dict: {
        reportDiagnostic(Diagnostic::Severity::Error, "Dict unpacking is not supported in DataDecl",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::UnpackType::List: {
        reportDiagnostic(Diagnostic::Severity::Error, "List unpacking is not supported in DataDecl",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::UnpackType::Tuple: {
        if (refs.size() == dataNodes->size()) {
            for (size_t i = 0; i < refs.size(); ++i) {
                const auto &ref = refs[i];
                const auto &dataNode = dataNodes->atAs<AST::DataLoad>(i);
                if (!validateIdent(ref.ident())) {
                    reportDiagnostic(Diagnostic::Severity::Error,
                                     "Identifiers starting and ending with '__' are reserved for internal use.",
                                     dataDeclLoad->tokenRange());
                    throw BuildAbortException();
                }
                if (dataNode->type() != AST::LoadType::Data) {
                    reportDiagnostic(Diagnostic::Severity::Error, "Tuple unpacking requires Data type nodes",
                                     dataNode->load()->tokenRange());
                    throw BuildAbortException();
                }
                node_ptr_t nRefNode = createNodeAs<NRefLoad>(ref.ident());
                *nRefNode << visitData(dataNode);
                res = nRefNode;
            }
        } else {
            if (dataNodes->size() == 1) {
                const auto &dataASTNode = dataNodes->atAs<AST::DataLoad>(0);
                node_ptr_t dataNode = visitData(dataASTNode);
                const string id = std::to_string(idIndex_++);
                node_ptr_t dataRefNode = createNodeAs<NRefLoad>(id);
                node_ptr_t dRefNode = createNodeAs<DRefLoad>(id);
                *dataRefNode << dataNode;
                *res << dataRefNode;
                for (size_t i = 0; i < refs.size(); ++i) {
                    const auto &ref = refs[i];
                    if (!validateIdent(ref.ident())) {
                        reportDiagnostic(Diagnostic::Severity::Error,
                                         "Identifiers starting and ending with '__' are reserved for internal use.",
                                         dataDeclLoad->tokenRange());
                        throw BuildAbortException();
                    }
                    node_ptr_t nRefNode = createNodeAs<NRefLoad>(ref.ident());
                    node_ptr_t accsNode = createNodeAs<AccsLoad>(i);
                    *accsNode << dRefNode->clone();
                    *nRefNode << accsNode;
                    *res << nRefNode;
                }
            } else {
                reportDiagnostic(
                    Diagnostic::Severity::Error,
                    "Tuple unpacking requires the same number of references and data nodes, or exactly one tuple data",
                    ast->load()->tokenRange());
                throw BuildAbortException();
            }
        }
    } break;
    default:
        ASSERT(false, "Unknown unpack type in DataDecl");
    }

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
node_ptr_t Constructor::visitFuncDecl(const AST::node_ptr_t &ast) {
    ENTER("FuncDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for FuncDecl");
    const auto &funcDataNode = ast->atAs<AST::FuncDataLoad>(0);
    const auto &funcLoad = ast->loadAs<AST::FuncDeclLoad>();
    node_ptr_t funcNode = visitFuncData(funcDataNode);
    LEAVE("FuncDecl");
    return funcNode;
}

/*
TypeDecl(Ref ref, ImplMark impl, string uri) : Type? type ;
*/
node_ptr_t Constructor::visitTypeDecl(const AST::node_ptr_t &ast) {
    ENTER("TypeDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for TypeDecl");
    const auto &typeNode = ast->optAtAs<AST::TypeLoad>(0);
    type_ptr_t type;
    if (typeNode) {
        type = visitType(typeNode);
    } else {
        reportDiagnostic(Diagnostic::Severity::Error, "Type declaration requires a type", ast->load()->tokenRange());
        throw BuildAbortException();
    }
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
node_ptr_t Constructor::visitNameDecl(const AST::node_ptr_t &ast) {
    ENTER("NameDecl");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for NameDecl");
    reportDiagnostic(Diagnostic::Severity::Error,
                     "NameDecl is not supported in the current version of the compiler. "
                     "Please use DataDecl or TypeDecl instead.",
                     ast->load()->tokenRange());
    throw BuildAbortException();
    LEAVE("NameDecl");
    return nullptr;
}

/*
ExprStmt() : Data data ;
*/
node_ptr_t Constructor::visitExprStmt(const AST::node_ptr_t &ast) {
    ENTER("ExprStmt");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExprStmt");
    node_ptr_t exprNode = visitData(ast->atAs<AST::DataLoad>(0));
    LEAVE("ExprStmt");
    return exprNode;
}

/*
ExitStmt(ExitType type) : Data* data ;
*/
node_ptr_t Constructor::visitExitStmt(const AST::node_ptr_t &ast) {
    ENTER("ExitStmt");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExitStmt");
    node_ptr_t exitNode = createNodeAs<ExitLoad>();
    const auto &dataNodes = ast->atAs<AST::RepeatedLoad>(0);
    if (dataNodes->size() > 0) {
        if (dataNodes->size() == 1) {
            const auto &dataNode = dataNodes->front();
            node_ptr_t d = visitData(dataNode);
            *exitNode << visitData(dataNode);
        } else {
            auto tupleData = make_shared<TupleData>();
            node_ptr_t dataNode = createNodeAs<DataLoad>(tupleData);
            bool dangling = false;
            node_ptr_t execNode = createNodeAs<ExecLoad>();
            for (const auto &item : *dataNodes) {
                node_ptr_t dataNode = visitData(item);
                auto [data, _] = extractData(dataNode, execNode, dangling);
                tupleData->emplace(data);
            }
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
node_ptr_t Constructor::visitStmtBlock(const AST::node_ptr_t &ast) {
    ENTER("StmtBlock");
    ASSERT(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for StmtBlock");
    const auto &stmtBlock = ast->loadAs<AST::StmtBlockLoad>();
    node_ptr_t execNode = createNodeAs<ExecLoad>(stmtBlock->synced());

    pushScope();
    for (const auto &stmt : *ast->atAs<AST::RepeatedLoad>(0)) {
        ASSERT(stmt->type() == AST::LoadType::Stmt, "Expected a statement in StmtBlock");
        *execNode << visitStmt(stmt);
    }
    popScope();

    LEAVE("StmtBlock");
    return execNode;
}

node_ptr_t Constructor::visitData(const AST::node_ptr_t &ast) {
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
    case AST::DataType::List:
        dataNode = visitListData(ast);
        break;
    case AST::DataType::Dict:
        dataNode = visitDictData(ast);
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
node_ptr_t Constructor::visitUnaryExpr(const AST::node_ptr_t &ast) {
    ENTER("UnaryExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for UnaryExpr");
    const auto &unaryExpr = ast->loadAs<AST::UnaryExprLoad>();
    const auto &dataASTNode = ast->atAs<AST::DataLoad>(0);
    node_ptr_t opNode;
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
        ASSERT(false, "Unknown unary operation");
        return nullptr;
    }
    node_ptr_t linkNode = createNodeAs<LinkLoad>(1);
    *linkNode << opNode << visitData(dataASTNode);
    LEAVE("UnaryExpr");
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
    Exp,
    Index, // Data obj, Data* indices
}
BinaryExpr(BinaryDataOp op) := Data lhs, Data rhs ;
*/
node_ptr_t Constructor::visitBinaryExpr(const AST::node_ptr_t &ast) {
    ENTER("BinaryExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for BinaryExpr");
    const auto &binaryExpr = ast->loadAs<AST::BinaryExprLoad>();
    const auto &lhsASTNode = ast->atAs<AST::DataLoad>(0);
    node_ptr_t opNode;
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
        opNode = createNodeAs<DRefLoad>("__assn_exp__");
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
    case AST::BinaryDataOp::Exp: {
        opNode = createNodeAs<DRefLoad>("__exp__");
    } break;
    case AST::BinaryDataOp::Index: {
        opNode = createNodeAs<DRefLoad>("__idx__");
    } break;
    default:
        ASSERT(false, "Unknown binary operation");
        return nullptr;
    }
    node_ptr_t linkNode = createNodeAs<LinkLoad>(2);
    *linkNode << opNode << visitData(lhsASTNode);
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        auto indices = ast->atAs<AST::RepeatedLoad>(1);
        for (const auto &index : *indices) {
            *linkNode << visitData(index);
        }
    } else {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        *linkNode << visitData(rhsASTNode);
    }
    LEAVE("BinaryExpr");
    return linkNode;
}

/*
enum ReservedDataOp {
    NullThen,
    NullThen,
    ErrorThen,
    NotNullThen,
    Call, // Data obj, Data* args, NamedData* kwargs
    With, // Data obj, Data* args, NamedData* kwargs
    Bind, // Data lhs, Data rhs
    As, // Data lhs, Type rhs
    Is, // Data lhs, Type rhs
    Access, // Data obj, RefData ref
}
ReservedExpr(ReservedDataOp op) := Data lhs, Any? rhs ;
*/
node_ptr_t Constructor::visitReservedExpr(const AST::node_ptr_t &ast) {
    ENTER("ReservedExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ReservedExpr");
    node_ptr_t res;
    const auto &reservedExpr = ast->loadAs<AST::ReservedExprLoad>();
    bool waited = reservedExpr->waited();
    const auto &lhsASTNode = ast->atAs<AST::DataLoad>(0);
    switch (reservedExpr->op()) {
    case AST::ReservedDataOp::NullThen: {
        reportDiagnostic(Diagnostic::Severity::Warning, "NullThen is not supported yet", ast->load()->tokenRange());
        res = visitData(lhsASTNode);
    } break;
    case AST::ReservedDataOp::ErrorThen: {
        reportDiagnostic(Diagnostic::Severity::Warning, "ErrorThen is not supported yet", ast->load()->tokenRange());
        res = visitData(lhsASTNode);
    } break;
    case AST::ReservedDataOp::NotNullThen: {
        reportDiagnostic(Diagnostic::Severity::Warning, "NotNullThen is not supported yet", ast->load()->tokenRange());
        res = visitData(lhsASTNode);
    } break;

    case AST::ReservedDataOp::Call: {
        const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res = createNodeAs<LinkLoad>(argsNode->size());
        const auto &linkLoad = res->loadAs<LinkLoad>();
        *res << visitData(lhsASTNode);
        for (auto &argNode : *argsNode) {
            *res << visitData(argNode);
        }
        for (const auto &kwargNode : *kwargsNode) {
            const auto &namedData = kwargNode->loadAs<AST::NamedDataLoad>();
            const auto &dataNode = kwargNode->atAs<AST::DataLoad>(0);
            const auto &kwargName = namedData->ref().ident();
            linkLoad->addKwarg(kwargName);
            *res << visitData(dataNode);
        }
    } break;
    case AST::ReservedDataOp::With: {
        const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res = createNodeAs<WithLoad>(argsNode->size());
        const auto &linkLoad = res->loadAs<WithLoad>();
        *res << visitData(lhsASTNode);
        for (auto &argNode : *argsNode) {
            *res << visitData(argNode);
        }
        for (const auto &kwargNode : *kwargsNode) {
            const auto &namedData = kwargNode->loadAs<AST::NamedDataLoad>();
            const auto &dataNode = kwargNode->atAs<AST::DataLoad>(0);
            const auto &kwargName = namedData->ref().ident();
            linkLoad->addKwarg(kwargName);
            *res << visitData(dataNode);
        }
    } break;
    case AST::ReservedDataOp::Bind: {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        res = createNodeAs<BindLoad>();
        *res << visitData(lhsASTNode) << visitData(rhsASTNode);
    } break;

    case AST::ReservedDataOp::As: {
        reportDiagnostic(Diagnostic::Severity::Error, "As is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::ReservedDataOp::Is: {
        reportDiagnostic(Diagnostic::Severity::Error, "Is is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;

    case AST::ReservedDataOp::Access: {
        const auto &refASTNode = ast->atAs<AST::RefDataLoad>(1);
        const auto &refDataLoad = refASTNode->loadAs<AST::RefDataLoad>();
        res = createNodeAs<AccsLoad>(refDataLoad->ref());
        *res << visitData(lhsASTNode);
    } break;
    default:
        ASSERT(false, "Unknown reserved operation");
        res = nullptr;
    }
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
node_ptr_t Constructor::visitIfExpr(const AST::node_ptr_t &ast) {
    ENTER("IfExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for IfExpr");
    node_ptr_t brchNode = createNodeAs<BrchLoad>();
    *brchNode << visitData(ast->atAs<AST::DataLoad>(0)); // condition
    const auto &thenBlock = ast->atAs<AST::StmtBlockLoad>(1);
    node_ptr_t thenNode = visitStmtBlock(thenBlock);
    *brchNode << thenNode; // then block
    const auto &elseNode = ast->optAtAs<AST::StmtBlockLoad>(2);
    if (elseNode) {
        node_ptr_t elseBlock = visitStmtBlock(elseNode);
        *brchNode << elseBlock; // else block
    } else {
        *brchNode << createNodeAs<ExecLoad>(); // empty else block
    }
    LEAVE("IfExpr");
    return brchNode;
}

/*

*/
node_ptr_t Constructor::visitMatchExpr(const AST::node_ptr_t &ast) {
    ENTER("MatchExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchExpr");
    reportDiagnostic(Diagnostic::Severity::Error, "MatchExpr is not supported yet", ast->load()->tokenRange());
    throw BuildAbortException();
    LEAVE("MatchExpr");
    return nullptr;
}

/*

*/
node_ptr_t Constructor::visitTryExpr(const AST::node_ptr_t &ast) {
    ENTER("TryExpr");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for TryExpr");
    reportDiagnostic(Diagnostic::Severity::Error, "TryExpr is not supported yet", ast->load()->tokenRange());
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
node_ptr_t Constructor::visitLiteral(const AST::node_ptr_t &ast) {
    ENTER("Literal");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for Literal");
    const auto &literal = ast->loadAs<AST::LiteralLoad>();
    const Literal &value = literal->value();
    const auto &str = value.data();
    data_ptr_t data;
    switch (value.type()) {
    case LiteralType::String: {
        data = tt::as_shared<Data>(make_shared<StringData>(str));
    } break;
    case LiteralType::FString: {
        reportDiagnostic(Diagnostic::Severity::Error, "FString is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case LiteralType::Integer: {
        data = makeDataFromLiteral(parseNumber<int64_t>(str));
    } break;
    case LiteralType::Real: {
        data = makeDataFromLiteral(parseNumber<double>(str));
    } break;
    case LiteralType::Boolean: {
        if (str == "true") {
            data = makeDataFromLiteral(true);
        } else if (str == "false") {
            data = makeDataFromLiteral(false);
        } else {
            reportDiagnostic(Diagnostic::Severity::Error, "Invalid boolean literal: " + str, ast->load()->tokenRange());
            throw BuildAbortException();
        }
    } break;
    case LiteralType::Null: {
        data = tt::as_shared<Data>(make_shared<NullData>());
    } break;
    default: {
        reportDiagnostic(Diagnostic::Severity::Error, "Unknown literal type", ast->load()->tokenRange());
        throw BuildAbortException();
    }
    }
    node_ptr_t res = createNodeAs<DataLoad>(data);
    LEAVE("Literal");
    return res;
}

/*
ListData() : Data* data ;
*/
node_ptr_t Constructor::visitListData(const AST::node_ptr_t &ast) {
    ENTER("ListData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ListData");
    auto listData = make_shared<ListData>();
    node_ptr_t res = createNodeAs<DataLoad>(listData);
    bool dangling = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _] = extractData(dataNode, execNode, dangling);
        listData->emplace(data);
    }
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("ListData");
    return res;
}

/*
DictData() : NamedData* dataList ;
*/
node_ptr_t Constructor::visitDictData(const AST::node_ptr_t &ast) {
    ENTER("DictData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for DictData");
    auto dictData = make_shared<DictData>();
    node_ptr_t res = createNodeAs<DataLoad>(dictData);
    bool dangling = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name = namedPair->ref().ident();
        node_ptr_t dataNode = visitData(child->atAs<AST::DataLoad>(0));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        dictData->emplace(name, data);
    }
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("DictData");
    return res;
}

/*
TupleData() : Data* data ;
*/
node_ptr_t Constructor::visitTupleData(const AST::node_ptr_t &ast) {
    ENTER("TupleData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for TupleData");
    auto tupleData = make_shared<TupleData>();
    node_ptr_t res = createNodeAs<DataLoad>(tupleData);
    bool dangling = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        auto [data, _] = extractData(dataNode, execNode, dangling);
        tupleData->emplace(data);
    }
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
node_ptr_t Constructor::visitFuncData(const AST::node_ptr_t &ast) {
    ENTER("FuncData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for FuncData");
    const auto &funcData = ast->loadAs<AST::FuncDataLoad>();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    node_ptr_t typeNode = createNodeAs<TypeLoad>(funcType, funcType->implMark(), funcType->uri());
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    node_ptr_t funcNode = createNodeAs<FuncLoad>(funcData->ref().ident());
    *funcNode << typeNode << stmtsNode;
    LEAVE("FuncData");
    return funcNode;
}

/*
RefData(Ref ref) ;
*/
node_ptr_t Constructor::visitRefData(const AST::node_ptr_t &ast) {
    ENTER("RefData");
    ASSERT(ast->type() == AST::LoadType::Data, "Expected DataLoad type for RefData");
    const auto &refData = ast->loadAs<AST::RefDataLoad>();
    node_ptr_t refNode = createNodeAs<DRefLoad>(refData->ref());
    LEAVE("RefData");
    return refNode;
}

/*
Type(TypeType type) :=
    NullableType | TypeExpr | ListType | DictType | TupleType
    | FuncType | SpecType | UnitType | InferType | DataType | RefType ;
*/
type_ptr_t Constructor::visitType(const AST::node_ptr_t &ast) {
    ENTER("Type");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for visitType");
    const auto &type = ast->loadAs<AST::TypeLoad>();
    type_ptr_t res;

    switch (type->typeType()) {
    case AST::TypeType::Null:
        res = visitNullableType(ast);
        break;
    case AST::TypeType::Expr:
        res = visitTypeExpr(ast);
        break;
    case AST::TypeType::List:
        res = visitListType(ast);
        break;
    case AST::TypeType::Dict:
        res = visitDictType(ast);
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
type_ptr_t Constructor::visitNullableType(const AST::node_ptr_t &ast) {
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
type_ptr_t Constructor::visitTypeExpr(const AST::node_ptr_t &ast) {
    ENTER("TypeExpr");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TypeExpr");
    type_ptr_t res;
    const auto &typeExpr = ast->loadAs<AST::TypeExprLoad>();
    AST::TypeOp op = typeExpr->op();
    switch (op) {
    case AST::TypeOp::Union: {
        const auto &lhsType = visitType(ast->atAs<AST::TypeLoad>(0));
        const auto &rhsType = visitType(ast->atAs<AST::TypeLoad>(1));
        res = make_shared<UnionType>(lhsType, rhsType);
    } break;
    case AST::TypeOp::Inter: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::Inter is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::Diff: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::Diff is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyUnion: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::KeyUnion is not supported yet",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyInter: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::KeyInter is not supported yet",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyDiff: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::KeyDiff is not supported yet",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::ErrorThen: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::ErrorThen is not supported yet",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::Specialize: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::Specialize is not supported yet",
                         ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::TypeOf: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::TypeOf is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    case AST::TypeOp::TypeAs: {
        reportDiagnostic(Diagnostic::Severity::Error, "TypeOp::TypeAs is not supported yet", ast->load()->tokenRange());
        throw BuildAbortException();
    } break;
    default:
        reportDiagnostic(Diagnostic::Severity::Error, "Unsupported TypeOp: " + AST::to_string(op),
                         ast->load()->tokenRange());
        throw BuildAbortException();
    }
    LEAVE("TypeExpr");
    return res;
}

/*
ListType(siz dim) : Type type ;
*/
type_ptr_t Constructor::visitListType(const AST::node_ptr_t &ast) {
    ENTER("ListType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for ListType");
    const auto &listTypeLoad = ast->loadAs<AST::ListTypeLoad>();
    type_ptr_t type = visitType(ast->atAs<AST::TypeLoad>(0));
    const auto &arrayType = make_shared<ArrayType>(type, listTypeLoad->dims());
    LEAVE("ListType");
    return arrayType;
}

/*
DictType() : NamedType* types ;
*/
type_ptr_t Constructor::visitDictType(const AST::node_ptr_t &ast) {
    ENTER("DictType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for DictType");
    auto res = make_shared<DictType>();
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedPairLoad>();
        const string &name = namedPair->getRef().ident();
        type_ptr_t type = visitType(child->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        if (!res->add(name, type)) {
            reportDiagnostic(Diagnostic::Severity::Error, "Duplicate key detected: " + name, namedPair->tokenRange());
            throw BuildAbortException();
        }
    }
    LEAVE("DictType");
    return res;
}
/*
TupleType() : Type* types ;
*/
type_ptr_t Constructor::visitTupleType(const AST::node_ptr_t &ast) {
    ENTER("TupleType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TupleType");
    vector<type_ptr_t> types;
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        type_ptr_t type = visitType(child);
        types.push_back(type);
    }
    type_ptr_t tupleType = make_shared<TupleType>(types);
    LEAVE("TupleType");
    return tupleType;
}

/*
FuncType(Modifier[] modifiers, ImplMark impl, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
type_ptr_t Constructor::visitFuncType(const AST::node_ptr_t &ast) {
    ENTER("FuncType");
    ASSERT(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for FuncType");
    auto const &typeLoad = ast->loadAs<AST::FuncTypeLoad>();

    const auto withParamsType = make_shared<ParamsType>();
    const auto normParamsType = make_shared<ParamsType>();
    type_ptr_t exitType = voidTypePtr;

    const auto &exitTypeNode = ast->optAtAs<AST::TypeLoad>(2);
    if (exitTypeNode) {
        exitType = visitType(exitTypeNode);
    }

    func_type_ptr_t funcType = make_shared<FunctionType>(withParamsType, normParamsType, exitType);
    funcType->setImplMark(typeLoad->implMark());
    funcType->setModifiers(typeLoad->modifiers());

    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        bool isVar = paramLoad->isVar();
        if (isVar && !typeLoad->modifiers().sync()) {
            reportDiagnostic(Diagnostic::Severity::Warning, "Variable parameters are only allowed in sync functions",
                             paramLoad->tokenRange());
        }
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.isAlone()) {
            reportDiagnostic(Diagnostic::Severity::Error, "Parameter reference must be alone: " + paramRef.toString(),
                             paramLoad->tokenRange());
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        type_ptr_t type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                reportDiagnostic(Diagnostic::Severity::Error,
                                 "Data for parameter " + paramRef.toString() + " is not static",
                                 dataNode->load()->tokenRange());
                throw BuildAbortException();
            }
        }
        bool success = funcType->addIdent(name, isVar);
        if (!success) {
            reportDiagnostic(Diagnostic::Severity::Error, "Duplicate parameter detected: " + name,
                             paramLoad->tokenRange());
            throw BuildAbortException();
        }
        withParamsType->add(name, type, data);
    }

    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(1)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        bool isVar = paramLoad->isVar();
        if (isVar && !typeLoad->modifiers().sync()) {
            reportDiagnostic(Diagnostic::Severity::Warning, "Variable parameters are only allowed in sync functions",
                             paramLoad->tokenRange());
        }
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.isAlone()) {
            reportDiagnostic(Diagnostic::Severity::Error, "Parameter reference must be alone: " + paramRef.toString(),
                             paramLoad->tokenRange());
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        type_ptr_t type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                reportDiagnostic(Diagnostic::Severity::Error,
                                 "Data for parameter " + paramRef.toString() + " is not static",
                                 dataNode->load()->tokenRange());
                throw BuildAbortException();
            }
        }
        bool success = funcType->addIdent(name, isVar);
        if (!success) {
            reportDiagnostic(Diagnostic::Severity::Error, "Duplicate parameter detected: " + name,
                             paramLoad->tokenRange());
            throw BuildAbortException();
        }
        normParamsType->add(name, type, data);
    }

    LEAVE("FuncType");
    return funcType;
}

/*
UnitType(Ref ref) : Type type ;
*/
type_ptr_t Constructor::visitUnitType(const AST::node_ptr_t &ast) {
    ENTER("UnitType");
    reportDiagnostic(Diagnostic::Severity::Error, "UnitType is not supported yet", ast->load()->tokenRange());
    throw BuildAbortException();
    LEAVE("UnitType");
    return nullptr;
}

/*
InferType(Ref ref) ;
*/
type_ptr_t Constructor::visitInferType(const AST::node_ptr_t &ast) {
    ENTER("InferType");
    reportDiagnostic(Diagnostic::Severity::Error, "InferType is not supported yet", ast->load()->tokenRange());
    throw BuildAbortException();
    LEAVE("InferType");
    return nullptr;
}

/*
DataType() : Data data ;
*/
type_ptr_t Constructor::visitDataType(const AST::node_ptr_t &ast) {
    ENTER("DataType");
    reportDiagnostic(Diagnostic::Severity::Error, "DataType is not supported yet", ast->load()->tokenRange());
    throw BuildAbortException();
    LEAVE("DataType");
    return nullptr;
}

/*
RefType(Ref ref) ;
*/
type_ptr_t Constructor::visitRefType(const AST::node_ptr_t &ast) {
    ENTER("RefType");
    ASSERT(ast->load()->type() == AST::LoadType::Type, "Expected TypeLoad type for RefType");
    auto const &typeLoad = ast->loadAs<AST::RefTypeLoad>();
    const Reference &ref = typeLoad->ref();
    const auto &type = typeScope_->get(ref);
    if (!type.has_value()) {
        reportDiagnostic(Diagnostic::Severity::Error, "Unresolved type reference: " + ref.toString(),
                         ast->load()->tokenRange());
        throw BuildAbortException();
    }
    LEAVE("RefType");
    return type.value();
}
} // namespace GraphConstructTree