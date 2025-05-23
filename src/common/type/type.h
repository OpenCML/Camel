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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class TypeCode {
    // primitive types
    INT32 = 0b00'000000,
    INT64 = 0b00'000001,
    FLOAT = 0b00'000010,
    DOUBLE = 0b00'000011,
    STRING = 0b00'000100,
    BOOL = 0b00'000101,
    CHAR = 0b00'000110,
    // structured types
    SET = 0b01'000000,
    MAP = 0b01'000001,
    DICT = 0b01'000010,
    LIST = 0b01'000011,
    UNION = 0b01'000100,
    ARRAY = 0b01'000101,
    TUPLE = 0b01'000110,
    VECTOR = 0b01'000111,
    PARAMS = 0b01'001000,
    TENSOR = 0b01'001001,
    // special types
    ANY = 0b10'000000,
    VOID = 0b10'000001,
    FUNCTOR = 0b10'000010,
    // for internal use
    REF = 0b11'000000,
};

enum class TypeConv {
    SAFE = 1,
    UNSAFE = -1,
    FORBIDDEN = 0,
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

    virtual std::string toString() const;

    virtual bool operator==(const Type &other) const;
    virtual bool operator!=(const Type &other) const;

    bool equals(const type_ptr_t &type) const;

    virtual TypeConv convertibility(const Type &other) const;
};
