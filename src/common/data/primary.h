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
 * Updated: Mar. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"

template <typename T> class PrimaryData : public Data {
  private:
    T data_;

    template <typename Dest, typename Src> std::shared_ptr<PrimaryData<Dest>> convertAndMakeShared(const Src &data) {
        return std::make_shared<PrimaryData<Dest>>(static_cast<Dest>(data));
    }

  public:
    PrimaryData() = delete;
    PrimaryData(const T &data) : Data(), data_(data) {
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
            type_ = charTypePtr;
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

    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        try {
            if (target->primary()) {
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
                } else if constexpr (std::is_same_v<T, bool>) {
                    const bool &b = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<PrimaryData<int32_t>>(static_cast<int32_t>(b));
                    case TypeCode::INT64:
                        return std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(b));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimaryData<float>>(static_cast<float>(b));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimaryData<double>>(static_cast<double>(b));
                    case TypeCode::STRING:
                        return std::dynamic_pointer_cast<Data>(std::make_shared<StringData>(b ? "true" : "false"));
                    case TypeCode::CHAR:
                        return std::make_shared<PrimaryData<char>>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<PrimaryData<int32_t>>(static_cast<int32_t>(c));
                    case TypeCode::INT64:
                        return std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(c));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimaryData<float>>(static_cast<float>(c));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimaryData<double>>(static_cast<double>(c));
                    case TypeCode::STRING:
                        return std::dynamic_pointer_cast<Data>(std::make_shared<StringData>(std::string(1, c)));
                    case TypeCode::BOOL:
                        return std::make_shared<PrimaryData<bool>>(c != 0);

                    default:
                        throw UnsupportedConvError();
                    }
                } else {
                    static_cml_assert(!std::is_same_v<T, T>, "Unsupported type");
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw DataConvError("Cannot convert " + typeCodeToString(type_->code()) + " to " +
                                typeCodeToString(target->code()));
        } catch (const std::exception &e) {
            throw DataConvError(e.what());
        }
        throw DataConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<PrimaryData<T>>(data_); }
    virtual const std::string toString() const override { return std::to_string(data_); }
};
