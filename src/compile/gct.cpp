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
#include "utils/token.h"
#include "utils/type.h"

#define DEBUG_LEVEL 0

using namespace std;
using namespace GCT;

namespace GraphConstructTree {

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

data_ptr_t extractStaticDataFromNode(const node_ptr_t &node) {
    if (node->type() == NodeType::DATA) {
        return node->loadAs<DataLoad>()->data();
    } else if (node->type() == NodeType::DREF) {
        return make_shared<RefData>(node->loadAs<DRefLoad>()->ref());
    } else {
        return nullptr;
    }
}

pair<node_ptr_t, data_ptr_t> Constructor::makeRefData(const node_ptr_t &expr) {
    const string indent = to_string(indentIndex_++);
    node_ptr_t refNode = createNodeAs<NRefLoad>(indent);
    *refNode << expr;
    data_ptr_t data = make_shared<RefData>(indent);
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
node_ptr_t Constructor::visitStmtBlock(const AST::node_ptr_t &ast) {
    enter("StmtBlock");
    assert(ast->type() == AST::LoadType::Stmt);
    const auto &stmtBlock = ast->loadAs<AST::StmtBlockLoad>();
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    pushScope();
    for (const auto &stmt : *ast->atAs<AST::RepeatedLoad>(0)) {
        assert(stmt->type() == AST::LoadType::Stmt && "Expected a statement in StmtBlock");
        *execNode << visitStmt(stmt);
    }
    popScope();

    leave("StmtBlock");
    return execNode;
}

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
class DataOp {
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
    NullThen,
    ErrorThen,
    NotNullThen,
    Index,
    Call,
    With,
    Bind,
    As,
    Is,
    Not,
    Neg,
    Inv
};
*/
node_ptr_t Constructor::visitDataExpr(const AST::node_ptr_t &ast) {
    enter("DataExpr");
    assert(ast->type() == AST::LoadType::Data);
    const auto &dataExpr = ast->loadAs<AST::BinaryExprLoad>();
    node_ptr_t res;
    node_ptr_t execNode = createNodeAs<ExecLoad>();

    node_ptr_t opNode;

    switch (dataExpr->op()) {
    // Assignment operations
    case AST::DataOp::Assign:
        opNode = createNodeAs<DRefLoad>("__assn__");
        break;
    case AST::DataOp::AssignAdd: {
    } break;
    case AST::DataOp::AssignSub: {
    } break;
    case AST::DataOp::AssignMul: {
    } break;
    case AST::DataOp::AssignDiv: {
    } break;
    case AST::DataOp::AssignMod: {
    } break;
    case AST::DataOp::AssignMat: {
    } break;
    case AST::DataOp::AssignExp: {
    } break;
    case AST::DataOp::AssignAnd: {
    } break;
    case AST::DataOp::AssignOr: {
    } break;

    // Logical operations
    case AST::DataOp::Or: {
    } break;
    case AST::DataOp::And: {
    } break;
    case AST::DataOp::Eq: {
    } break;
    case AST::DataOp::Neq: {
    } break;
    case AST::DataOp::StrictEq: {
    } break;
    case AST::DataOp::StrictNeq: {
    } break;
    case AST::DataOp::Less: {
    } break;
    case AST::DataOp::LessEq: {
    } break;
    case AST::DataOp::Greater: {
    } break;
    case AST::DataOp::GreaterEq: {
    } break;

    // Arithmetic operations
    case AST::DataOp::Add: {
    } break;
    case AST::DataOp::Sub: {
    } break;
    case AST::DataOp::Mul: {
    } break;
    case AST::DataOp::Div: {
    } break;
    case AST::DataOp::Mod: {
    } break;
    case AST::DataOp::Mat: {
    } break;
    case AST::DataOp::Exp: {
    } break;

    // Null-safety operations
    case AST::DataOp::NullThen: {
    } break;
    case AST::DataOp::ErrorThen: {
    } break;
    case AST::DataOp::NotNullThen: {
    } break;

    // Indexing and function call operations
    case AST::DataOp::Index: {
    } break;
    case AST::DataOp::Call: {
    } break;
    case AST::DataOp::With: {
    } break;
    case AST::DataOp::Bind: {
    } break;

    // Type checking operations
    case AST::DataOp::As: {
    } break;
    case AST::DataOp::Is: {
    } break;

    // Unary operations
    case AST::DataOp::Not: {
    } break;
    case AST::DataOp::Neg: {
    } break;
    case AST::DataOp::Inv: {
    } break;

    default: {
        reportDiagnostic("Unsupported DataOp: " + dataOpToString(dataExpr->op()), ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    }
    }

    leave("DataExpr");
    return res;
}

/*
IfExpr() : Data cond, StmtBlock then, StmtBlock? else ;
*/
node_ptr_t Constructor::visitIfExpr(const AST::node_ptr_t &ast) {
    enter("IfExpr");
    assert(ast->type() == AST::LoadType::Data);
    reportDiagnostic("IfExpr is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("IfExpr");
    return nullptr;
}

/*

*/
node_ptr_t Constructor::visitMatchExpr(const AST::node_ptr_t &ast) {
    enter("MatchExpr");
    assert(ast->type() == AST::LoadType::Data);
    reportDiagnostic("MatchExpr is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("MatchExpr");
    return nullptr;
}

/*

*/
node_ptr_t Constructor::visitTryExpr(const AST::node_ptr_t &ast) {
    enter("TryExpr");
    assert(ast->type() == AST::LoadType::Data);
    reportDiagnostic("TryExpr is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
    throw BuildAbortException();
    leave("TryExpr");
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
    enter("Literal");
    assert(ast->type() == AST::LoadType::Data);
    const auto &literal = ast->loadAs<AST::LiteralLoad>();
    const Literal &value = literal->value();
    const auto &str = value.data();
    data_ptr_t data;
    node_ptr_t res = createNodeAs<DataLoad>(data);
    switch (value.type()) {
    case LiteralType::String: {
        data = tt::as_shared<Data>(make_shared<StringData>(str));
    } break;
    case LiteralType::FString: {
        reportDiagnostic("FString is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
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
            reportDiagnostic("Invalid boolean literal: " + str, ast->load()->tokenRange(), Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
    } break;
    case LiteralType::Null: {
        data = tt::as_shared<Data>(make_shared<NullData>());
    } break;
    default: {
        reportDiagnostic("Unknown literal type", ast->load()->tokenRange(), Diagnostic::Severity::Error);
        throw BuildAbortException();
    }
    }
    leave("Literal");
    return res;
}

/*
ListData() : Data* data ;
*/
node_ptr_t Constructor::visitListData(const AST::node_ptr_t &ast) {
    enter("ListData");
    assert(ast->type() == AST::LoadType::Data);
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
    leave("ListData");
    return res;
}

/*
DictData() : NamedData* dataList ;
*/
node_ptr_t Constructor::visitDictData(const AST::node_ptr_t &ast) {
    enter("DictData");
    assert(ast->type() == AST::LoadType::Data);
    auto dictData = make_shared<DictData>();
    node_ptr_t res = createNodeAs<DataLoad>(dictData);
    bool dangling = false;
    node_ptr_t execNode = createNodeAs<ExecLoad>();
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name = namedPair->getRef().ident();
        node_ptr_t dataNode = visitData(child->atAs<AST::DataLoad>(0));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        dictData->emplace(name, data);
    }
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    leave("DictData");
    return res;
}

/*
TupleData() : Data* data ;
*/
node_ptr_t Constructor::visitTupleData(const AST::node_ptr_t &ast) {
    enter("TupleData");
    assert(ast->type() == AST::LoadType::Data);
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
    leave("TupleData");
    return res;
}

/*
FuncData(Ref ref) : FuncType funcType, StmtBlock body ;
*/
node_ptr_t Constructor::visitFuncData(const AST::node_ptr_t &ast) {
    enter("FuncData");
    assert(ast->type() == AST::LoadType::Data);
    const auto &funcData = ast->loadAs<AST::FuncDataLoad>();
    func_type_ptr_t funcType = tt::as_shared<FunctionType>(visitFuncType(ast->atAs<AST::FuncTypeLoad>(0)));
    node_ptr_t stmtsNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    node_ptr_t funcNode = createNodeAs<FuncLoad>(funcType);
    *funcNode << stmtsNode;
    leave("FuncData");
    return funcNode;
}

/*
RefData(Ref ref) ;
*/
node_ptr_t Constructor::visitRefData(const AST::node_ptr_t &ast) {
    enter("RefData");
    assert(ast->type() == AST::LoadType::Data);
    const auto &refData = ast->loadAs<AST::RefDataLoad>();
    node_ptr_t refNode = createNodeAs<DRefLoad>(refData->ref());
    leave("RefData");
    return refNode;
}

/*
Type(TypeType type) :=
    NullableType | TypeExpr | ListType | DictType | TupleType
    | FuncType | SpecType | UnitType | InferType | DataType | RefType ;
*/
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
NullableType : Type type ;
*/
type_ptr_t Constructor::visitNullableType(const AST::node_ptr_t &ast) {
    enter("NullableType");
    assert(ast->type() == AST::LoadType::Type);
    type_ptr_t type = visitType(ast->atAs<AST::TypeLoad>(0));
    leave("NullableType");
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
    enter("TypeExpr");
    assert(ast->type() == AST::LoadType::Type);
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
        reportDiagnostic("TypeOp::Inter is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::Diff: {
        reportDiagnostic("TypeOp::Diff is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyUnion: {
        reportDiagnostic("TypeOp::KeyUnion is not supported yet", ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyInter: {
        reportDiagnostic("TypeOp::KeyInter is not supported yet", ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::KeyDiff: {
        reportDiagnostic("TypeOp::KeyDiff is not supported yet", ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::ErrorThen: {
        reportDiagnostic("TypeOp::ErrorThen is not supported yet", ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::Specialize: {
        reportDiagnostic("TypeOp::Specialize is not supported yet", ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::TypeOf: {
        reportDiagnostic("TypeOp::TypeOf is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    case AST::TypeOp::TypeAs: {
        reportDiagnostic("TypeOp::TypeAs is not supported yet", ast->load()->tokenRange(), Diagnostic::Severity::Error);
        throw BuildAbortException();
    } break;
    default:
        reportDiagnostic("Unsupported TypeOp: " + AST::typeOpToString(op), ast->load()->tokenRange(),
                         Diagnostic::Severity::Error);
        throw BuildAbortException();
    }
    leave("TypeExpr");
    return res;
}

/*
ListType(siz dim) : Type type ;
*/
type_ptr_t Constructor::visitListType(const AST::node_ptr_t &ast) {
    enter("ListType");
    assert(ast->type() == AST::LoadType::Type);
    const auto &listTypeLoad = ast->loadAs<AST::ListTypeLoad>();
    type_ptr_t type = visitType(ast->atAs<AST::TypeLoad>(0));
    const auto &arrayType = make_shared<ArrayType>(type, listTypeLoad->dims());
    leave("ListType");
    return arrayType;
}

/*
DictType() : NamedType* types ;
*/
type_ptr_t Constructor::visitDictType(const AST::node_ptr_t &ast) {
    enter("DictType");
    assert(ast->type() == AST::LoadType::Type);
    auto res = make_shared<DictType>();
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedPairLoad>();
        const string &name = namedPair->getRef().ident();
        type_ptr_t type = visitType(child->atAs<AST::TypeLoad>(0));
        data_ptr_t data = nullptr;
        if (!res->add(name, type)) {
            reportDiagnostic("Duplicate key detected: " + name, namedPair->tokenRange(), Diagnostic::Severity::Error);
            throw BuildAbortException();
        }
    }
    leave("DictType");
    return res;
}

/*
TupleType() : Type* types ;
*/
type_ptr_t Constructor::visitTupleType(const AST::node_ptr_t &ast) {
    enter("TupleType");
    assert(ast->type() == AST::LoadType::Type);
    vector<type_ptr_t> types;
    for (const auto &child : *ast) {
        type_ptr_t type = visitType(child);
        types.push_back(type);
    }
    type_ptr_t tupleType = make_shared<TupleType>(types);
    leave("TupleType");
    return tupleType;
}

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
    funcType->setImplMark(typeLoad->implMark());
    funcType->setModifiers(typeLoad->modifiers());

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