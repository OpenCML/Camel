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

#include "type.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

std::string typeTypeToString(TypeType type) {
    switch (type) {
    case TypeType::Expr:
        return "Expr";
    case TypeType::List:
        return "List";
    case TypeType::Dict:
        return "Dict";
    case TypeType::Tuple:
        return "Tuple";
    case TypeType::Func:
        return "Func";
    case TypeType::Spec:
        return "Spec";
    case TypeType::Unit:
        return "Unit";
    case TypeType::Infer:
        return "Infer";
    case TypeType::Data:
        return "Data";
    case TypeType::Ref:
        return "Ref";
    default:
        throw std::runtime_error("Unknown TypeType");
    }
}

std::string typeOpToString(TypeOp op) {
    switch (op) {
    case TypeOp::Union:
        return "|";
    case TypeOp::Inter:
        return "&";
    case TypeOp::Diff:
        return "/";
    case TypeOp::KeyUnion:
        return "KEY_UNION";
    case TypeOp::KeyInter:
        return "KEY_INTER";
    case TypeOp::KeyDiff:
        return "KEY_DIFF";
    default:
        throw std::runtime_error("Unknown TypeOp");
    }
}

const std::string FuncTypeLoad::toString() const {
    std::string result = "FuncTypeLoad: " + typeTypeToString(TypeType::Func);
    if (!uri_.empty()) {
        result += " @ " + uri_;
    }
    result += " [" + implMarkToString(implMark_) + "]";
    return result;
}

} // namespace AbstractSyntaxTree