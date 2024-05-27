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
    ValueConvError(const std::string &message) { message_ = "Value conversion failed: " + message; }

    virtual const char *what() const noexcept override { return message_.c_str(); }
};

class Value;
using value_ptr_t = std::shared_ptr<Value>;

class Value : public std::enable_shared_from_this<Value> {
  protected:
    type_ptr_t type_ = nullptr;

  public:
    Value() : type_(nullptr){};
    Value(type_ptr_t type) : type_(type) {}
    virtual ~Value() = default;

    bool isNull() const { return type_ == nullptr; }
    type_ptr_t type() const { return type_; }

    virtual bool resolved() = 0;
    virtual void resolve(){};
    virtual void pending(){};

    virtual const value_ptr_t clone(bool deep = false) const = 0;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) = 0;

    virtual const std::string toString() const = 0;
};

class StringValue;

template <typename T> class PrimValue : public Value {
  private:
    T data_;

    template <typename Dest, typename Src> std::shared_ptr<PrimValue<Dest>> convertAndMakeShared(const Src &value) {
        return std::make_shared<PrimValue<Dest>>(static_cast<Dest>(value));
    }

  public:
    PrimValue() = delete;
    PrimValue(const T &data) : Value(), data_(data) {
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
        } else {
            static_assert(!std::is_same_v<T, T>, "Unsupported type");
        }
    }

    const T &data() const { return data_; }

    bool resolved() override { return true; }
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override {
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
                } else if constexpr (std::is_same_v<T, bool>) {
                    const bool &b = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<PrimValue<int32_t>>(static_cast<int32_t>(b));
                    case TypeCode::INT64:
                        return std::make_shared<PrimValue<int64_t>>(static_cast<int64_t>(b));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimValue<float>>(static_cast<float>(b));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimValue<double>>(static_cast<double>(b));
                    case TypeCode::STRING:
                        return std::make_shared<StringValue>(b ? "true" : "false");
                    case TypeCode::CHAR:
                        return std::make_shared<PrimValue<char>>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<PrimValue<int32_t>>(static_cast<int32_t>(c));
                    case TypeCode::INT64:
                        return std::make_shared<PrimValue<int64_t>>(static_cast<int64_t>(c));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimValue<float>>(static_cast<float>(c));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimValue<double>>(static_cast<double>(c));
                    case TypeCode::STRING:
                        return std::make_shared<StringValue>(std::string(1, c));
                    case TypeCode::BOOL:
                        return std::make_shared<PrimValue<bool>>(c != 0);

                    default:
                        throw UnsupportedConvError();
                    }
                } else {
                    static_assert(!std::is_same_v<T, T>, "Unsupported type");
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw ValueConvError("Cannot convert " + typeCodeToString(type_->code()) + " to " +
                                 typeCodeToString(target->code()));
        } catch (const std::exception &e) {
            throw ValueConvError(e.what());
        }
    }
    virtual const value_ptr_t clone(bool deep = false) const override { return std::make_shared<PrimValue<T>>(data_); }
    virtual const std::string toString() const override { return std::to_string(data_); }
};

class StringValue : public Value {
  private:
    std::string data_;

  public:
    StringValue() = delete;
    StringValue(const std::string &data) : Value(stringTypePtr), data_(data) {}

    const std::string &data() const { return data_; }

    bool resolved() override { return true; }
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        try {
            if (target->primitive()) {
                switch (target->code()) {
                case TypeCode::BOOL: {
                    return std::make_shared<PrimValue<bool>>(data_.length() > 0);
                }

                default:
                    throw UnsupportedConvError();
                }
            }
        } catch (const UnsupportedConvError &e) {
            throw ValueConvError("Cannot convert " + typeCodeToString(type_->code()) + " to " +
                                 typeCodeToString(target->code()));
        } catch (const std::exception &e) {
            throw ValueConvError(e.what());
        }
    }
    virtual const value_ptr_t clone(bool deep = false) const override { return std::make_shared<StringValue>(data_); }
    virtual const std::string toString() const override { return data_; }
};

class StructValue : public Value {
  public:
    StructValue() = delete;
    StructValue(type_ptr_t type) : Value(type) {}
    virtual ~StructValue() = default;

    virtual bool resolved() = 0;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override = 0;
    virtual const value_ptr_t clone(bool deep = false) const override = 0;
    virtual const std::string toString() const override = 0;
};

class SetValue : public StructValue {
  private:
    bool resolved_ = false;
    std::set<entity_ptr_t> data_;

  public:
    SetValue(type_ptr_t elType) : StructValue(std::make_shared<SetType>(elType)) {}
    SetValue(type_ptr_t elType, std::initializer_list<entity_ptr_t> data)
        : StructValue(std::make_shared<SetType>(elType)), data_(data) {}
    SetValue(type_ptr_t elType, const std::set<entity_ptr_t> &data)
        : StructValue(std::make_shared<SetType>(elType)), data_(data) {}
    virtual ~SetValue() = default;

    // bool add(const entity_ptr_t &e) {
    //     if (e->type() == type_->elementType()) {
    //         data_.insert(e);
    //         return true;
    //     }
    //     return false;
    // }

    bool resolved() override;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual const value_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class ListValue : public StructValue {
  private:
    bool resolved_ = false;
    std::vector<entity_ptr_t> data_;

  public:
    ListValue() : StructValue(listTypePtr) {}
    ListValue(std::initializer_list<entity_ptr_t> data) : StructValue(listTypePtr), data_(data) {}
    ListValue(const std::vector<entity_ptr_t> &data) : StructValue(listTypePtr), data_(data) {}
    virtual ~ListValue() = default;

    bool add(const entity_ptr_t &e) { data_.push_back(e); }

    bool resolved() override;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual const value_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class DictValue : public StructValue {
  private:
    bool resolved_ = false;
    std::unordered_map<std::string, entity_ptr_t> data_;

  public:
    DictValue() : StructValue(std::make_shared<DictType>()) {}
    DictValue(std::initializer_list<std::pair<std::string, entity_ptr_t>> data);
    DictValue(const std::unordered_map<std::string, entity_ptr_t> &data);
    virtual ~DictValue() = default;

    bool add(const std::string &key, const entity_ptr_t &e);

    bool del(const std::string &key);

    bool has(const std::string &key) const;

    void set(const std::string &key, const entity_ptr_t &e);

    entity_ptr_t get(const std::string &key) const;

    void clear() {
        DictType &dictType = *static_cast<DictType *>(type_.get());
        dictType.clear();
        data_.clear();
    }

    bool resolved() override;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual const value_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class NamedTupleValue : public Value {
  private:
    bool resolved_ = false;
    bool typeResolved_ = false;
    std::vector<entity_ptr_t> indexData_;
    std::unordered_map<std::string, entity_ptr_t> namedData_;

  public:
    NamedTupleValue() : Value(std::make_shared<NamedTupleType>()) {}
    NamedTupleValue(const std::vector<entity_ptr_t> &indexData,
                    const std::unordered_map<std::string, entity_ptr_t> &namedData)
        : Value(std::make_shared<NamedTupleType>()), indexData_(indexData), namedData_(namedData) {}
    virtual ~NamedTupleValue() = default;

    bool setType(type_ptr_t type);

    bool add(const entity_ptr_t &e, const std::string &key = "");

    bool resolved() override;
    virtual const value_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual const value_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};