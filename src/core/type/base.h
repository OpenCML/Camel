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
 * Updated: Nov. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

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
    OtherType = 1u << 31, // 第三方类型，由第三方库在运行时注册实际id
    Composite = 1u << 30, // 组合类型，由多个类型组合而成
    GC_Traced = 1u << 29, // GC 跟踪类型，其数据对象会持有由 GC 管理的内存块引用
    Auxiliary = 1u << 28  // 辅助类型，参与类型解算但无实际数据对象
};

constexpr TypeFlag operator|(TypeFlag a, TypeFlag b) {
    return static_cast<TypeFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr uint32_t makeTypeCode(TypeFlag flags, uint32_t id) {
    return static_cast<uint32_t>(flags) | (id & 0x0FFFFFFF); // 低 28 位存 id
}

enum class TypeCode : uint32_t {
    Int      = makeTypeCode(TypeFlag::Primitive, 0x000),
    Long     = makeTypeCode(TypeFlag::Primitive, 0x001),
    Float    = makeTypeCode(TypeFlag::Primitive, 0x002),
    Double   = makeTypeCode(TypeFlag::Primitive, 0x003),
    Bool     = makeTypeCode(TypeFlag::Primitive, 0x004),
    Byte     = makeTypeCode(TypeFlag::Primitive, 0x005),
    Void     = makeTypeCode(TypeFlag::Primitive, 0x006),
    String   = makeTypeCode(TypeFlag::Primitive | TypeFlag::GC_Traced, 0x007),
    Array    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x008),
    Tuple    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x009),
    Struct   = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00A),
    Function = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00B),
    Frame    = makeTypeCode(TypeFlag::Composite | TypeFlag::GC_Traced, 0x00C),
    Ref      = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00D),
    Any      = makeTypeCode(TypeFlag::Primitive | TypeFlag::Auxiliary, 0x00E),
    Union    = makeTypeCode(TypeFlag::Composite | TypeFlag::Auxiliary, 0x00F),
    Other    = makeTypeCode(TypeFlag::OtherType, 0x000),
};

inline constexpr bool hasFlag(uint32_t code, TypeFlag flag) {
    return (code & static_cast<uint32_t>(flag)) != 0;
}

// !注意，isPrimitive 意味着
// !isComposite && !isOtherType && !isAuxiliary
template <typename T> inline constexpr bool isPrimitive(T code) {
    // Primitive 类型没有任何标志位，即前四位均为 0
    return (static_cast<uint32_t>(code) & 0xF0000000) == 0;
}

template <typename T> inline constexpr bool isOtherType(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::OtherType);
}

template <typename T> inline constexpr bool isComposite(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::Composite);
}

template <typename T> inline constexpr bool isGCTraced(T code) {
    return hasFlag(static_cast<uint32_t>(code), TypeFlag::GC_Traced);
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
using type_ptr_t  = std::shared_ptr<Type>;
using type_vec_t  = std::vector<type_ptr_t>;
using type_wptr_t = std::weak_ptr<Type>;

class Type {
  protected:
    TypeCode code_;

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() noexcept = default;

    const TypeCode &code() const;

    virtual std::string toString() const;
    virtual std::string mangle() const;
    virtual type_ptr_t clone() const;

    virtual bool equals(const type_ptr_t &type) const;
    virtual CastSafety castSafetyTo(const Type &other) const;
    virtual bool assignable(const type_ptr_t &type) const;

    bool operator==(const type_ptr_t &other) const = delete;
    bool operator!=(const type_ptr_t &other) const = delete;

    static type_ptr_t Int();
    static type_ptr_t Long();
    static type_ptr_t Float();
    static type_ptr_t Double();
    static type_ptr_t Bool();
    static type_ptr_t Byte();
    static type_ptr_t Void();
    static type_ptr_t String();
    static type_ptr_t Ref();
    static type_ptr_t Any();
};
