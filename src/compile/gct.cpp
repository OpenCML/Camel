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

std::any visit(const AST::node_ptr_t &ast) {}

/*
Module(Ref ref) : ImportDecl import, ExportDecl export, Stmt* ;
*/
node_ptr_t Constructor::visitModule(const AST::node_ptr_t &ast) {
    enter("Module");
    assert(ast->type() == AST::LoadType::Module);

    root_ = createNodeBy<ExecLoad>();

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