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
 * Created: Oct. 06, 2024
 * Updated: Nov. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "composed/array.h"
#include "composed/struct.h"
#include "composed/tuple.h"
#include "other/other.h"
#include "primary.h"
#include "special/func.h"
#include "utils/type.h"

using namespace std;

const signed char primitiveTypeConvMatrix[7][7] = {
    // Int, Long, Float, Double, Bool, Byte, Void
    {01, 01, 01, 01, 01, -1, -1}, // Int
    {-1, 01, -1, 01, 01, -1, -1}, // Long
    {-1, -1, 01, 01, 01, -1, -1}, // Float
    {-1, -1, -1, 01, 01, -1, -1}, // Double
    {01, 01, 01, 01, 01, 01, -1}, // Bool
    {01, 01, 01, 01, 01, 01, -1}, // Byte
    {00, 00, 00, 00, 00, 00, 01}  // Void
};

string typeCodeToString(TypeCode code) {
    switch (code) {
    case TypeCode::Int:
        return "int";
    case TypeCode::Long:
        return "long";
    case TypeCode::Float:
        return "float";
    case TypeCode::Double:
        return "double";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Byte:
        return "byte";
    case TypeCode::Void:
        return "void";
    case TypeCode::String:
        return "string";
    case TypeCode::Array:
        return "array";
    case TypeCode::Tuple:
        return "tuple";
    case TypeCode::Struct:
        return "struct";
    case TypeCode::Function:
        return "function";
    case TypeCode::Frame:
        return "frame";
    case TypeCode::Ref:
        return "ref";
    case TypeCode::Any:
        return "any";
    case TypeCode::Union:
        return "union";
    default:
        return isOtherType(code) ? "other" : "unknown";
    }
}

Type::Type(TypeCode type) : code_(type) {}

const TypeCode &Type::code() const { return code_; }

std::string Type::toString() const { return typeCodeToString(code_); }

std::string Type::mangle() const {
    ASSERT(false, "Type::mangle() not implemented");
    return "";
}

bool Type::operator==(const Type &other) const {
    if (this == &other) {
        return true;
    }
    return code_ == other.code();
}

bool Type::operator!=(const Type &other) const { return !(*this == other); }

type_ptr_t Type::clone() const {
    ASSERT(false, "Type::clone() not implemented");
    return nullptr;
}

bool Type::equals(const type_ptr_t &type) const { return type && *type == *this; }

bool Type::assignable(const type_ptr_t &type) const {
    if (!type)
        return false;
    if (this == type.get())
        return true;

    // 内置类型，含有 Ref 类型的复合类型必须 resolve 后才能赋值给其他类型
    ASSERT(code_ != TypeCode::Ref && type->code_ != TypeCode::Ref, "Ref type cannot be assigned");

    // any 类型可以接受任何类型的赋值，但不可以赋值给其他类型
    if (type->code_ == TypeCode::Any)
        return true;
    if (code_ == TypeCode::Any)
        return false;

    // void 类型不能赋值给任何类型，任何类型也不能赋值给 void 类型
    if (code_ == TypeCode::Void || type->code_ == TypeCode::Void)
        return false;

    if (code_ == TypeCode::Function && type->code_ == TypeCode::Function) {
        // TODO: 这里需要进一步设计
        return true;
    }

    // 复合类型目前需要完全相等才能赋值
    if (isComposite(code_) && isComposite(type->code_)) {
        return this->equals(type);
    }

    // 第三方类型交由第三方自己重载的 assignable 方法处理
    if (isOtherType(code_)) {
        const auto &self = static_cast<const OtherType &>(*this);
        return self.assignable(type);
    }

    // 剩余情况只能是基础类型之间的赋值，必须相同
    return code_ == type->code_;
}

CastSafety Type::castSafetyTo(const Type &other) const {
    if (code_ == other.code_) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool Type::castSafetyCheck(const Type &from, const Type &to, CastSafety required) {
    CastSafety safety = from.castSafetyTo(to);
    if (required == CastSafety::Safe) {
        return safety == CastSafety::Safe;
    } else if (required == CastSafety::Unsafe) {
        return safety != CastSafety::Forbidden;
    } else { // required == CastSafety::Forbidden
        return safety == CastSafety::Forbidden;
    }
}

bool Type::castSafetyCheck(const type_ptr_t &from, const type_ptr_t &to, CastSafety required) {
    return castSafetyCheck(*from, *to, required);
}

std::shared_ptr<PrimaryType> Type::Int() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Int);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::Long() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Long);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::Float() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Float);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::Double() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Double);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::String() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::String);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::Bool() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Bool);
    }
    return type;
}

std::shared_ptr<PrimaryType> Type::Byte() {
    static std::shared_ptr<PrimaryType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<PrimaryType>(TypeCode::Byte);
    }
    return type;
}

std::shared_ptr<ArrayType> Type::Array(const type_ptr_t &elementType) {
    static std::shared_ptr<ArrayType> voidArrayType = nullptr;
    if (!elementType) {
        if (voidArrayType == nullptr) {
            voidArrayType = std::make_shared<ArrayType>(Type::Void());
        }
        return voidArrayType;
    }
    return std::make_shared<ArrayType>(elementType);
}

std::shared_ptr<TupleType> Type::Tuple(const type_vec_t &types) {
    return make_shared<TupleType>(types);
}

std::shared_ptr<StructType> Type::Struct() { return make_shared<StructType>(); }

std::shared_ptr<SpecialType> Type::Any() {
    static std::shared_ptr<SpecialType> type = nullptr;
    if (type == nullptr) {
        type = make_shared<SpecialType>(TypeCode::Any);
    }
    return type;
}

std::shared_ptr<FunctionType> Type::Func() {
    static std::shared_ptr<FunctionType> type = nullptr;
    if (type == nullptr) {
        type =
            make_shared<FunctionType>(param_vec_t{}, param_vec_t{}, Type::Void(), Modifier::None);
    }
    return type;
}

type_ptr_t Type::Ref() {
    static type_ptr_t type = nullptr;
    if (type == nullptr) {
        type = make_shared<Type>(TypeCode::Ref);
    }
    return type;
}

std::shared_ptr<SpecialType> Type::Void() {
    static std::shared_ptr<SpecialType> type = nullptr;
    if (type == nullptr) {
        type = std::make_shared<SpecialType>(TypeCode::Void);
    }
    return type;
}
