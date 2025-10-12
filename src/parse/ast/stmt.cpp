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

#include "stmt.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

const std::string StmtLoad::geneCode() const { return ""; }

const std::string ImportLoad::toString() const {
    if (path_.empty()) {
        return std::string("ImportDecl: null");
    }
    std::string result = "ImportDecl: ";
    if (!refs_.empty()) {
        result += "{ " +
                  strutil::join(refs_, ", ", [](const Reference &ref) { return ref.toString(); }) +
                  " }";
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
        return std::string("ExportDecl: null");
    }
    std::string result = "ExportDecl: " + strutil::join(refs_, ", ", [](const Reference &ref) {
                             return ref.toString();
                         });
    return result;
}

const std::string DataDeclLoad::toString() const {
    std::string result = isVar_ ? "DataDecl: var " : "DataDecl: ";
    if (refs_.size() == 1) {
        result += refs_[0].toString();
    } else {
        std::string refs =
            strutil::join(refs_, ", ", [](const Reference &ref) { return ref.toString(); });
        switch (type_) {
        case UnpackType::Struct:
            result += "{" + refs + "}";
            break;
        case UnpackType::Array:
            result += "[" + refs + "]";
            break;
        case UnpackType::Tuple:
            result += "(" + refs + ")";
            break;
        default:
            throw std::runtime_error(
                "Unknown UnpackType in DataDeclLoad: " + std::to_string(static_cast<int>(type_)));
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

const std::string DataDeclLoad::geneCode() const {
    std::string result = isVar_ ? "var " : "let ";

    if (refs_.size() == 1) {
        result += refs_[0].toString();
    } else {
        std::string refs =
            strutil::join(refs_, ", ", [](const Reference &ref) { return ref.toString(); });
        switch (type_) {
        case UnpackType::Struct:
            result += "{" + refs + "}";
            break;
        case UnpackType::Array:
            result += "[" + refs + "]";
            break;
        case UnpackType::Tuple:
            result += "(" + refs + ")";
            break;
        default:
            result += "Unknown";
        }
    }
    return result;
}

const std::string FuncDeclLoad::geneCode() const { return "func " + ref_.toString(); }

const std::string TypeDeclLoad::geneCode() const {
    std::string result = "type ";
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    result += ref_.toString();
    return result;
}

const std::string NameDeclLoad::geneCode() const {
    return "name " + ref_.toString() + " as " + alias_.toString();
}

const std::string ExitStmtLoad::geneCode() const {
    switch (type_) {
    case ExitType::Yield:
        return "yield";
    case ExitType::Raise:
        return "raise";
    case ExitType::Return:
        return "return";
    case ExitType::Throw:
        return "throw";
    default:
        return "exit";
    }
}
const std::string StmtBlockLoad::geneCode() const {
    std::string result = "block";
    if (synced_) {
        result += " sync";
    }
    if (waited_) {
        result += " wait";
    }
    return result;
}

} // namespace AbstractSyntaxTree
