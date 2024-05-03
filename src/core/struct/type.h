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

#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "value.h"
#include <set>

template <typename T> T parseNumber(const std::string &input) {
    bool isNegative = false;
    T result = 0;
    int base = 10;
    int exponent = 0;
    size_t i = 0;

    // Handle sign
    if (input[0] == '-') {
        isNegative = true;
        i++;
    } else if (input[0] == '+') {
        i++;
    }

    // Handle different bases
    if (input.substr(i, 2) == "0x") {
        base = 16;
        i += 2;
    } else if (input.substr(i, 2) == "0b") {
        base = 2;
        i += 2;
    } else if (input[i] == '0') {
        base = 8;
        i++;
    }

    // Parse the number part
    while (i < input.length()) {
        if (base == 10 && input[i] == '.') {
            // For integer types, discard the fractional part
            if (std::is_integral<T>::value) {
                // Skip the fractional part
                while (i < input.length() && isdigit(input[i])) {
                    i++;
                }
                break;
            }
            // Parse floating point
            i++;
            double fractional = 0.0;
            double weight = 0.1;
            while (i < input.length() && isdigit(input[i])) {
                fractional += (input[i] - '0') * weight;
                weight *= 0.1;
                i++;
            }
            result += fractional;
        } else if (base == 10 && (input[i] == 'e' || input[i] == 'E')) {
            // Parse exponent part
            i++;
            bool isNegativeExponent = false;
            if (input[i] == '-') {
                isNegativeExponent = true;
                i++;
            } else if (input[i] == '+') {
                i++;
            }
            while (i < input.length() && isdigit(input[i])) {
                exponent = exponent * 10 + (input[i] - '0');
                i++;
            }
            if (isNegativeExponent) {
                exponent = -exponent;
            }
        } else if (isdigit(input[i])) {
            // Parse integer part
            result = result * base + (input[i] - '0');
            i++;
        } else if (base == 16 && isxdigit(input[i])) {
            // Parse hexadecimal
            result = result * 16 +
                     (toupper(input[i]) - (isdigit(input[i]) ? '0' : 'A' - 10));
            i++;
        } else if (base == 2 && (input[i] == '0' || input[i] == '1')) {
            // Parse binary
            result = result * 2 + (input[i] - '0');
            i++;
        } else if (base == 8 && (input[i] >= '0' && input[i] <= '7')) {
            // Parse octal
            result = result * 8 + (input[i] - '0');
            i++;
        } else {
            // Encountered an invalid character
            throw std::invalid_argument("Invalid number format: " + input);
        }
    }

    // Apply exponent
    result *= std::pow(10.0, exponent);

    // Handle sign
    if (isNegative) {
        result = -result;
    }

    // Check for overflow/underflow
    if (result > std::numeric_limits<T>::max() ||
        result < std::numeric_limits<T>::min()) {
        throw std::overflow_error("Number out of range");
    }

    return result;
}

enum class PrimeType {
    ANY,
    MAP,
    VOID,
    LIST,
    DICT,
    ARRAY,
    UNION,
    INT32,
    INT64,
    FLOAT,
    DOUBLE,
    STRING,
    VECTOR,
    MATRIX,
    BOOLEAN,
    FUNCTOR
};

class Type {
  protected:
    PrimeType type_;

  public:
    Type() = delete;
    Type(PrimeType type) : type_(type) {}

    const PrimeType &type() const { return type_; }

    virtual bool operator==(const Type &other) const {
        return type_ == other.type_;
    }
    virtual bool operator!=(const Type &other) const {
        return type_ != other.type_;
    }

    bool equals(const type_ptr_t &type) const { return *type == *this; }
};

using type_ptr_t = std::shared_ptr<Type>;

class AnyType : public Type {
  public:
    AnyType() = delete;
    AnyType() : Type(PrimeType::ANY) {}
};

class VoidType : public Type {
  public:
    VoidType() = delete;
    VoidType() : Type(PrimeType::VOID) {}
};

class Int32Type : public Type {
  public:
    Int32Type() = delete;
    Int32Type() : Type(PrimeType::INT32) {}

    static int32_t parseToken(const std::string &str) {
        return parseNumber<int32_t>(str);
    }
};

class Int64Type : public Type {
  public:
    Int64Type() = delete;
    Int64Type() : Type(PrimeType::INT64) {}

    static int64_t parseToken(const std::string &str) {
        return parseNumber<int64_t>(str);
    }
};

class FloatType : public Type {
  public:
    FloatType() = delete;
    FloatType() : Type(PrimeType::FLOAT) {}

