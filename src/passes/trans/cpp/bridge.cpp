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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 11, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "bridge.h"

#include "camel/core/data/primary.h"
#include "camel/core/module/module.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <format>
#include <set>
#include <sstream>
#include <string_view>

using namespace camel::core::type;
namespace data = camel::core::data;

namespace {

std::string escapeCppStringLiteral(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(static_cast<char>(ch));
            break;
        }
    }
    return escaped;
}

std::string sanitizeUriForSymbol(std::string_view uri) {
    std::string text;
    text.reserve(uri.size());
    bool lastUnderscore = false;
    for (unsigned char ch : uri) {
        if (std::isalnum(ch)) {
            text.push_back(static_cast<char>(std::tolower(ch)));
            lastUnderscore = false;
            continue;
        }
        if (!lastUnderscore) {
            text.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!text.empty() && text.front() == '_') {
        text.erase(text.begin());
    }
    while (!text.empty() && text.back() == '_') {
        text.pop_back();
    }
    return text.empty() ? "operator" : text;
}

} // namespace

bool isPureScalarTypeForCpp(Type *type) {
    if (!type) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
        return true;
    default:
        return false;
    }
}

bool isBridgeableTypeForCpp(Type *type) {
    if (!type) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::String:
    case TypeCode::Void:
        return true;
    default:
        return false;
    }
}

std::string cppTypeFor(Type *type) {
    switch (type->code()) {
    case TypeCode::Int32:
        return "Int32";
    case TypeCode::Int64:
        return "Int64";
    case TypeCode::Float32:
        return "Float32";
    case TypeCode::Float64:
        return "Float64";
    case TypeCode::Bool:
        return "Bool";
    case TypeCode::String:
        return "String *";
    case TypeCode::Void:
        return "slot_t";
    default:
        return "slot_t";
    }
}

std::optional<std::string> cppLiteralFor(const data::data_ptr_t &value) {
    if (auto intData = std::dynamic_pointer_cast<data::IntData>(value)) {
        return std::to_string(intData->data());
    }
    if (auto longData = std::dynamic_pointer_cast<data::LongData>(value)) {
        return std::to_string(longData->data()) + "LL";
    }
    if (auto floatData = std::dynamic_pointer_cast<data::FloatData>(value)) {
        return std::to_string(floatData->data()) + "f";
    }
    if (auto doubleData = std::dynamic_pointer_cast<data::DoubleData>(value)) {
        return std::to_string(doubleData->data());
    }
    if (auto boolData = std::dynamic_pointer_cast<data::BoolData>(value)) {
        return boolData->data() ? "true" : "false";
    }
    if (auto stringData = std::dynamic_pointer_cast<data::StringData>(value)) {
        return std::format(
            "String::from(\"{}\", mm::autoSpace())",
            escapeCppStringLiteral(stringData->data()));
    }
    return std::nullopt;
}

std::optional<std::string> cppLiteralFor(slot_t slot, Type *type) {
    if (!type) {
        return std::nullopt;
    }
    switch (type->code()) {
    case TypeCode::Int32:
        return std::to_string(camel::core::rtdata::fromSlot<int32_t>(slot));
    case TypeCode::Int64:
        return std::to_string(camel::core::rtdata::fromSlot<int64_t>(slot)) + "LL";
    case TypeCode::Float32:
        return std::to_string(camel::core::rtdata::fromSlot<float>(slot)) + "f";
    case TypeCode::Float64:
        return std::to_string(camel::core::rtdata::fromSlot<double>(slot));
    case TypeCode::Bool:
        return camel::core::rtdata::fromSlot<bool>(slot) ? "true" : "false";
    case TypeCode::String: {
        auto *str = camel::core::rtdata::fromSlot<String *>(slot);
        if (!str) {
            return std::nullopt;
        }
        return std::format(
            "String::from(\"{}\", mm::autoSpace())",
            escapeCppStringLiteral(str->toString()));
    }
    default:
        return std::nullopt;
    }
}

std::string implMarkExpr(ImplMark mark) {
    switch (mark) {
    case ImplMark::Inner:
        return "ImplMark::Inner";
    case ImplMark::Outer:
        return "ImplMark::Outer";
    case ImplMark::Graph:
        return "ImplMark::Graph";
    default:
        throw std::runtime_error("unsupported impl mark for C++ bridge emission");
    }
}

