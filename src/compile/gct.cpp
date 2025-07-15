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
#include "utils/type.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace GCT;

namespace GraphConstructTree {

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNodeAs<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}

data_ptr_t extractStaticDataFromNode(const node_ptr_t &node) {
    if (node->type() == NodeType::DATA) {
        return node->loadAs<DataLoad>()->data();
    } else if (node->type() == NodeType::DREF) {
        return make_shared<RefData>(node->loadAs<DRefLoad>()->ident());
    } else {
        return nullptr;
    }
}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
node_ptr_t Constructor::visitModule(const AST::node_ptr_t &ast) {
    enter("Module");
    assert(ast->type() == AST::LoadType::Module);
    auto importNodes = ast->atAs<AST::RepeatedLoad>(0);
    auto exportOptNode = ast->atAs<AST::OptionalLoad>(1);
    auto stmtNodes = ast->atAs<AST::RepeatedLoad>(2);

    for (const auto &import : *importNodes) {
        visitImport(import);
    }

    root_ = createNodeAs<ExecLoad>();
    for (auto &stmt : *stmtNodes) {
        try {
            *root_ << visitStmt(stmt);
        } catch (const BuildAbortException &) {
            continue;
        }
    }

    if (!exportOptNode->empty()) {
        visitExport(exportOptNode->front());
    }

    leave("Module");
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

/*
NamedData(Ref ref) : Data data ;
*/
node_ptr_t Constructor::visitNamedData(const AST::node_ptr_t &ast) {}

/*
NamedType(Ref ref, bool isVar) : Type type ;
*/
node_ptr_t Constructor::visitNamedType(const AST::node_ptr_t &ast) {}

/*
NamedPair(Ref ref, bool isVar) : Type type, Data? data ;
*/
node_ptr_t Constructor::visitNamedPair(const AST::node_ptr_t &ast) {}

node_ptr_t Constructor::visitStmt(const AST::node_ptr_t &ast) {
    enter("Stmt");
    assert(ast->type() == AST::LoadType::Stmt);
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

    leave("Stmt");
    return stmtNode;
}

/*
DataDecl(bool isVar, UnpackType type, Ref[] refs) : Type* type, Data* value;
*/
node_ptr_t Constructor::visitDataDecl(const AST::node_ptr_t &ast) {}

/*
FuncDecl(Ref ref) : FuncData ;
*/
node_ptr_t Constructor::visitFuncDecl(const AST::node_ptr_t &ast) {}

/*
TypeDecl(Ref ref, ImplMark impl, string uri) : Type? type ;
*/
node_ptr_t Constructor::visitTypeDecl(const AST::node_ptr_t &ast) {}

/*
NameDecl(Ref ref, Ref alias) ;
*/
node_ptr_t Constructor::visitNameDecl(const AST::node_ptr_t &ast) {}

/*

*/
node_ptr_t Constructor::visitExprStmt(const AST::node_ptr_t &ast) {}

/*

*/
node_ptr_t Constructor::visitExitStmt(const AST::node_ptr_t &ast) {}

/*
StmtBlock(bool sync) : Stmt* stmts ;
*/
node_ptr_t Constructor::visitStmtBlock(const AST::node_ptr_t &ast) {}

node_ptr_t Constructor::visitData(const AST::node_ptr_t &ast) {
    enter("Data");
    assert(ast->type() == AST::LoadType::Data);
    const auto &data = ast->loadAs<AST::DataLoad>();
    node_ptr_t dataNode;

    switch (data->dataType()) {
    case AST::DataType::DataExpr:
        dataNode = visitDataExpr(ast);
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
    case AST::DataType::Index:
        dataNode = visitIndexData(ast);
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

    leave("Data");
    return dataNode;
}

/*
DataExpr(DataOp op) := Any... lhs, rhs... ;
*/
node_ptr_t Constructor::visitDataExpr(const AST::node_ptr_t &ast) {}

/*
IfExpr() : Data cond, StmtBlock then, StmtBlock? else ;
*/
node_ptr_t Constructor::visitIfExpr(const AST::node_ptr_t &ast) {}

/*

*/
node_ptr_t Constructor::visitMatchExpr(const AST::node_ptr_t &ast) {}

/*

*/
node_ptr_t Constructor::visitTryExpr(const AST::node_ptr_t &ast) {}

/*
Literal(Value value) ;
*/
node_ptr_t Constructor::visitLiteral(const AST::node_ptr_t &ast) {}

/*
ListData() : Data* data ;
*/
node_ptr_t Constructor::visitListData(const AST::node_ptr_t &ast) {}

/*
DictData() : NamedPair* pairs ;
*/
node_ptr_t Constructor::visitDictData(const AST::node_ptr_t &ast) {}

/*
TupleData() : Data* data ;
*/
node_ptr_t Constructor::visitTupleData(const AST::node_ptr_t &ast) {}

/*

*/
node_ptr_t Constructor::visitIndexData(const AST::node_ptr_t &ast) {}

/*
FuncData(Ref ref) : FuncType funcType, StmtBlock body ;
*/
node_ptr_t Constructor::visitFuncData(const AST::node_ptr_t &ast) {}

/*
RefData(Ref ref) ;
*/
node_ptr_t Constructor::visitRefData(const AST::node_ptr_t &ast) {}

type_ptr_t Constructor::visitType(const AST::node_ptr_t &ast) {
    enter("Type");
    assert(ast->type() == AST::LoadType::Type);
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

    leave("Type");
    return res;
}

/*

*/
type_ptr_t Constructor::visitNullableType(const AST::node_ptr_t &ast) {}

/*
TypeExpr(TypeOp op) := Type lhs, Type rhs ;
*/
type_ptr_t Constructor::visitTypeExpr(const AST::node_ptr_t &ast) {}

/*
ListType(siz dim) : Type type ;
*/
type_ptr_t Constructor::visitListType(const AST::node_ptr_t &ast) {}

/*
DictType() : NamedType* types ;
*/
type_ptr_t Constructor::visitDictType(const AST::node_ptr_t &ast) {}

/*
TupleType() : Type* types ;
*/
type_ptr_t Constructor::visitTupleType(const AST::node_ptr_t &ast) {}

/*
FuncType(Modifier[] modifiers, ImplMark impl, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
type_ptr_t Constructor::visitFuncType(const AST::node_ptr_t &ast) {
    enter("FuncType");
    assert(ast->type() == AST::LoadType::Type);
    auto const &typeLoad = ast->loadAs<AST::FuncTypeLoad>();

    const auto withParamsType = make_shared<ParamsType>();
    const auto normParamsType = make_shared<ParamsType>();
    type_ptr_t exitType = voidTypePtr;

    const auto &exitTypeLoad = ast->optAtAs<AST::OptionalLoad>(2);
    if (exitTypeLoad) {
        exitType = visitTypeExpr(exitTypeLoad);
    }

    func_type_ptr_t funcType = make_shared<FunctionType>("", withParamsType, normParamsType, exitType);

    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.isAlone()) {
            reportDiagnostic("Parameter reference must be alone: " + paramRef.toString(), paramLoad->tokenRange(),
                             Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        type_ptr_t type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                reportDiagnostic("Data for parameter " + paramRef.toString() + " is not static",
                                 dataNode->load()->tokenRange(), Diagnostic::Severity::Error);
                throw BuildAbortException();
            }
        }
        bool success = funcType->addIdent(name, paramLoad->isVar());
        if (!success) {
            reportDiagnostic("Duplicate parameter detected: " + name, paramLoad->tokenRange(),
                             Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
        withParamsType->add(name, type, data);
    }

    for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(1)) {
        const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
        const Reference &paramRef = paramLoad->getRef();
        if (!paramRef.isAlone()) {
            reportDiagnostic("Parameter reference must be alone: " + paramRef.toString(), paramLoad->tokenRange(),
                             Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
        const string &name = paramRef.ident();
        type_ptr_t type = visitType(paramPair->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        const auto &dataNode = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            data = extractStaticDataFromNode(visitData(dataNode));
            if (!data) {
                reportDiagnostic("Data for parameter " + paramRef.toString() + " is not static",
                                 dataNode->load()->tokenRange(), Diagnostic::Severity::Error);
                throw BuildAbortException();
            }
        }
        bool success = funcType->addIdent(name, paramLoad->isVar());
        if (!success) {
            reportDiagnostic("Duplicate parameter detected: " + name, paramLoad->tokenRange(),
                             Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
        normParamsType->add(name, type, data);
    }

    leave("FuncType");
    return funcType;
}

/*
UnitType(Ref ref) : Type type ;
*/
type_ptr_t Constructor::visitUnitType(const AST::node_ptr_t &ast) {
    enter("UnitType");
    reportDiagnostic("UnitType is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("UnitType");
    return nullptr;
}

/*
InferType(Ref ref) ;
*/
type_ptr_t Constructor::visitInferType(const AST::node_ptr_t &ast) {
    enter("InferType");
    reportDiagnostic("InferType is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("InferType");
    return nullptr;
}

/*
DataType() : Data data ;
*/
type_ptr_t Constructor::visitDataType(const AST::node_ptr_t &ast) {
    enter("DataType");
    reportDiagnostic("DataType is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("DataType");
    return nullptr;
}

/*
RefType(Ref ref) ;
*/
type_ptr_t Constructor::visitRefType(const AST::node_ptr_t &ast) {
    enter("RefType");
    assert(ast->load()->type() == AST::LoadType::Type);
    auto const &typeLoad = ast->loadAs<AST::RefTypeLoad>();
    const Reference &ref = typeLoad->ref();
    const auto &type = typeScope_->at(ref);
    if (!type.has_value()) {
        reportDiagnostic("Unresolved type reference: " + ref.toString(), ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    }
    leave("RefType");
    return type.value();
}
} // namespace GraphConstructTree