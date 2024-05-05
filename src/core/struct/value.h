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

#include "type.h"

class UnsupportedConvError : public std::exception {};

class ValueConvError : public std::exception {
  private:
    std::string message_;

  public:
    ValueConvError() = delete;
    ValueConvError(const std::string &message) : message_(message) {}

    virtual const char *what() const noexcept override {
        return std::string("Value conversion failed: " + message_).c_str();
    }
};

class Value;
class Entity;

using value_ptr_t = std::shared_ptr<const Value>;
using entity_ptr_t = std::shared_ptr<const Entity>;

class Value : public std::enable_shared_from_this<Value> {
  protected:
    type_ptr_t type_ = nullptr;

  public:
    Value() = delete;
    Value(type_ptr_t type = nullptr) : type_(type) {}
    virtual ~Value() = default;

    bool isNull() const { return type_ == nullptr; }
    type_ptr_t type() const { return type_; }

    virtual const value_ptr_t clone() const = 0;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) const = 0;
};

template <typename Dest, typename Src> std::shared_ptr<Dest> convertAndMakeShared(const Src &value) {
    if constexpr (std::is_same_v<Dest, Src>) {
        static_assert(always_false<Dest>::value, "This path should not be reached");
    }
    return std::make_shared<Dest>(static_cast<Dest>(value));
}

template <typename T> class PrimeValue : public Value {
  private:
    T data_;

  public:
    PrimeValue() = delete;
    PrimeValue(const T &data) : Value(), data_(data) {
        if constexpr (std::is_same_v<T, int32_t>) {
            type_ = int32TypePtr;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            type_ = int64TypePtr;
        } else if constexpr (std::is_same_v<T, float>) {
            type_ = floatTypePtr;
        } else if constexpr (std::is_same_v<T, double>) {
            type_ = doubleTypePtr;
        } else if constexpr (std::is_same_v<T, bool>) {
            type_ = boolTypePtr;
        } else if constexpr (std::is_same_v<T, char>) {
            type_ = boolTypePtr;
        } else if constexpr (std::is_same_v<T, std::string>) {
            type_ = stringTypePtr;
        } else {
            static_assert(always_false<T>::value, "Unsupported type");
        }
    }

    const T &data() const { return data_; }

    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) const override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        try {
            if (target->primitive()) {
                if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, float> ||
                              std::is_same_v<T, double>) {
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return convertAndMakeShared<int32_t>(data_);
                    case TypeCode::INT64:
                        return convertAndMakeShared<int64_t>(data_);
                    case TypeCode::FLOAT:
                        return convertAndMakeShared<float>(data_);
                    case TypeCode::DOUBLE:
                        return convertAndMakeShared<double>(data_);
                    case TypeCode::BOOL:
                        return convertAndMakeShared<bool>(data_);
                    case TypeCode::CHAR:
                        return convertAndMakeShared<char>(data_);

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, std::string>) {
                    switch (target->code()) {
                    case TypeCode::BOOL: {
                        const std::string &str = data_;
                        return std::make_shared<bool>(str.length() > 0);
                    }

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, bool>) {
                    const bool &b = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<int32_t>(static_cast<int32_t>(b));
                    case TypeCode::INT64:
                        return std::make_shared<int64_t>(static_cast<int64_t>(b));
                    case TypeCode::FLOAT:
                        return std::make_shared<float>(static_cast<float>(b));
                    case TypeCode::DOUBLE:
                        return std::make_shared<double>(static_cast<double>(b));
                    case TypeCode::STRING:
                        return std::make_shared<std::string>(b ? "true" : "false");
                    case TypeCode::CHAR:
                        return std::make_shared<char>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<int32_t>(static_cast<int32_t>(c));
                    case TypeCode::INT64:
                        return std::make_shared<int64_t>(static_cast<int64_t>(c));
                    case TypeCode::FLOAT:
                        return std::make_shared<float>(static_cast<float>(c));
                    case TypeCode::DOUBLE:
                        return std::make_shared<double>(static_cast<double>(c));
                    case TypeCode::STRING:
                        return std::make_shared<std::string>(1, c);
                    case TypeCode::BOOL:
                        return std::make_shared<bool>(c != 0);

                    default:
                        throw UnsupportedConvError();
                    }
                } else {
                    static_assert(always_false<T>::value, "Unsupported type");
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw ValueConvError("Cannot convert " + typeCodeToString(type_->code()) + " to " +
                                 typeCodeToString(target->code()));
        } catch (const std::exception &e) {
            throw ValueConvError(e.what());
        }
    }
};

class StructValue : public Value {
  public:
    StructValue() = delete;
    StructValue(type_ptr_t type) : Value(type) {}

    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) const override = 0;
};

class SetValue : public StructValue {
  private:
    std::set<entity_ptr_t> data_;

  public:
    SetValue() = delete;
    SetValue(type_ptr_t type) : StructValue(type) {}

    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) const override;
};