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

const std::string ImportDeclLoad::toString() const {
    std::string result = "ImportDecl: " + path_;
    if (!refs_.empty()) {
        result += " refs: " + join(refs_, std::string(", "));
    }
    if (!as_.ident().empty()) {
        result += " as: " + as_.toString();
    }
    return result;
}

const std::string ExportDeclLoad::toString() const {
    std::string result = "ExportDecl: " + join(refs_, std::string(", "));
    return result;
}

const std::string TypeDeclLoad::toString() const {
    std::string result = "TypeDecl: " + implMarkToString(implMark_) + ref_.toString();
    if (!uri_.empty()) {
        result += " @ " + uri_;
    }
    return result;
}

const std::string ExitStmtLoad::toString() const {
    switch (type_) {
    case ExitType::Yield:
        return "ExitStmt: YIELD";
    case ExitType::Raise:
        return "ExitStmt: RAISE";
    case ExitType::Return:
        return "ExitStmt: RETURN";
    case ExitType::Throw:
        return "ExitStmt: THROW";
    default:
        throw std::runtime_error("Unknown ExitType");
    }
}

} // namespace AbstractSyntaxTree