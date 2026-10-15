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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/parse/ast/stmt.h"
#include "camel/utils/str.h"

#include <algorithm>

namespace camel::parse::ast {

const std::string StmtLoad::geneCode() const { return ""; }

const std::string ImportLoad::toString() const {
    if (path_.empty()) {
        return std::string("ImportDecl: null");
    }
    std::string result = "ImportDecl: ";
    if (!refs_.empty()) {
        result += "{ ";
        for (size_t i = 0; i < refs_.size(); ++i) {
            result += (i ? ", " : "") + refs_[i].toString();
            if (!locals_.empty() && !(locals_[i] == refs_[i])) {
                result += " as " + locals_[i].toString();
            }
        }
        result += " }";
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

std::string StmtLoad::docSuffix() const {
    if (doc_.empty()) {
        return "";
    }
    const size_t eol   = doc_.find('\n');
    const size_t more  = static_cast<size_t>(std::count(doc_.begin(), doc_.end(), '\n'));
    std::string suffix = " /// " + doc_.substr(0, eol);
    if (more > 0) {
        suffix += " (+" + std::to_string(more) + (more == 1 ? " line)" : " lines)");
    }
    return suffix;
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
    return result + docSuffix();
}

const std::string TypeDeclLoad::toString() const {
    std::string result = "TypeDecl: ";
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    result += ref_.toString();
    if (!params_.empty()) {
        result += "<";
        for (size_t i = 0; i < params_.size(); ++i) {
            result += (i > 0 ? ", " : "") + params_[i];
        }
        result += ">";
    }
    if (!uri_.empty()) {
        result += " [" + uri_ + "]";
    }
    return result + docSuffix();
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

const std::string FuncDeclLoad::geneCode() const {
    return "func " + (operatorSymbol_.empty() ? ref_.toString() : "operator " + operatorSymbol_);
}

const std::string TypeDeclLoad::geneCode() const {
    std::string result = "type ";
    if (implMark_ != ImplMark::Graph) {
        result += to_string(implMark_) + " ";
    }
    result += ref_.toString();
    if (!params_.empty()) {
        result += "<";
        for (size_t i = 0; i < params_.size(); ++i) {
            result += (i > 0 ? ", " : "") + params_[i];
        }
        result += ">";
    }
    return result;
}

const std::string UsingNamespaceLoad::geneCode() const {
    return "using namespace " + ref_.toString();
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

} // namespace camel::parse::ast
