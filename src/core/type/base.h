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
 * Updated: Oct. 18, 2025
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

enum class TypeCode : uint32_t {
    // internal use (P = 0x0)
    Ref = 0x0'0000000,

    // primitive types (P = 0x1)
    Int32 = 0x1'0000000,
    Int64 = 0x1'0000001,
    Float = 0x1'0000002,
    Double = 0x1'0000003,
    String = 0x1'0000004,
    Bool = 0x1'0000005,
    Char = 0x1'0000006,

    // composed types (P = 0x2)
    Array = 0x2'0000001,
    Tuple = 0x2'0000002,
    Union = 0x2'0000003,
    Struct = 0x2'0000004,
    Function = 0x2'0000005,

    // special types (P = 0x3)
    Any = 0x3'0000000,
    Void = 0x3'0000001,

    // other types (P = 0xF)
    Other = 0xF'0000000,
};

enum class CastSafety {
    Safe = 1,
    Unsafe = -1,
    Forbidden = 0,
};

std::string typeCodeToString(TypeCode code);

extern const signed char primeTypeConvMatrix[7][7];

class Type;
class PrimaryType;
class ComposedType;
class SpecialType;
class FunctionType;

class TupleType;
class ArrayType;
class StructType;

using type_ptr_t = std::shared_ptr<Type>;
using type_vec_t = std::vector<type_ptr_t>;
using type_wptr_t = std::weak_ptr<Type>;

class Data;
using data_ptr_t = std::shared_ptr<Data>;
using data_wptr_t = std::weak_ptr<Data>;
using data_lst_t = std::list<data_ptr_t>;
using data_vec_t = std::vector<data_ptr_t>;
using data_list_t = std::initializer_list<data_ptr_t>;

class Type {
  protected:
    TypeCode code_;

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() = default;

    const TypeCode &code() const;

    bool internal() const;
    bool primary() const;
    bool composed() const;
    bool special() const;
    bool other() const;

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

    static std::shared_ptr<PrimaryType> Int32();
    static std::shared_ptr<PrimaryType> Int64();
    static std::shared_ptr<PrimaryType> Float();
    static std::shared_ptr<PrimaryType> Double();
    static std::shared_ptr<PrimaryType> String();
    static std::shared_ptr<PrimaryType> Bool();
    static std::shared_ptr<PrimaryType> Char();

    static std::shared_ptr<PrimaryType> Int();
    static std::shared_ptr<PrimaryType> Real();
    static std::shared_ptr<PrimaryType> Number();

    static std::shared_ptr<ArrayType> Array(const type_ptr_t &elementType = nullptr);
    static std::shared_ptr<TupleType> Tuple(const type_vec_t &types = {});
    static std::shared_ptr<StructType> Struct();

    static std::shared_ptr<SpecialType> Any();
    static std::shared_ptr<SpecialType> Void();
    static std::shared_ptr<FunctionType> Func();

    static type_ptr_t Ref();
};
