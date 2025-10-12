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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "type.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

std::string to_string(TypeType type) {
    switch (type) {
    case TypeType::Null:
        return "Null";
    case TypeType::Expr:
        return "Expr";
    case TypeType::Array:
        return "List";
    case TypeType::Struct:
        return "Struct";
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
        ASSERT(false, "Unknown TypeType");
        return "Unknown";
    }
}

std::string to_string(TypeOp op) {
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
    case TypeOp::ErrorThen:
        return "?";
    case TypeOp::Specialize:
        return "SPEC";
    case TypeOp::TypeOf:
        return "typeof";
    case TypeOp::TypeAs:
        return "typeas";
    default:
        ASSERT(false, "Unknown TypeOp");
        return "Unknown";
    }
}

const std::string FuncTypeLoad::toString() const {
    std::string result = "FuncType:";
    if (implMark_ != ImplMark::Graph) {
        result += " " + to_string(implMark_);
    }
    if (!modifiers_.empty()) {
        result += " " + std::string(modifiers_);
    }
    if (!uri_.empty()) {
        result += " [" + uri_ + "]";
    }
    return result;
}

const std::string NullableTypeLoad::geneCode() const { return "?"; }

const std::string TypeExprLoad::geneCode() const { return to_string(op_); }

const std::string ArrayTypeLoad::geneCode() const {
    std::string result = "";
    for (size_t i = 0; i < dims_; i++) {
        result += "[]";
    }
    return result;
}

const std::string StructTypeLoad::geneCode() const { return "{}"; }

const std::string TupleTypeLoad::geneCode() const { return "()"; }

const std::string FuncTypeLoad::geneCode() const {
    std::string result = "";
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    if (!modifiers_.empty()) {
        result += std::string(modifiers_) + " ";
    }
    return result;
}

const std::string UnitTypeLoad::geneCode() const { return ref_.toString(); }

const std::string InferTypeLoad::geneCode() const { return ref_.toString(); }

const std::string DataTypeLoad::geneCode() const { return ref_.toString(); }

const std::string RefTypeLoad::geneCode() const {
    auto s = ref_.toString();
    // std::cout << "[RefTypeLoad::geneCode] " << s << std::endl;
    return s;
}

} // namespace AbstractSyntaxTree
