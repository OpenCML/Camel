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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2024
 * Updated: Apr. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iterator>

#include "ast.h"
#include "utils/log.h"

using namespace std;
using namespace AST;

const string Load::typeStr() const {
    switch (type_) {
    case NodeType::Program:
        return "Program";
    case NodeType::Decl:
        return "Decl";
    case NodeType::ModuleDecl:
        return "ModuleDecl";
    case NodeType::ImportDecl:
        return "ImportDecl";
    case NodeType::ExportDecl:
        return "ExportDecl";
    case NodeType::LetDecl:
        return "LetDecl";
    case NodeType::UseDecl:
        return "UseDecl";
    case NodeType::FuncDecl:
        return "FuncDecl";
    case NodeType::TypeDecl:
        return "TypeDecl";
    case NodeType::EnumDecl:
        return "EnumDecl";
    case NodeType::ImplMark:
        return "ImplMark";
    case NodeType::Modifiers:
        return "Modifiers";
    case NodeType::Stmt:
        return "Stmt";
    case NodeType::StmtBlock:
        return "StmtBlock";
    case NodeType::RetStmt:
        return "RetStmt";
    case NodeType::WaitStmt:
        return "WaitStmt";
    case NodeType::LambdaExpr:
        return "LambdaExpr";
    case NodeType::KeyTypePair:
        return "KeyTypePair";
    case NodeType::KeyValuePair:
        return "KeyValuePair";
    case NodeType::KeyParamPair:
        return "KeyParamPair";
    case NodeType::IdentDefList:
        return "IdentDefList";
    case NodeType::ValueList:
        return "ValueList";
    case NodeType::IndexValues:
        return "IndexValues";
    case NodeType::PairedValues:
        return "PairedValues";
    case NodeType::PairedParams:
        return "PairedParams";
    case NodeType::ArgumentList:
        return "ArgumentList";
    case NodeType::MemberAccess:
        return "MemberAccess";
    case NodeType::DataExpr:
        return "DataExpr";
    case NodeType::DataList:
        return "DataList";
    case NodeType::CtrlExpr:
        return "CtrlExpr";
    case NodeType::IfLoad:
        return "IfLoad";
    case NodeType::CatchClause:
        return "CatchClause";
    case NodeType::BlockExpr:
        return "BlockExpr";
    case NodeType::WaitExpr:
        return "WaitExpr";
    case NodeType::Wildcard:
        return "Wildcard";
    case NodeType::AssignExpr:
        return "AssignExpr";
    case NodeType::LogicalOrExpr:
        return "LogicalOrExpr";
    case NodeType::LogicalAndExpr:
        return "LogicalAndExpr";
    case NodeType::EqualityExpr:
        return "EqualityExpr";
    case NodeType::RelationalExpr:
        return "RelationalExpr";
    case NodeType::AdditiveExpr:
        return "AdditiveExpr";
    case NodeType::MultiplicativeExpr:
        return "MultiplicativeExpr";
    case NodeType::NullableExpr:
        return "NullableExpr";
    case NodeType::UnaryExpr:
        return "UnaryExpr";
    case NodeType::LinkExpr:
        return "LinkExpr";
    case NodeType::BindExpr:
        return "BindExpr";
    case NodeType::WithExpr:
        return "WithExpr";
    case NodeType::AnnoExpr:
        return "AnnoExpr";
    case NodeType::DictExpr:
        return "DictExpr";
    case NodeType::Literal:
        return "Literal";
    case NodeType::TypeExpr:
        return "TypeExpr";
    case NodeType::UnionType:
        return "UnionType";
    case NodeType::UnionUnit:
        return "UnionUnit";
    case NodeType::ListType:
        return "ListType";
    case NodeType::ArgsType:
        return "ArgsType";
    case NodeType::PrimaryType:
        return "PrimaryType";
    case NodeType::DictExprType:
        return "DictExprType";
    case NodeType::TypeList:
        return "TypeList";
    case NodeType::LambdaType:
        return "LambdaType";
    case NodeType::IdentDef:
        return "IdentDef";
    case NodeType::IdentRef:
        return "IdentRef";
    default:
        return "Unknown";
    }
}

const string ModuleDecl::toString() const {
    stringstream ss;
    ss << "module " << ident_;
    return ss.str();
}

const string ImportDecl::toString() const {
    stringstream ss;
    ss << "import ";
    if (!ident_.empty()) {
        ss << ident_;
    }
    if (!from_.empty()) {
        ss << " from " << from_;
    }
    return ss.str();
}

const string LambdaExpr::toString() const { return "lambda expr"; }

const string FuncDecl::toString() const { return "func decl"; }

const string LetDecl::toString() const { return "let decl"; }

const string UseDecl::toString() const { return "use decl"; }

const string RetStmt::toString() const { return "ret stmt"; }

const string TypeDecl::toString() const { return "type decl"; }

const string EnumDecl::toString() const { return "enum decl"; }

const string ImplMark::toString() const { return "impl mark"; }

const string Modifiers::toString() const { return "modifiers"; }

const string KeyTypePair::toString() const { return "key type pair"; }

const string KeyValuePair::toString() const { return "key value pair"; }

const string KeyParamPair::toString() const { return "key param pair"; }

const string IdentList::toString() const { return "ident def list"; }

const string DataList::toString() const { return "data list"; }

const string ValueList::toString() const { return "value list"; }

const string IndexValues::toString() const { return "index values"; }

const string PairedValues::toString() const { return "paired values"; }

const string PairedParams::toString() const { return "paired params"; }

const string ArgumentList::toString() const { return "argument list"; }

const string MemberAccess::toString() const { return "member access"; }

const string WaitExpr::toString() const { return "wait expr"; }

const string Wildcard::toString() const { return "wildcard"; }

const string MatchCase::toString() const { return "match case"; }

const string IfLoad::toString() const { return "if load"; }

const string CatchClause::toString() const { return "catch clause"; }

const string AssignExpr::toString() const { return "assign expr"; }

const string LogicalOrExpr::toString() const { return "logical or expr"; }

const string LogicalAndExpr::toString() const { return "logical and expr"; }

const string EqualityExpr::toString() const { return "equality expr"; }

const string RelationalExpr::toString() const { return "relational expr"; }

const string AdditiveExpr::toString() const { return "additive expr"; }

const string MultiplicativeExpr::toString() const { return "multiplicative expr"; }

const string NullableExpr::toString() const { return "nullable expr"; }

const string UnaryExpr::toString() const { return "unary expr"; }

const string LinkExpr::toString() const { return "link expr"; }

const string BindExpr::toString() const { return "bind expr"; }

const string WithExpr::toString() const { return "with expr"; }

const string AnnoExpr::toString() const { return "anno expr"; }

const string DictExpr::toString() const { return "dict expr"; }

const string ListExpr::toString() const { return "list expr"; }

const string Literal::toString() const { return "literal"; }

const string TypeExpr::toString() const { return "type expr"; }

const string UnionUnit::toString() const { return "union type"; }

const string ArgsType::toString() const { return "args type"; }

const string DictExprType::toString() const { return "dict expr type"; }

const string TypeList::toString() const { return "type list"; }

const string LambdaType::toString() const { return "lambda type"; }

const string IdentDef::toString() const { return "ident def"; }

const string IdentRef::toString() const { return "ident ref"; }