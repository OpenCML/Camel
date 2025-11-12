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
 * Updated: Nov. 12, 2025
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
    code += " from " + path_;
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

void AbstractSyntaxTree::Load::setTokenRange(size_t start, size_t end) {
    tokenRange_.start = start;
    tokenRange_.end = end;
}

const Reference &ModuleLoad::getRef() const {
    if (ref_.empty()) {
        throw std::runtime_error("ModuleLoad: Reference is not set");
    }
    return ref_;
}

const std::string &ImportLoad::getPath() const {
    if (path_.empty()) {
        throw std::runtime_error("ImportLoad: Path is not set");
    }
    return path_;
}

std::vector<Reference> &ImportLoad::getRefs() {
    if (refs_.empty()) {
        throw std::runtime_error("ImportLoad: References are not set");
    }
    return refs_;
}

const Reference &ImportLoad::getAs() const {
    if (as_.empty()) {
        throw std::runtime_error("ImportLoad: 'as' Reference is not set");
    }
    return as_;
}

std::vector<Reference> &ExportLoad::getRefs() {
    if (refs_.empty()) {
        throw std::runtime_error("ExportLoad: References are not set");
    }
    return refs_;
}

void ExportLoad::addRef(const Reference &ref) {
    if (ref.empty()) {
        throw std::runtime_error("ExportLoad: Cannot add null Reference");
    }
    refs_.push_back(ref);
}

} // namespace AbstractSyntaxTree
