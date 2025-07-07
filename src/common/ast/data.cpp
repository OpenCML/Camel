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
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <stdexcept>

#include "data.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

std::string dataOpToString(DataOp op) {
    switch (op) {
    case DataOp::Assign:
        return "=";
    case DataOp::AssignAdd:
        return "+=";
    case DataOp::AssignSub:
        return "-=";
    case DataOp::AssignMul:
        return "*=";
    case DataOp::AssignDiv:
        return "/=";
    case DataOp::AssignMod:
        return "%=";
    case DataOp::AssignMat:
        return "@=";
    case DataOp::AssignExp:
        return "^=";
    case DataOp::AssignAnd:
        return "&=";
    case DataOp::AssignOr:
        return "|=";
    case DataOp::Or:
        return "||";
    case DataOp::And:
        return "&&";
    case DataOp::Eq:
        return "==";
    case DataOp::Neq:
        return "!=";
    case DataOp::StrictEq:
        return "===";
    case DataOp::StrictNeq:
        return "!==";
    case DataOp::Less:
        return "<";
    case DataOp::LessEq:
        return "<=";
    case DataOp::Greater:
        return ">";
    case DataOp::GreaterEq:
        return ">=";
    case DataOp::Add:
        return "+";
    case DataOp::Sub:
        return "-";
    case DataOp::Mul:
        return "*";
    case DataOp::Div:
        return "/";
    case DataOp::Mod:
        return "%";
    case DataOp::Mat:
        return "@";
    case DataOp::Exp:
        return "^";
    case DataOp::NullThen:
        return "??";
    case DataOp::ErrorThen:
        return "!!";
    case DataOp::NotNullThen:
        return "?";
    case DataOp::Call:
        return "->";
    case DataOp::With:
        return ".";
    case DataOp::Bind:
        return "..";
    case DataOp::As:
        return "AS";
    case DataOp::Is:
        return "IS";
    case DataOp::Not:
        return "!";
    case DataOp::Neg:
        return "-";
    case DataOp::Inv:
        return "~";
    default:
        throw std::runtime_error("Unknown data operation");
    }
}

std::string dataTypeToString(DataType type) {
    switch (type) {
    case DataType::DataExpr:
        return "DataExpr";
    case DataType::IfExpr:
        return "IfExpr";
    case DataType::MatchExpr:
        return "MatchExpr";
    case DataType::TryExpr:
        return "TryExpr";
    case DataType::UnaryExpr:
        return "UnaryExpr";
    case DataType::Literal:
        return "Literal";
    case DataType::List:
        return "List";
    case DataType::Dict:
        return "Dict";
    case DataType::Tuple:
        return "Tuple";
    case DataType::Index:
        return "Index";
    case DataType::Func:
        return "Func";
    case DataType::Ref:
        return "Ref";
    default:
        throw std::runtime_error("Unknown DataType");
    }
}

} // namespace AbstractSyntaxTree