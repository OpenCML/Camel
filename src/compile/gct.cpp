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

template <typename LoadType, typename... Args> node_ptr_t createNodeBy(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNodeBy<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}

inline bool optHasVal(const AST::node_ptr_t &opt) {
    assert(opt->type() == AST::LoadType::Optional);
    return opt && opt->size() > 0;
}

std::any visit(const AST::node_ptr_t &ast) {}

/*
Module(Ref ref) : ImportDecl* import, ExportDecl? export, Stmt* ;
*/
node_ptr_t Constructor::visitModule(const AST::node_ptr_t &ast) {
    enter("Module");
    assert(ast->type() == AST::LoadType::Module);
    auto moduleLoad = ast->loadAs<AST::ModuleLoad>();
    auto importNodes = ast->at(0);
    auto exportOptNode = ast->at(1);
    auto stmtNodes = ast->at(2);

    for (auto &import : *importNodes) {
        // TODO: Handle import declarations
    }

    root_ = createNodeBy<ExecLoad>();
    for (auto &stmt : *stmtNodes) {
        auto node = visitStmt(stmt);
        *root_ << node;
    }

    if (optHasVal(exportOptNode)) {
        auto exportNode = visitExport(exportOptNode);
        *root_ << exportNode;
    }

    leave("Module");
}

/*
ImportDecl(string path, Ref[] refs, Ref as) ;
*/
node_ptr_t Constructor::visitImport(const AST::node_ptr_t &ast) {}

/*
ExportDecl(Ref[] refs) ;
*/
node_ptr_t Constructor::visitExport(const AST::node_ptr_t &ast) {}

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

node_ptr_t Constructor::visitType(const AST::node_ptr_t &ast) {
    enter("Type");
    assert(ast->type() == AST::LoadType::Type);
    const auto &type = ast->loadAs<AST::TypeLoad>();
    node_ptr_t typeNode;

    switch (type->typeType()) {
    case AST::TypeType::Null:
        typeNode = visitNullableType(ast);
        break;
    case AST::TypeType::Expr:
        typeNode = visitTypeExpr(ast);
        break;
    case AST::TypeType::List:
        typeNode = visitListType(ast);
        break;
    case AST::TypeType::Dict:
        typeNode = visitDictType(ast);
        break;
    case AST::TypeType::Tuple:
        typeNode = visitTupleType(ast);
        break;
    case AST::TypeType::Func:
        typeNode = visitFuncType(ast);
        break;
    case AST::TypeType::Unit:
        typeNode = visitUnitType(ast);
        break;
    case AST::TypeType::Infer:
        typeNode = visitInferType(ast);
        break;
    case AST::TypeType::Data:
        typeNode = visitDataType(ast);
        break;
    case AST::TypeType::Ref:
        typeNode = visitRefType(ast);
        break;
    default:
        throw std::runtime_error("Unknown type type");
    }

    leave("Type");
    return typeNode;
}

/*

*/
node_ptr_t Constructor::visitNullableType(const AST::node_ptr_t &ast) {}

/*
TypeExpr(TypeOp op) := Type lhs, Type rhs ;
*/
node_ptr_t Constructor::visitTypeExpr(const AST::node_ptr_t &ast) {}

/*
ListType(siz dim) : Type type ;
*/
node_ptr_t Constructor::visitListType(const AST::node_ptr_t &ast) {}

/*
DictType() : NamedType* types ;
*/
node_ptr_t Constructor::visitDictType(const AST::node_ptr_t &ast) {}

/*
TupleType() : Type* types ;
*/
node_ptr_t Constructor::visitTupleType(const AST::node_ptr_t &ast) {}

/*
FuncType(Modifier[] modifiers, ImplMark impl, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
*/
node_ptr_t Constructor::visitFuncType(const AST::node_ptr_t &ast) {}

/*
UnitType(Ref ref) : Type type ;
*/
node_ptr_t Constructor::visitUnitType(const AST::node_ptr_t &ast) {}

/*
InferType(Ref ref) ;
*/
node_ptr_t Constructor::visitInferType(const AST::node_ptr_t &ast) {}

/*
DataType() : Data data ;
*/
node_ptr_t Constructor::visitDataType(const AST::node_ptr_t &ast) {}

/*
Type(Ref ref) ;
*/
node_ptr_t Constructor::visitRefType(const AST::node_ptr_t &ast) {}
} // namespace GraphConstructTree