    static float parseToken(const std::string &str) {
        return parseNumber<float>(str);
    }
};

class DoubleType : public Type {
  public:
    DoubleType() = delete;
    DoubleType() : Type(PrimeType::DOUBLE) {}

    static double parseToken(const std::string &str) {
        return parseNumber<double>(str);
    }
};

class StringType : public Type {
  public:
    StringType() = delete;
    StringType() : Type(PrimeType::STRING) {}

    static std::string parseToken(const std::string &str) {
        // remove the quotes
        return str.substr(1, str.size() - 2);
    }
};

class BooleanType : public Type {
  public:
    BooleanType() = delete;
    BooleanType() : Type(PrimeType::BOOLEAN) {}
};

class UnionType : public Type {
  private:
    std::set<type_ptr_t> types_;

  public:
    UnionType() = delete;
    UnionType() : Type(PrimeType::UNION) {}
    UnionType(const std::initializer_list<type_ptr_t> &types)
        : Type(PrimeType::UNION) {
        for (const auto &type : types) {
            types_.insert(type);
        }
    }
    UnionType(const std::vector<type_ptr_t> &types) : Type(PrimeType::UNION) {
        for (const auto &type : types) {
            types_.insert(type);
        }
    }

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::UNION) {
            return false;
        }
        const UnionType &otherUnion = dynamic_cast<const UnionType &>(other);

        if (types_.size() != otherUnion.types_.size()) {
            return false;
        }
        for (const auto &type : otherUnion.types_) {
            if (types_.find(type) == types_.end()) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const Type &other) const override {
        return !(*this == other);
    }

    void add(const type_ptr_t &type) { types_.insert(type); }
    bool has(const type_ptr_t &type) const {
        return types_.find(type) != types_.end();
    }
};

class MapType : public Type {
  private:
    type_ptr_t keyType_;
    type_ptr_t valueType_;

  public:
    MapType() = delete;
    MapType(const type_ptr_t &keyType, const type_ptr_t &valueType)
        : keyType_(keyType), valueType_(valueType), Type(PrimeType::DICT) {}

    type_ptr_t keyType() const { return keyType_; }
    type_ptr_t valueType() const { return valueType_; }

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::DICT) {
            return false;
        }
        const MapType &otherMap = dynamic_cast<const MapType &>(other);

        return keyType_->equals(otherMap.keyType_) &&
               valueType_->equals(otherMap.valueType_);
    }
    bool operator!=(const Type &other) const override {
        if (other.type() != PrimeType::DICT) {
            return true;
        }
        const MapType &otherMap = dynamic_cast<const MapType &>(other);

        return !keyType_->equals(otherMap.keyType_) ||
               !valueType_->equals(otherMap.valueType_);
    }
};

class ArrayType : public Type {
  private:
    type_ptr_t elementType_;
    size_t size_;

  public:
    ArrayType() = delete;
    ArrayType(const type_ptr_t &elementType, size_t size)
        : elementType_(elementType), size_(size), Type(PrimeType::LIST) {}

    size_t size() const { return size_; }
    type_ptr_t elementType() const { return elementType_; }

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::LIST) {
            return false;
        }
        const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
        return size_ == otherArray.size_ &&
               elementType_->equals(otherArray.elementType_);
    }
    bool operator!=(const Type &other) const override {
        if (other.type() != PrimeType::LIST) {
            return true;
        }
        const ArrayType &otherArray = dynamic_cast<const ArrayType &>(other);
        return size_ != otherArray.size_ ||
               !elementType_->equals(otherArray.elementType_);
    }
};

class VectorType : public Type {
  private:
    type_ptr_t elementType_;
    size_t size_;

  public:
    VectorType() = delete;
    VectorType(const type_ptr_t &elementType, size_t size)
        : elementType_(elementType), size_(size), Type(PrimeType::VECTOR) {
        if (size == 0) {
            throw std::invalid_argument("Vector size must be greater than 0");
        }
        // element type must be a primitive type
        const PrimeType type = elementType->type();
        if (type != PrimeType::INT32 && type != PrimeType::INT64 &&
            type != PrimeType::FLOAT && type != PrimeType::DOUBLE) {
            throw std::invalid_argument(
                "Vector element type must be primitive");
        }
    }

