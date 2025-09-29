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
 * Created: Jul. 03, 2025
 * Updated: Sep. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <stdexcept>

#include "data.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

std::string to_string(DataType type) {
    switch (type) {
    case DataType::UnaryExpr:
        return "UnaryExpr";
    case DataType::BinaryExpr:
        return "BinaryExpr";
    case DataType::ReservedExpr:
        return "ReservedExpr";
    case DataType::IfExpr:
        return "IfExpr";
    case DataType::MatchExpr:
        return "MatchExpr";
    case DataType::TryExpr:
        return "TryExpr";
    case DataType::Literal:
        return "Literal";
    case DataType::List:
        return "List";
    case DataType::Dict:
        return "Dict";
    case DataType::Tuple:
        return "Tuple";
    case DataType::Func:
        return "Func";
    case DataType::Ref:
        return "Ref";
    default:
        ASSERT(false, "Unknown DataType");
        return "UnknownDataType";
    }
}

std::string to_string(UnaryDataOp op) {
    switch (op) {
    case UnaryDataOp::Not:
        return "!";
    case UnaryDataOp::Neg:
        return "-";
    case UnaryDataOp::Inv:
        return "~";
    default:
        ASSERT(false, "Unknown UnaryDataOp");
        return "UnknownUnaryDataOp";
    }
}

std::string to_string(BinaryDataOp op) {
    switch (op) {
    case BinaryDataOp::Assign:
        return "=";
    case BinaryDataOp::AssignAdd:
        return "+=";
    case BinaryDataOp::AssignSub:
        return "-=";
    case BinaryDataOp::AssignMul:
        return "*=";
    case BinaryDataOp::AssignDiv:
        return "/=";
    case BinaryDataOp::AssignMod:
        return "%=";
    case BinaryDataOp::AssignMat:
        return "@=";
    case BinaryDataOp::AssignExp:
        return "^=";
    case BinaryDataOp::AssignAnd:
        return "&=";
    case BinaryDataOp::AssignOr:
        return "|=";
    case BinaryDataOp::Or:
        return "||";
    case BinaryDataOp::And:
        return "&&";
    case BinaryDataOp::Eq:
        return "==";
    case BinaryDataOp::Neq:
        return "!=";
    case BinaryDataOp::StrictEq:
        return "===";
    case BinaryDataOp::StrictNeq:
        return "!==";
    case BinaryDataOp::Less:
        return "<";
    case BinaryDataOp::LessEq:
        return "<=";
    case BinaryDataOp::Greater:
        return ">";
    case BinaryDataOp::GreaterEq:
        return ">=";
    case BinaryDataOp::Add:
        return "+";
    case BinaryDataOp::Sub:
        return "-";
    case BinaryDataOp::Mul:
        return "*";
    case BinaryDataOp::Div:
        return "/";
    case BinaryDataOp::Mod:
        return "%";
    case BinaryDataOp::Mat:
        return "@";
    case BinaryDataOp::Pow:
        return "^";
    case BinaryDataOp::Index:
        return "[]";
    default:
        ASSERT(false, "Unknown BinaryDataOp");
        return "UnknownBinaryDataOp";
    }
}

std::string to_string(ReservedDataOp op) {
    switch (op) {
    case ReservedDataOp::NullThen:
        return "??";
    case ReservedDataOp::ErrorThen:
        return "!!";
    case ReservedDataOp::NotNullThen:
        return "?";
    case ReservedDataOp::Access:
        return ".$";
    case ReservedDataOp::Call:
        return "()";
    case ReservedDataOp::With:
        return "<>";
    case ReservedDataOp::Bind:
        return "..";
    case ReservedDataOp::As:
        return "AS";
    case ReservedDataOp::Is:
        return "IS";
    default:
        ASSERT(false, "Unknown ReservedDataOp");
        return "UnknownReservedDataOp";
    }
}

const std::string DataLoad::geneCode() const { return ""; }

const std::string UnaryExprLoad::geneCode() const { return to_string(op_); }

const std::string BinaryExprLoad::geneCode() const { return to_string(op_); }

const std::string ReservedExprLoad::geneCode() const { return to_string(op_); }

const std::string IfExprLoad::geneCode() const { return "if"; }

const std::string MatchExprLoad::geneCode() const { return "match"; }

const std::string TryExprLoad::geneCode() const { return "try"; }

const std::string LiteralLoad::geneCode() const { return value_.toString(); }

const std::string ListDataLoad::geneCode() const { return "[]"; }

const std::string DictDataLoad::geneCode() const { return "{}"; }

const std::string TupleDataLoad::geneCode() const { return "()"; }

const std::string FuncDataLoad::geneCode() const {
    if (ref_.empty()) {
        return "func";
    } else {
        return "func " + ref_.toString();
    }
}

const std::string RefDataLoad::geneCode() const { return ref_.toString(); }

} // namespace AbstractSyntaxTree