void collectTypeDepsDepthFirst(
    Type *type, std::vector<Type *> &ordered, std::unordered_set<Type *> &visited) {
    if (!type || !visited.insert(type).second) {
        return;
    }

    switch (type->code()) {
    case TypeCode::Array: {
        auto *arrayType = tt::as_ptr<ArrayType>(type);
        collectTypeDepsDepthFirst(arrayType->elemType(), ordered, visited);
        break;
    }
    case TypeCode::Tuple: {
        auto *tupleType = tt::as_ptr<TupleType>(type);
        for (auto *elemType : tupleType->types()) {
            collectTypeDepsDepthFirst(elemType, ordered, visited);
        }
        break;
    }
    case TypeCode::Struct: {
        auto *structType = tt::as_ptr<StructType>(type);
        for (auto *fieldType : structType->types()) {
            collectTypeDepsDepthFirst(fieldType, ordered, visited);
        }
        break;
    }
    case TypeCode::Function: {
        auto *funcType = tt::as_ptr<FunctionType>(type);
        for (auto *withType : funcType->withTypesSpan()) {
            collectTypeDepsDepthFirst(withType, ordered, visited);
        }
        for (auto *normType : funcType->normTypesSpan()) {
            collectTypeDepsDepthFirst(normType, ordered, visited);
        }
        collectTypeDepsDepthFirst(funcType->exitType(), ordered, visited);
        break;
    }
    default:
        break;
    }

    ordered.push_back(type);
}

std::string primitiveTypeExpr(Type *type) {
    switch (type->code()) {
    case TypeCode::Int32:
        return "Type::Int32()";
    case TypeCode::Int64:
        return "Type::Int64()";
    case TypeCode::Float32:
        return "Type::Float32()";
    case TypeCode::Float64:
        return "Type::Float64()";
    case TypeCode::Bool:
        return "Type::Bool()";
    case TypeCode::Byte:
        return "Type::Byte()";
    case TypeCode::Void:
        return "Type::Void()";
    case TypeCode::String:
        return "Type::String()";
    case TypeCode::Ref:
        return "Type::Ref()";
    case TypeCode::Any:
        return "Type::Any()";
    default:
        throw std::runtime_error(std::format("unsupported primitive type '{}'", type->toString()));
    }
}

std::string serializeTypeInitExpr(Type *type, const std::function<std::string(Type *)> &typeRef) {
    if (!type) {
        throw std::runtime_error("null type cannot be serialized for C++ bridge emission");
    }

    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::Byte:
    case TypeCode::Void:
    case TypeCode::String:
    case TypeCode::Ref:
    case TypeCode::Any:
        return primitiveTypeExpr(type);

    case TypeCode::Array: {
        auto *arrayType = tt::as_ptr<ArrayType>(type);
        return std::format("ArrayType::create({})", typeRef(arrayType->elemType()));
    }

    case TypeCode::Tuple: {
        auto *tupleType = tt::as_ptr<TupleType>(type);
        std::ostringstream oss;
        oss << "TupleType::create(std::vector<Type *>{";
        bool first = true;
        for (auto *elemType : tupleType->types()) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << typeRef(elemType);
        }
        oss << "})";
        return oss.str();
    }

    case TypeCode::Struct: {
        auto *structType = tt::as_ptr<StructType>(type);
        std::ostringstream oss;
        oss << "[]() {\n";
        oss << "        StructTypeFactory f;\n";
        for (size_t i = 0; i < structType->size(); ++i) {
            oss << "        f.add(\"" << structType->fieldName(i) << "\", "
                << typeRef(structType->typeAt(i)) << ");\n";
        }
        oss << "        return f.build();\n";
        oss << "    }()";
        return oss.str();
    }

    case TypeCode::Function: {
        auto *funcType = tt::as_ptr<FunctionType>(type);
        std::ostringstream oss;
        oss << "[]() {\n";
        oss << "        FunctionTypeFactory f;\n";
        oss << "        f.setImplMark(" << implMarkExpr(funcType->implMark()) << ");\n";
        oss << "        f.setModifiers(ModifierSet(" << funcType->modifiers().mask << "));\n";
        oss << "        f.setExitType(" << typeRef(funcType->exitType()) << ");\n";
        oss << "        f.setHasCompileInfo(false);\n";
        for (size_t i = 0; i < funcType->withTypesCount(); ++i) {
            oss << "        f.addWithArg(\"\", " << typeRef(funcType->withTypeAt(i)) << ", "
                << (funcType->withIsVarAt(i) ? "true" : "false") << ");\n";
        }
        for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
            oss << "        f.addNormArg(\"\", " << typeRef(funcType->normTypeAt(i)) << ", "
                << (funcType->normIsVarAt(i) ? "true" : "false") << ");\n";
        }
        oss << "        return f.build();\n";
        oss << "    }()";
        return oss.str();
    }

    case TypeCode::Union:
        throw std::runtime_error(
            std::format(
                "UnionType '{}' is not serializable by the C++ bridge yet",
                type->toString()));

    default:
        throw std::runtime_error(
            std::format("type '{}' is not serializable by the C++ bridge yet", type->toString()));
    }
}

