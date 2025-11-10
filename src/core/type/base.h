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

#pragma once

#include <any>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../impl.h"
#include "utils/assert.h"

/*
 TypeCode (32 bits) 编码结构:

 bit31      bit30      bit29      bit28      bit27 ............... bit0
 +----------+----------+----------+----------+------------------------+
 |OtherType |Composite |GC_Traced |Auxiliary |      ID (28 bits)      |
 +----------+----------+----------+----------+------------------------+
 ^          ^          ^          ^          ^
 |          |          |          |          └─ 类内唯一编号 (0x0000000 ~ 0x0FFFFFFF)
 |          |          |          └─ bit28: 辅助类型（无实际数据存储）
 |          |          └─ bit29: 是否为 GC 跟踪类型
 |          └─ bit30: 是否为组合类型（Struct / Array / Tuple / ...）
 └─ bit31: 是否为第三方类型（非内置类型）

 说明:
 - 高位标志位可组合使用，例如 Composite | GC_Traced
 - ID 用于区分具体类型编号，低 28 位有效
*/

enum class TypeFlag : uint32_t {
    Primitive = 0,        // 内置原始类型，既非组合类型，也非 GC 跟踪类型
    OtherType = 1u << 31, // 第三方类型
    Composite = 1u << 30, // 组合类型
    GC_Traced = 1u << 29, // GC 跟踪类型
    Auxiliary = 1u << 28  // 辅助类型，无实际数据存储
};

constexpr TypeFlag operator|(TypeFlag a, TypeFlag b) {
    return static_cast<TypeFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t makeTypeCode(TypeFlag flags, uint32_t id) {
    return static_cast<uint32_t>(flags) | (id & 0x0FFFFFFF); // 低 28 位存 id
}

enum class TypeCode : uint32_t {
    // Primitive data types
    Int    = makeTypeCode(TypeFlag::Primitive, 0x000),
    Long   = makeTypeCode(TypeFlag::Primitive, 0x001),
    Float  = makeTypeCode(TypeFlag::Primitive, 0x002),
    Double = makeTypeCode(TypeFlag::Primitive, 0x003),
    Bool   = makeTypeCode(TypeFlag::Primitive, 0x004),
    Byte   = makeTypeCode(TypeFlag::Primitive, 0x005),
    Void   = makeTypeCode(TypeFlag::Primitive, 0x006),
    // GC Traced data types
    String   = makeTypeCode(TypeFlag::GC_Traced, 0x007),
    Array    = makeTypeCode(TypeFlag::GC_Traced | TypeFlag::Composite, 0x008),
    Tuple    = makeTypeCode(TypeFlag::GC_Traced | TypeFlag::Composite, 0x009),
    Struct   = makeTypeCode(TypeFlag::GC_Traced | TypeFlag::Composite, 0x00A),
    Function = makeTypeCode(TypeFlag::GC_Traced | TypeFlag::Composite, 0x00B),
    Frame    = makeTypeCode(TypeFlag::GC_Traced | TypeFlag::Composite, 0x00C),
    // Auxiliary types (no actual data storage)
    Ref   = makeTypeCode(TypeFlag::Auxiliary, 0x00D),
    Any   = makeTypeCode(TypeFlag::Auxiliary, 0x00E),
    Union = makeTypeCode(TypeFlag::Auxiliary | TypeFlag::Composite, 0x00F),
    // Third-party types
    Other = makeTypeCode(TypeFlag::OtherType, 0x000),
};

constexpr bool hasFlag(uint32_t code, TypeFlag flag) {
    return (code & static_cast<uint32_t>(flag)) != 0;
}

inline constexpr bool isPrimitive(uint32_t code) {
    // Primitive 类型没有任何标志位，即前四位均为 0
    return (code & 0xF0000000) == 0;
}

inline constexpr bool isOtherType(uint32_t code) { return hasFlag(code, TypeFlag::OtherType); }

inline constexpr bool isComposite(uint32_t code) { return hasFlag(code, TypeFlag::Composite); }

inline constexpr bool isGCTraced(uint32_t code) { return hasFlag(code, TypeFlag::GC_Traced); }

inline constexpr bool isAuxiliary(uint32_t code) { return hasFlag(code, TypeFlag::Auxiliary); }

template <typename T> inline constexpr bool isComposite(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Composite);
}

template <typename T> inline constexpr bool isGCTraced(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::GC_Traced);
}

template <typename T> inline constexpr bool isOtherType(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::OtherType);
}

template <typename T> inline constexpr bool isAuxiliary(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Auxiliary);
}

enum class CastSafety {
    Safe      = 1,
    Unsafe    = -1,
    Forbidden = 0,
};

std::string typeCodeToString(TypeCode code);

extern const signed char primitiveTypeConvMatrix[7][7];

class Type;
class PrimaryType;
class ComposedType;
class SpecialType;
class FunctionType;

class TupleType;
class ArrayType;
class StructType;

using type_ptr_t  = std::shared_ptr<Type>;
using type_vec_t  = std::vector<type_ptr_t>;
using type_wptr_t = std::weak_ptr<Type>;

class Data;
using data_ptr_t  = std::shared_ptr<Data>;
using data_wptr_t = std::weak_ptr<Data>;
using data_lst_t  = std::list<data_ptr_t>;
using data_vec_t  = std::vector<data_ptr_t>;
using data_list_t = std::initializer_list<data_ptr_t>;

class Type {
  protected:
    TypeCode code_;

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() = default;

    const TypeCode &code() const;

    virtual std::string toString() const;
    virtual std::string mangle() const;

    virtual bool operator==(const Type &other) const;
    virtual bool operator!=(const Type &other) const;

    virtual type_ptr_t clone() const;

    bool equals(const type_ptr_t &type) const;
    bool assignable(const type_ptr_t &type) const;

    virtual CastSafety castSafetyTo(const Type &other) const;

    static bool
    castSafetyCheck(const Type &from, const Type &to, CastSafety required = CastSafety::Safe);
    static bool castSafetyCheck(
        const type_ptr_t &from, const type_ptr_t &to, CastSafety required = CastSafety::Safe);

    static std::shared_ptr<PrimaryType> Int();
    static std::shared_ptr<PrimaryType> Long();
    static std::shared_ptr<PrimaryType> Float();
    static std::shared_ptr<PrimaryType> Double();
    static std::shared_ptr<PrimaryType> String();
    static std::shared_ptr<PrimaryType> Bool();
    static std::shared_ptr<PrimaryType> Byte();
    static std::shared_ptr<SpecialType> Void();

    static std::shared_ptr<ArrayType> Array(const type_ptr_t &elementType = nullptr);
    static std::shared_ptr<TupleType> Tuple(const type_vec_t &types = {});
    static std::shared_ptr<StructType> Struct();
    static std::shared_ptr<FunctionType> Func();

    static type_ptr_t Ref();
    static std::shared_ptr<SpecialType> Any();
};
