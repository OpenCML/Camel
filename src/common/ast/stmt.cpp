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

#include "stmt.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

const std::string ImportLoad::toString() const {
    if (path_.empty()) {
        return std::string("ImportDecl: NULL");
    }
    std::string result = "ImportDecl: ";
    if (!refs_.empty()) {
        result += "{ " + join(refs_, std::string(", ")) + " }";
    } else {
        result += "*";
    }
    if (!as_.ident().empty()) {
        result += " as " + as_.toString();
    }
    result += " from " + path_;
    return result;
}

const std::string ExportLoad::toString() const {
    if (refs_.empty()) {
        return std::string("ExportDecl: NULL");
    }
    std::string result = "ExportDecl: " + join(refs_, std::string(", "));
    return result;
}

const std::string DataDeclLoad::toString() const {
    std::string result = isVar_ ? "DataDecl: var " : "DataDecl: ";
    if (refs_.size() == 1) {
        result += refs_[0].toString();
    } else {
        std::string refs = join(refs_, std::string(", "));
        switch (type_) {
        case UnpackType::Dict:
            result += "{" + refs + "}";
            break;
        case UnpackType::List:
            result += "[" + refs + "]";
            break;
        case UnpackType::Tuple:
            result += "(" + refs + ")";
            break;
        default:
            throw std::runtime_error("Unknown UnpackType in DataDeclLoad: " + std::to_string(static_cast<int>(type_)));
        }
    }
    return result;
}

const std::string TypeDeclLoad::toString() const {
    std::string result = "TypeDecl: ";
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    result += ref_.toString();
    if (!uri_.empty()) {
        result += " [" + uri_ + "]";
    }
    return result;
}

const std::string ExitStmtLoad::toString() const {
    switch (type_) {
    case ExitType::Yield:
        return "ExitStmt: yield";
    case ExitType::Raise:
        return "ExitStmt: raise";
    case ExitType::Return:
        return "ExitStmt: return";
    case ExitType::Throw:
        return "ExitStmt: throw";
    default:
        throw std::runtime_error("Unknown ExitType");
    }
}

} // namespace AbstractSyntaxTree