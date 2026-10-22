// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/type.h"
#include "camel/utils/str.h"

namespace camel::parse::ast {

namespace {

/// One `[extent, ...]` as it is written: `[]` for the unknown dimension, `?` for an unknown
/// extent next to others, `..name` for a pack (design principles 5.4).
std::string dimGroupCode(const DimGroup &group) {
    if (group.empty()) {
        return "[]";
    }
    std::string result = "[";
    bool first         = true;
    for (const node_ptr_t &extent : group.extents) {
        if (!first) {
            result += ", ";
        }
        first = false;
        result += extent ? extent->load()->geneCode() : "?";
    }
    if (group.pack != DimPack::None) {
        if (!first) {
            result += ", ";
        }
        result += group.pack == DimPack::Named ? ".." + group.packName : "..";
    }
    return result + "]";
}

} // namespace

std::string to_string(TypeType type) {
    switch (type) {
    case TypeType::Null:
        return "Null";
    case TypeType::Expr:
        return "Expr";
    case TypeType::Array:
        return "List";
    case TypeType::ArrayDim:
        return "ArrayDim";
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

const std::string ResourceClauseLoad::toString() const {
    if (pure_) {
        return "ResourceClause: pure";
    }
    std::string result = "ResourceClause: ";
    for (size_t i = 0; i < uses_.size(); ++i) {
        result += (i > 0 ? ", " : "") + uses_[i].toString();
    }
    return result;
}

const std::string ResourceClauseLoad::geneCode() const {
    if (pure_) {
        return "pure";
    }
    std::string result = "uses ";
    for (size_t i = 0; i < uses_.size(); ++i) {
        result += (i > 0 ? ", " : "") + uses_[i].toString();
    }
    return result;
}

const std::string FuncTypeLoad::toString() const {
    std::string result = "FuncType:";
    if (!modifiers_.empty()) {
        result += " " + std::string(modifiers_);
    }
    if (!uri_.empty()) {
        result += " [" + uri_ + "]";
    }
    return result;
}

const std::string NullableTypeLoad::geneCode() const { return fallible_ ? "!" : "?"; }

const std::string TypeExprLoad::geneCode() const { return to_string(op_); }

const std::string SpecTypeLoad::geneCode() const { return "<>"; }

const std::string ArrayTypeLoad::geneCode() const {
    std::string result = "";
    for (const DimGroup &group : dims_) {
        result += dimGroupCode(group);
    }
    return result;
}

const std::string ArrayDimLoad::geneCode() const { return dimGroupCode(dim_); }

const std::string StructTypeLoad::geneCode() const { return "{}"; }

const std::string EnumTypeLoad::geneCode() const {
    std::string result = "enum {";
    for (size_t i = 0; i < variants_.size(); ++i) {
        if (i > 0) {
            result += ",";
        }
        result += variants_[i].name;
    }
    return result + "}";
}

const std::string TupleTypeLoad::geneCode() const { return "()"; }

const std::string FuncTypeLoad::geneCode() const {
    std::string result = "";
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

} // namespace camel::parse::ast
