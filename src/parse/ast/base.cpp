// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/base.h"
#include "camel/utils/str.h"

using namespace std;

namespace camel::parse::ast {

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
    case LoadType::TagUse:
        return "TagUse";
    case LoadType::ResourceClause:
        return "ResourceClause";
    default:
        throw std::runtime_error("Unknown LoadType");
    }
}

const string Load::geneCode() const { return ""; }

const string ModuleLoad::geneCode() const { return "module " + ref_.toString(); }

const string ImportLoad::geneCode() const {
    if (path_.empty()) {
        return "import";
    }
    if (refs_.empty() && as_.empty()) {
        return "import " + path_;
    }

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
    } else if (!as_.empty()) {
        code += as_.toString();
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
    return (isRest_ ? string("..")
                     : isConstas_ ? string("constas ")
                                  : isVar_ ? string("var ")
                                           : isConst_ ? string("const ") : "") +
           ref_.toString();
}

} // namespace camel::parse::ast
