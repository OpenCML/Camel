/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 9, 2024
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
class PrimType;
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

class Function;
using func_ptr_t = std::shared_ptr<Function>;

class Entity;
using entity_ptr_t = std::shared_ptr<Entity>;
using entity_wptr_t = std::weak_ptr<Entity>;
using entity_lst_t = std::list<entity_ptr_t>;
using entity_vec_t = std::vector<entity_ptr_t>;
using entity_list_t = std::initializer_list<entity_ptr_t>;

class Functor;
using functor_ptr_t = std::shared_ptr<Functor>;
using functor_wptr_t = std::weak_ptr<Functor>;
using functor_lst_t = std::list<functor_ptr_t>;
using functor_vec_t = std::vector<functor_ptr_t>;
using functor_list_t = std::initializer_list<functor_ptr_t>;

enum class FunctionModifier;

class Type {
  protected:
    TypeCode code_;

  public:
    Type() = delete;
    Type(TypeCode type) : code_(type) {}
    virtual ~Type() = default;

    const TypeCode &code() const { return code_; }

    bool primitive() const { return (static_cast<int>(code_) & 0b11'000000) == 0b00'000000; }
    bool structured() const { return (static_cast<int>(code_) & 0b11'000000) == 0b01'000000; }
    bool special() const { return (static_cast<int>(code_) & 0b11'000000) == 0b10'000000; }

    virtual std::string toString() const { return typeCodeToString(code_); }

    virtual bool operator==(const Type &other) const { return code_ == other.code_; }
    virtual bool operator!=(const Type &other) const { return code_ != other.code_; }

    bool equals(const type_ptr_t &type) const { return type && *type == *this; }

    virtual TypeConv convertibility(const Type &other) const {
        if (code_ == other.code_) {
            return TypeConv::SAFE;
        }
        return TypeConv::FORBIDDEN;
    }
};

class PrimType : public Type {
  public:
    PrimType() = delete;
    PrimType(TypeCode code) : Type(code) {}

    TypeConv convertibility(const Type &other) const override;
};

class StructType : public Type {
  public:
    StructType() = delete;
    StructType(TypeCode code) : Type(code) {}

    virtual std::string toString() const override = 0;

    virtual bool operator==(const Type &other) const override = 0;
    virtual bool operator!=(const Type &other) const override = 0;

    virtual TypeConv convertibility(const Type &other) const override = 0;
};

class SpecialType : public Type {
  public:
    SpecialType() = delete;
    SpecialType(TypeCode code) : Type(code) {}

    virtual std::string toString() const override { return typeCodeToString(code_); }

    virtual bool operator==(const Type &other) const override { return code_ == other.code(); }
    virtual bool operator!=(const Type &other) const override { return code_ != other.code(); }

    virtual TypeConv convertibility(const Type &other) const override;
};

class SetType : public StructType {
  private:
    type_ptr_t valueType_;

  public:
    SetType() = delete;
    SetType(const type_ptr_t &valueType);

    type_ptr_t valueType() const;

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class MapType : public StructType {
  private:
    type_ptr_t keyType_;
    type_ptr_t valueType_;

  public:
    MapType() = delete;
    MapType(const type_ptr_t &keyType, const type_ptr_t &valueType);

    type_ptr_t keyType() const { return keyType_; }
    type_ptr_t valueType() const { return valueType_; }

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class DictType : public StructType {
  private:
    // field name -> field type with default value
    std::unordered_map<std::string, type_ptr_t> fields_;

  public:
    DictType();

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    bool add(const std::string &name, const type_ptr_t &type);
    bool del(const std::string &name);
    bool has(const std::string &name) const;
    void set(const std::string &name, const type_ptr_t &type);
    type_ptr_t get(const std::string &name) const;

    void clear();

    type_ptr_t operator|(const DictType &other) const;
    type_ptr_t operator&(const DictType &other) const;

    TypeConv convertibility(const Type &other) const override;
};

class ListType : public StructType {
  public:
    ListType() : StructType(TypeCode::LIST) {}

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class UnionType : public StructType {
  private:
    std::unordered_set<type_ptr_t> types_;

    void insertUnion(const UnionType &other);

  public:
    UnionType();
    UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs);
    UnionType(const std::initializer_list<type_ptr_t> &types);
    UnionType(const std::vector<type_ptr_t> &types);

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    void add(const type_ptr_t &type);
    bool has(const type_ptr_t &type) const;

    TypeConv convertibility(const Type &other) const override;
};

class ArrayType : public StructType {
  private:
    size_t size_;
    type_ptr_t elementType_;

  public:
    ArrayType() = delete;
    ArrayType(const type_ptr_t &elementType, size_t size);

    size_t size() const;
    type_ptr_t elementType() const;

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class TupleType : public StructType {
  private:
    std::vector<type_ptr_t> types_;

  public:
    TupleType();
    TupleType(const std::initializer_list<type_ptr_t> &types);
    TupleType(const std::vector<type_ptr_t> &types);

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    void add(const type_ptr_t &type);
    void set(size_t index, const type_ptr_t &type);

    size_t size() const;

    const std::vector<type_ptr_t> &types() const;

    TypeConv convertibility(const Type &other) const override;
};

class VectorType : public StructType {
  private:
    type_ptr_t elementType_;

  public:
    VectorType(const type_ptr_t &elementType);

    type_ptr_t elementType() const;

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class ParamsType : public StructType {
  private:
    std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> elements_;

    TypeConv convertibilityToParams(const ParamsType &other) const;

  public:
    ParamsType();

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    bool add(const std::string &key, const type_ptr_t &type, const data_ptr_t &value = nullptr);

    size_t size() const;
    const std::vector<std::tuple<std::string, type_ptr_t, data_ptr_t>> &elements() const;

    std::map<std::string, type_ptr_t> map() const;

    void clear();

    TypeConv convertibility(const Type &other) const override;
};

class TensorType : public StructType {
  private:
    type_ptr_t elementType_;
    std::vector<size_t> shape_;

  public:
    TensorType(const type_ptr_t &elementType, const std::vector<size_t> &shape);

    std::vector<size_t> shape() const { return shape_; }
    type_ptr_t elementType() const { return elementType_; }

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

class FunctorType : public SpecialType {
  private:
    std::unordered_set<FunctionModifier> modifiers_;
    std::shared_ptr<ParamsType> withType_;
    std::shared_ptr<ParamsType> paramsType_;
    std::unordered_set<std::string> innerIdents_;
    bool hasSideEffect_ = false;
    type_ptr_t returnType_;

  public:
    FunctorType() = delete;
    FunctorType(const std::shared_ptr<ParamsType> &withType = nullptr,
                const std::shared_ptr<ParamsType> &paramsType = nullptr, const type_ptr_t &returnType = nullptr);

    void addModifier(FunctionModifier modifier);
    void setModifiers(const std::unordered_set<FunctionModifier> &modifiers);

    bool addIdent(const std::string &ident);
    bool hasSideEffect() const;
    const std::unordered_set<std::string> &innerIdents() const;

    type_ptr_t withType() const;
    type_ptr_t paramsType() const;
    type_ptr_t returnType() const;

    std::string toString() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    TypeConv convertibility(const Type &other) const override;
};

extern type_ptr_t int32TypePtr;
extern type_ptr_t int64TypePtr;
extern type_ptr_t floatTypePtr;
extern type_ptr_t doubleTypePtr;
extern type_ptr_t stringTypePtr;
extern type_ptr_t boolTypePtr;
extern type_ptr_t charTypePtr;

extern type_ptr_t intTypePtr;
extern type_ptr_t realTypePtr;
extern type_ptr_t numberTypePtr;

extern type_ptr_t listTypePtr;

extern type_ptr_t anyTypePtr;
extern type_ptr_t voidTypePtr;
extern type_ptr_t functorTypePtr;

extern type_ptr_t refTypePtr;

void initTypes();