    size_t size() const { return size_; }
    type_ptr_t elementType() const { return elementType_; }

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::VECTOR) {
            return false;
        }
        const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
        return size_ == otherVector.size_ &&
               elementType_->equals(otherVector.elementType_);
    }
    bool operator!=(const Type &other) const override {
        if (other.type() != PrimeType::VECTOR) {
            return true;
        }
        const VectorType &otherVector = dynamic_cast<const VectorType &>(other);
        return size_ != otherVector.size_ ||
               !elementType_->equals(otherVector.elementType_);
    }
};

class MatrixType : public Type {
  private:
    type_ptr_t elementType_;
    size_t rows_;
    size_t cols_;

  public:
    MatrixType() = delete;
    MatrixType(const type_ptr_t &elementType, size_t rows, size_t cols)
        : elementType_(elementType), rows_(rows), cols_(cols),
          Type(PrimeType::MATRIX) {
        if (rows == 0 || cols == 0) {
            throw std::invalid_argument(
                "Matrix rows and columns must be greater than 0");
        }
        // element type must be a primitive type
        const PrimeType type = elementType->type();
        if (type != PrimeType::INT32 && type != PrimeType::INT64 &&
            type != PrimeType::FLOAT && type != PrimeType::DOUBLE) {
            throw std::invalid_argument(
                "Matrix element type must be primitive");
        }
    }

    size_t rows() const { return rows_; }
    size_t cols() const { return cols_; }
    type_ptr_t elementType() const { return elementType_; }

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::MATRIX) {
            return false;
        }
        const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
        return rows_ == otherMatrix.rows_ && cols_ == otherMatrix.cols_ &&
               elementType_->equals(otherMatrix.elementType_);
    }
    bool operator!=(const Type &other) const override {
        if (other.type() != PrimeType::MATRIX) {
            return true;
        }
        const MatrixType &otherMatrix = dynamic_cast<const MatrixType &>(other);
        return rows_ != otherMatrix.rows_ || cols_ != otherMatrix.cols_ ||
               !elementType_->equals(otherMatrix.elementType_);
    }
};

class ListType : public Type {
  public:
    ListType() = delete;
    ListType() : Type(PrimeType::LIST) {}

    bool operator==(const Type &other) const override { return true; }
    bool operator!=(const Type &other) const override { return false; }
};

class DictType : public Type {
  private:
    // field name -> field type with default value
    std::unordered_map<std::string, std::pair<type_ptr_t, std::any>> fields_;

  public:
    DictType() = delete;
    DictType() : Type(PrimeType::DICT) {}

    bool operator==(const Type &other) const override {
        if (other.type() != PrimeType::DICT) {
            return false;
        }
        const DictType &otherDict = dynamic_cast<const DictType &>(other);

        if (fields_.size() != otherDict.fields_.size()) {
            return false;
        }
        for (const auto &field : otherDict.fields_) {
            const auto &ident = field.first;
            const auto &type = field.second.first;
            if (!fields_.count(ident)) {
                return false;
            }
            const auto &fieldType = fields_.at(ident).first;
            if (fieldType->type() != type->type()) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const Type &other) const override {
        return !(*this == other);
    }

    bool add(const std::string &name, const type_ptr_t &type,
             const std::any &value = std::any()) {
        if (has(name)) {
            return false;
        }
        fields_[name] = std::make_pair(type, value);
        return true;
    }

    bool del(const std::string &name) { return fields_.erase(name) > 0; }

    bool has(const std::string &name) const {
        return fields_.find(name) != fields_.end();
    }

    void set(const std::string &name, const type_ptr_t &type,
             const std::any &value = std::any()) {
        fields_.at(name) = std::make_pair(type, value);
    }

    std::pair<type_ptr_t, std::any> get(const std::string &name) const {
        return fields_.at(name);
    }

    type_ptr_t operator|(const DictType &other) const {
        auto result = std::make_shared<DictType>();
        for (const auto &field : fields_) {
            result->add(field.first, field.second.first, field.second.second);
        }
        for (const auto &field : other.fields_) {
            const auto &ident = field.first;
            const auto &type = field.second.first;
            const auto &value = field.second.second;
            if (!result->has(ident)) {
                result->add(ident, type, value);
            } else {
                // if the field already exists, use the rhs type and value
                result->set(ident, type, value);
            }
        }
        return result;
    }

    type_ptr_t operator&(const DictType &other) const {
        auto result = std::make_shared<DictType>();
        for (const auto &field : fields_) {
            const auto &ident = field.first;
            if (other.has(ident)) {
                const type_ptr_t &otherType = other.get(ident).first;
                const std::any &otherValue = other.get(ident).second;
                result->add(ident, otherType, otherValue);
            }
        }
        return result;
    }
};