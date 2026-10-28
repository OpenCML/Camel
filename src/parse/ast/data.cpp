// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <stdexcept>

#include "camel/parse/ast/data.h"
#include "camel/utils/str.h"

namespace camel::parse::ast {

std::string to_string(DataType type) {
    switch (type) {
    case DataType::UnaryExpr:
        return "UnaryExpr";
    case DataType::BinaryExpr:
        return "BinaryExpr";
    case DataType::ReservedExpr:
        return "ReservedExpr";
    case DataType::IfExpr:
        return "IfExpr";
    case DataType::MatchExpr:
        return "MatchExpr";
    case DataType::TypeMatch:
        return "TypeMatch";
    case DataType::TypeCase:
        return "TypeCase";
    case DataType::StaticFor:
        return "StaticFor";
    case DataType::StaticLoop:
        return "StaticLoop";
    case DataType::StaticAccess:
        return "StaticAccess";
    case DataType::LoopExpr:
        return "LoopExpr";
    case DataType::Literal:
        return "Literal";
    case DataType::Array:
        return "List";
    case DataType::Struct:
        return "Struct";
    case DataType::Tuple:
        return "Tuple";
    case DataType::StructComp:
        return "StructComp";
    case DataType::TupleComp:
        return "TupleComp";
    case DataType::ShapeComp:
        return "ShapeComp";
    case DataType::Func:
        return "Func";
    case DataType::Ref:
        return "Ref";
    case DataType::ConstAs:
        return "ConstAs";
    case DataType::Slice:
        return "Slice";
    case DataType::SpreadArg:
        return "SpreadArg";
    default:
        ASSERT(false, "Unknown DataType");
        return "UnknownDataType";
    }
}

std::string to_string(TypeCaseKind kind) {
    switch (kind) {
    case TypeCaseKind::Struct:
        return "struct";
    case TypeCaseKind::Tuple:
        return "tuple";
    case TypeCaseKind::Leaf:
        return "#leaf";
    case TypeCaseKind::Wildcard:
        return "_";
    case TypeCaseKind::Func:
        return "func";
    case TypeCaseKind::Array:
        return "array";
    case TypeCaseKind::Nullable:
        return "nullable";
    case TypeCaseKind::Fallible:
        return "fallible";
    case TypeCaseKind::Map:
        return "map";
    case TypeCaseKind::Set:
        return "set";
    case TypeCaseKind::Identity:
        return "type";
    default:
        ASSERT(false, "Unknown TypeCaseKind");
        return "unknown";
    }
}

std::string to_string(UnaryDataOp op) {
    switch (op) {
    case UnaryDataOp::Not:
        return "!";
    case UnaryDataOp::Neg:
        return "-";
    case UnaryDataOp::Inv:
        return "~";
    default:
        ASSERT(false, "Unknown UnaryDataOp");
        return "UnknownUnaryDataOp";
    }
}

std::string to_string(BinaryDataOp op) {
    switch (op) {
    case BinaryDataOp::Assign:
        return "=";
    case BinaryDataOp::AssignAdd:
        return "+=";
    case BinaryDataOp::AssignSub:
        return "-=";
    case BinaryDataOp::AssignMul:
        return "*=";
    case BinaryDataOp::AssignDiv:
        return "/=";
    case BinaryDataOp::AssignMod:
        return "%=";
    case BinaryDataOp::AssignMat:
        return "@=";
    case BinaryDataOp::AssignExp:
        return "^=";
    case BinaryDataOp::AssignAnd:
        return "&=";
    case BinaryDataOp::AssignOr:
        return "|=";
    case BinaryDataOp::Or:
        return "|";
    case BinaryDataOp::And:
        return "&";
    case BinaryDataOp::LogicalOr:
        return "||";
    case BinaryDataOp::LogicalAnd:
        return "&&";
    case BinaryDataOp::Eq:
        return "==";
    case BinaryDataOp::Neq:
        return "!=";
    case BinaryDataOp::StrictEq:
        return "===";
    case BinaryDataOp::StrictNeq:
        return "!==";
    case BinaryDataOp::Less:
        return "<";
    case BinaryDataOp::LessEq:
        return "<=";
    case BinaryDataOp::Greater:
        return ">";
    case BinaryDataOp::GreaterEq:
        return ">=";
    case BinaryDataOp::Add:
        return "+";
    case BinaryDataOp::Sub:
        return "-";
    case BinaryDataOp::Mul:
        return "*";
    case BinaryDataOp::Div:
        return "/";
    case BinaryDataOp::Mod:
        return "%";
    case BinaryDataOp::Mat:
        return "@";
    case BinaryDataOp::Pow:
        return "^";
    case BinaryDataOp::Index:
        return "[]";
    default:
        ASSERT(false, "Unknown BinaryDataOp");
        return "UnknownBinaryDataOp";
    }
}

std::string to_string(ReservedDataOp op) {
    switch (op) {
    case ReservedDataOp::NullThen:
        return "??";
    case ReservedDataOp::ErrorThen:
        return "!!";
    case ReservedDataOp::Access:
        return ".";
    case ReservedDataOp::Call:
        return "()";
    case ReservedDataOp::Bind:
        return "<>";
    case ReservedDataOp::Instantiate:
        return "::<>";
    case ReservedDataOp::Comp:
        return "..";
    case ReservedDataOp::As:
        return "AS";
    default:
        ASSERT(false, "Unknown ReservedDataOp");
        return "UnknownReservedDataOp";
    }
}

const std::string DataLoad::geneCode() const { return ""; }

const std::string UnaryExprLoad::geneCode() const { return to_string(op_); }

const std::string BinaryExprLoad::geneCode() const { return to_string(op_); }

const std::string ReservedExprLoad::geneCode() const { return to_string(op_); }

const std::string IfExprLoad::geneCode() const { return "if"; }

const std::string MatchExprLoad::geneCode() const { return "match"; }

const std::string TypeCaseLoad::geneCode() const {
    std::string result = "case ";
    for (size_t i = 0; i < patterns_.size(); ++i) {
        if (i > 0) {
            result += " | ";
        }
        const TypeCasePattern &pattern = patterns_[i];
        result += pattern.kind == TypeCaseKind::Identity && pattern.type != nullptr
                      ? pattern.type->load()->geneCode()
                      : to_string(pattern.kind);
    }
    return result + " =>";
}

const std::string TypeCaseLoad::toString() const {
    return "TypeCase(" + geneCode() + ")";
}

const std::string LiteralLoad::geneCode() const { return value_.toString(); }

const std::string ArrayDataLoad::geneCode() const { return "[]"; }

const std::string StructDataLoad::geneCode() const { return "{}"; }

const std::string TupleDataLoad::geneCode() const { return "()"; }

const std::string StructCompLoad::geneCode() const { return "{ static for }"; }

const std::string TupleCompLoad::geneCode() const { return "( static for )"; }

const std::string ShapeCompLoad::geneCode() const { return "T { static for }"; }

const std::string FuncDataLoad::geneCode() const {
    if (ref_.empty()) {
        return "func";
    } else {
        return "func " + ref_.toString();
    }
}

const std::string RefDataLoad::geneCode() const { return ref_.toString(); }

const std::string ConstAsLoad::geneCode() const { return "constas " + ref_.toString(); }

const std::string SliceLoad::geneCode() const { return ":"; }

const std::string EnumPatternLoad::geneCode() const {
    std::string result = ref_.toString();
    result += structShape_ ? "{" : "(";
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (i > 0) {
            result += ",";
        }
        if (structShape_) {
            result += fields_[i].name;
            if (fields_[i].binding != fields_[i].name) {
                result += ":" + fields_[i].binding;
            }
        } else {
            result += fields_[i].binding.empty() ? "_" : fields_[i].binding;
        }
    }
    result += structShape_ ? "}" : ")";
    return result;
}

} // namespace camel::parse::ast

namespace camel::parse::ast {

const std::string LoopExprLoad::toString() const {
    std::string result = "LoopExpr: ";
    for (size_t i = 0; i < state_.size(); ++i) {
        result += (i ? ", " : "") + state_[i].toString();
    }
    if (hasFor()) {
        result += " for " + binder_.toString();
    }
    return result;
}

} // namespace camel::parse::ast