std::string emitCppBridgePreamble(const CppBridgePlan &plan, bool includeIostream) {
    std::ostringstream oss;
    auto emitInclude = [&](bool enabled, std::string_view includeLine) {
        if (enabled) {
            oss << includeLine << "\n";
        }
    };

    emitInclude(plan.usesRuntimeBridge, "#include \"camel/passes/trans/cpp/cpp_bridge.h\"");
    emitInclude(true, "#include \"camel/core/rtdata/base.h\"");
    emitInclude(plan.usesStringHeader, "#include \"camel/core/rtdata/string.h\"");
    emitInclude(plan.usesMmNamespace, "#include \"camel/core/mm.h\"");
    emitInclude(
        !plan.cachedTypes.empty() || plan.usesRuntimeBridge,
        "#include \"camel/core/type.h\"");
    emitInclude(plan.usesChronoSupport, "#include <chrono>");
    emitInclude(includeIostream, "#include <iostream>");
    oss << "\n";

    if (plan.usesMmNamespace) {
        oss << "namespace mm = camel::core::mm;\n";
    }
    if (!plan.cachedTypes.empty() || plan.usesRuntimeBridge) {
        oss << "using namespace camel::core::type;\n";
    }
    if (plan.usesRtdataNamespace) {
        oss << "using namespace camel::core::rtdata;\n";
    }
    if (plan.usesRuntimeBridge) {
        oss << "using namespace camel::execute::cppbridge;\n";
    }
    if (plan.usesRtdataNamespace || plan.usesMmNamespace || plan.usesRuntimeBridge ||
        !plan.cachedTypes.empty()) {
        oss << "\n";
    }
    return oss.str();
}

std::string emitCppBridgeSupport(const CppBridgePlan &plan) {
    if (!plan.usesRuntimeBridge && plan.cachedTypes.empty()) {
        return "";
    }

    std::ostringstream oss;
    if (!plan.cachedTypes.empty()) {
        auto typeRef = [&](Type *type) { return cppBridgeTypeExprFor(plan, type); };
        for (const auto &entry : plan.cachedTypes) {
            oss << "inline Type *" << entry.name << "() {\n";
            oss << "    static Type *type = " << serializeTypeInitExpr(entry.type, typeRef)
                << ";\n";
            oss << "    return type;\n";
            oss << "}\n\n";
        }
    }

    if (!plan.usesRuntimeBridge) {
        return oss.str();
    }

    for (const auto &binding : plan.runtimeOperators) {
        oss << "\n";
        oss << "inline BoundOperator &" << binding.name << "() {\n";
        oss << "    static auto &op = bridge().bind(\"" << binding.uri << "\");\n";
        oss << "    return op;\n";
        oss << "}\n";
    }
    oss << "\n";
    return oss.str();
}

std::string cppBridgeAccessorNameForUri(std::string_view uri) {
    return "o_" + sanitizeUriForSymbol(uri);
}

bool isAtomicTypeForCpp(Type *type) {
    if (!type) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::Byte:
    case TypeCode::Void:
    case TypeCode::String:
    case TypeCode::Ref:
    case TypeCode::Any:
        return true;
    default:
        return false;
    }
}

std::string cppBridgeTypeExprFor(const CppBridgePlan &plan, Type *type) {
    if (isAtomicTypeForCpp(type)) {
        return primitiveTypeExpr(type);
    }
    return cppBridgeTypeAccessorName(plan, type) + "()";
}

std::string cppBridgeTypeAccessorName(const CppBridgePlan &plan, Type *type) {
    for (const auto &entry : plan.cachedTypes) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    throw std::runtime_error(
        std::format("missing cached type binding for '{}'", type ? type->toString() : "null"));
}

std::string emitCppBridgeInitializeCall(const CppBridgePlan &plan) {
    if (!plan.usesRuntimeBridge) {
        return "";
    }
    std::ostringstream oss;
    oss << "    initialize();\n";
    if (!plan.importedModules.empty()) {
        oss << "    importModules({";
        for (size_t i = 0; i < plan.importedModules.size(); ++i) {
            if (i != 0) {
                oss << ", ";
            }
            oss << std::format("\"{}\"", plan.importedModules[i]);
        }
        oss << "});\n";
    }
    return oss.str();
}
