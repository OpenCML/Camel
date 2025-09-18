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

#include "base.h"
#include "utils/str.h"

using namespace std;

namespace AbstractSyntaxTree {

std::string to_string(LoadType type) {
    switch (type) {
    case LoadType::Module:
        return "Module";
    case LoadType::Import:
        return "Import";
    case LoadType::Export:
        return "Export";
    case LoadType::Stmt:
        return "Stmt";
    case LoadType::Data:
        return "Data";
    case LoadType::Type:
        return "Type";
    case LoadType::NamedData:
        return "NamedData";
    case LoadType::NamedType:
        return "NamedType";
    case LoadType::NamedPair:
        return "NamedPair";
    case LoadType::Repeated:
        return "Repeated";
    case LoadType::Optional:
        return "Optional";
    default:
        throw std::runtime_error("Unknown LoadType");
    }
}

const string Load::geneCode() const { return ""; }

const string ModuleLoad::geneCode() const { return "module " + ref_.toString(); }

const string ImportLoad::geneCode() const {
    string code = "import ";
    if (!refs_.empty()) {
        if (refs_.size() == 1) {
            code += refs_[0].toString();
        } else {
            code += "{ ";
            for (auto ref : refs_) {
                code += ref.toString() + ", ";
            }
            if (code.ends_with(", ")) {
                code = code.substr(0, code.size() - 2);
            }
            code += " }";
        }
    }
    code += " from \"" + path_ + "\"";
    return code;
}

const string ExportLoad::geneCode() const {
    string code = "export ";
    if (!refs_.empty()) {
        if (refs_.size() == 1) {
            code += refs_[0].toString();
        } else {
            code += "{ ";
            for (auto ref : refs_) {
                code += ref.toString() + ", ";
            }
            if (code.ends_with(", ")) {
                code = code.substr(0, code.size() - 2);
            }
            code += " }";
        }
    }
    return code;
}

const string NamedDataLoad::geneCode() const { return ref_.toString(); }

const string NamedTypeLoad::geneCode() const {
    return (isVar_ ? string("var ") : "") + ref_.toString();
}

const string NamedPairLoad::geneCode() const {
    return (isVar_ ? string("var ") : "") + ref_.toString();
}

} // namespace AbstractSyntaxTree