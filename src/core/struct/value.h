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

class Value : public std::enable_shared_from_this<Value> {
  protected:
    type_ptr_t type_ = nullptr;
    std::any data_;

  public:
    Value() = delete;
    Value(type_ptr_t type = nullptr, std::any data = 0)
        : type_(type), data_(data) {}
    virtual ~Value() = default;

    bool isNull() const { return !data_.has_value() || type_ == nullptr; }
    type_ptr_t type() const { return type_; }
    std::any data() const { return data_; }

    virtual const std::shared_ptr<Value> convert(type_ptr_t target) const = 0;
};

using value_ptr_t = std::shared_ptr<Value>;

template <typename Dest, typename Src>
std::shared_ptr<Dest> convertAndMakeShared(const std::any &value) {
    if constexpr (std::is_same_v<Dest, Src>) {
        throw ValueConvError("This path should not be reached");
    }
    if (!value.has_value()) {
        throw ValueConvError("Cannot convert null value");
    }
    if (value.type() != typeid(Src)) {
        throw ValueConvError("Value type mismatched with actual type");
    }
    const Src &data = std::any_cast<Src>(value);
    return std::make_shared<Dest>(static_cast<Dest>(data));
}

template <typename T> class PrimeValue : public Value {
  public:
    PrimeValue() = delete;
    PrimeValue(const T &data) : Value() {
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
            type_ = nullptr;
            throw std::runtime_error("Unsupported type");
        }
        data_ = std::make_shared<std::any>(data);
    }

    virtual const value_ptr_t convert(type_ptr_t target) const override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        try {
            if (target->primitive()) {
                if constexpr (std::is_same_v<T, int32_t> ||
                              std::is_same_v<T, int64_t> ||
                              std::is_same_v<T, float> ||
                              std::is_same_v<T, double>) {
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return convertAndMakeShared<int32_t, T>(data_);
                    case TypeCode::INT64:
                        return convertAndMakeShared<int64_t, T>(data_);
                    case TypeCode::FLOAT:
                        return convertAndMakeShared<float, T>(data_);
                    case TypeCode::DOUBLE:
                        return convertAndMakeShared<double, T>(data_);
                    case TypeCode::BOOL:
                        return convertAndMakeShared<bool, T>(data_);
                    case TypeCode::CHAR:
                        return convertAndMakeShared<char, T>(data_);

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, std::string>) {
                    switch (target->code()) {
                    case TypeCode::BOOL: {
                        const std::string &str =
                            std::any_cast<std::string>(data_);
                        return std::make_shared<bool>(str.length() > 0);
                    }

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, bool>) {
                    const bool &b = std::any_cast<bool>(data_);
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<int32_t>(
                            static_cast<int32_t>(b));
                    case TypeCode::INT64:
                        return std::make_shared<int64_t>(
                            static_cast<int64_t>(b));
                    case TypeCode::FLOAT:
                        return std::make_shared<float>(static_cast<float>(b));
                    case TypeCode::DOUBLE:
                        return std::make_shared<double>(static_cast<double>(b));
                    case TypeCode::STRING:
                        return std::make_shared<std::string>(b ? "true"
                                                               : "false");
                    case TypeCode::CHAR:
                        return std::make_shared<char>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = std::any_cast<char>(data_);
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<int32_t>(
                            static_cast<int32_t>(c));
                    case TypeCode::INT64:
                        return std::make_shared<int64_t>(
                            static_cast<int64_t>(c));
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
                    throw UnsupportedConvError();
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw ValueConvError("Cannot convert " +
                                 typeCodeToString(type_->code()) + " to " +
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

    virtual const value_ptr_t convert(type_ptr_t target) const override = 0;
};