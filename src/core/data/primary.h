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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"
#include "utils/assert.h"

#include <iomanip>
#include <sstream>
#include <type_traits>

template <typename T> class PrimaryData : public Data {
  private:
    T data_;

    template <typename Dest, typename Src>
    std::shared_ptr<PrimaryData<Dest>> convertAndMakeShared(const Src &data) {
        return std::make_shared<PrimaryData<Dest>>(static_cast<Dest>(data));
    }

  public:
    PrimaryData() = delete;
    PrimaryData(const T &data) : Data(), data_(data) {
        if constexpr (std::is_same_v<T, int32_t>) {
            type_ = Type::Int32();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            type_ = Type::Int64();
        } else if constexpr (std::is_same_v<T, float>) {
            type_ = Type::Float();
        } else if constexpr (std::is_same_v<T, double>) {
            type_ = Type::Double();
        } else if constexpr (std::is_same_v<T, bool>) {
            type_ = Type::Bool();
        } else if constexpr (std::is_same_v<T, char>) {
            type_ = Type::Char();
        } else {
            static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
        }
    }

    const T &data() const { return data_; }

    virtual bool equals(const data_ptr_t &other) const override {
        if (auto o = std::dynamic_pointer_cast<PrimaryData<T>>(other)) {
            return data_ == o->data_;
        }
        return false;
    }

    virtual bool isZero() const override {
        if constexpr (
            std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, float> ||
            std::is_same_v<T, double>) {
            return data_ == 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            return data_ == false;
        } else if constexpr (std::is_same_v<T, char>) {
            return data_ == '\0';
        } else {
            static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
        }
    }

    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        ASSERT(inplace == false, "In-place conversion not supported for PrimaryData");
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        try {
            if (target->primary()) {
                if constexpr (
                    std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t> ||
                    std::is_same_v<T, float> || std::is_same_v<T, double>) {
                    switch (target->code()) {
                    case TypeCode::Int32:
                        return convertAndMakeShared<int32_t>(data_);
                    case TypeCode::Int64:
                        return convertAndMakeShared<int64_t>(data_);
                    case TypeCode::Float:
                        return convertAndMakeShared<float>(data_);
                    case TypeCode::Double:
                        return convertAndMakeShared<double>(data_);
                    case TypeCode::Bool:
                        return convertAndMakeShared<bool>(data_);
                    case TypeCode::Char:
                        return convertAndMakeShared<char>(data_);

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, bool>) {
                    const bool &b = data_;
                    switch (target->code()) {
                    case TypeCode::Int32:
                        return std::make_shared<PrimaryData<int32_t>>(static_cast<int32_t>(b));
                    case TypeCode::Int64:
                        return std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(b));
                    case TypeCode::Float:
                        return std::make_shared<PrimaryData<float>>(static_cast<float>(b));
                    case TypeCode::Double:
                        return std::make_shared<PrimaryData<double>>(static_cast<double>(b));
                    case TypeCode::String:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<StringData>(b ? "true" : "false"));
                    case TypeCode::Char:
                        return std::make_shared<PrimaryData<char>>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = data_;
                    switch (target->code()) {
                    case TypeCode::Int32:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<PrimaryData<int32_t>>(static_cast<int32_t>(c)));
                    case TypeCode::Int64:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(c)));
                    case TypeCode::Float:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<PrimaryData<float>>(static_cast<float>(c)));
                    case TypeCode::Double:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<PrimaryData<double>>(static_cast<double>(c)));
                    case TypeCode::String:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<StringData>(std::string(1, c)));
                    case TypeCode::Bool:
                        return std::static_pointer_cast<Data>(
                            std::make_shared<PrimaryData<bool>>(c != 0));
                    default:
                        throw UnsupportedConvError();
                    }

                } else {
                    static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw DataConvError(
                "Cannot convert " + typeCodeToString(type_->code()) + " to " +
                typeCodeToString(target->code()));
        } catch (const std::exception &e) {
            throw DataConvError(e.what());
        }
        throw DataConvError(
            "Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override {
        return std::make_shared<PrimaryData<T>>(data_);
    }
    virtual const std::string toString() const override {
        if constexpr (std::is_same_v<T, bool>) {
            return data_ ? "true" : "false";
        } else if constexpr (std::is_same_v<T, char>) {
            return std::string(1, data_);
        } else if constexpr (std::is_same_v<T, float>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << data_;
            return oss.str();
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << data_;
            return oss.str();
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return std::to_string(data_);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(data_) + "L";
        } else {
            return std::to_string(data_);
        }
    }
    virtual void print(std::ostream &os) const override { os << std::to_string(data_); }
};

using Int32Data = PrimaryData<int32_t>;
using Int64Data = PrimaryData<int64_t>;
using FloatData = PrimaryData<float>;
using DoubleData = PrimaryData<double>;
using BoolData = PrimaryData<bool>;
using CharData = PrimaryData<char>;

class StringData : public Data {
  private:
    std::string data_;

  public:
    StringData() = delete;
    StringData(const std::string &data);

    const std::string &data() const;

    virtual bool equals(const data_ptr_t &other) const override;

    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
