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
 * Updated: Sep. 24, 2025
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

enum class TypeCode {
    // primitive types
    Int32 = 0b00'000000,
    Int64 = 0b00'000001,
    Float = 0b00'000010,
    Double = 0b00'000011,
    String = 0b00'000100,
    Bool = 0b00'000101,
    Char = 0b00'000110,
    // structured types
    Set = 0b01'000000,
    Map = 0b01'000001,
    Dict = 0b01'000010,
    List = 0b01'000011,
    Union = 0b01'000100,
    Array = 0b01'000101,
    Tuple = 0b01'000110,
    Vector = 0b01'000111,
    Params = 0b01'001000,
    Tensor = 0b01'001001,
    // special types
    Any = 0b10'000000,
    Void = 0b10'000001,
    Func = 0b10'000010,
    // for internal use
    Ref = 0b11'000000,
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
class StructType;
class SpecialType;

class SetType;
class MapType;
class ListType;
class DictType;
class ArrayType;
class TupleType;
class UnionType;
class VectorType;
class ParamsType;
class TensorType;

using type_ptr_t = std::shared_ptr<Type>;
using type_wptr_t = std::weak_ptr<Type>;

class Data;
using data_ptr_t = std::shared_ptr<Data>;
using data_wptr_t = std::weak_ptr<Data>;
using data_lst_t = std::list<data_ptr_t>;
using data_vec_t = std::vector<data_ptr_t>;
using data_list_t = std::initializer_list<data_ptr_t>;

class Entity;
using entity_ptr_t = std::shared_ptr<Entity>;
using entity_wptr_t = std::weak_ptr<Entity>;
using entity_lst_t = std::list<entity_ptr_t>;
using entity_vec_t = std::vector<entity_ptr_t>;
using entity_list_t = std::initializer_list<entity_ptr_t>;

class Type {
  protected:
    TypeCode code_;

  public:
    Type() = delete;
    Type(TypeCode type);
    virtual ~Type() = default;

    const TypeCode &code() const;

    bool primary() const;
    bool structured() const;
    bool special() const;
    ImplMark implMark() const;

    virtual std::string toString() const;

    virtual bool operator==(const Type &other) const;
    virtual bool operator!=(const Type &other) const;

    bool equals(const type_ptr_t &type) const;

    virtual CastSafety castSafetyTo(const Type &other) const;

    static bool
    castSafetyCheck(const Type &from, const Type &to, CastSafety required = CastSafety::Safe);
    static bool castSafetyCheck(
        const type_ptr_t &from, const type_ptr_t &to, CastSafety required = CastSafety::Safe);

    static type_ptr_t Int32();
    static type_ptr_t Int64();
    static type_ptr_t Float();
    static type_ptr_t Double();
    static type_ptr_t String();
    static type_ptr_t Bool();
    static type_ptr_t Char();

    static type_ptr_t Int();
    static type_ptr_t Real();
    static type_ptr_t Number();

    static type_ptr_t List();

    static type_ptr_t Any();
    static type_ptr_t Void();
    static type_ptr_t Func();

    static type_ptr_t Ref();
};
