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

#include <iostream>
#include <regex>

#include "entity.h"
#include "type.h"

class UnsupportedConvError : public std::exception {};

class ValueConvError : public std::exception {
  private:
    std::string message_;

  public:
    ValueConvError() = delete;
    ValueConvError(const std::string &message) { message_ = "Data conversion failed: " + message; }

    virtual const char *what() const noexcept override { return message_.c_str(); }
};

class Data : public std::enable_shared_from_this<Data> {
  protected:
    bool mutable_ = false;
    type_ptr_t type_;
    entity_wptr_t entity_;

  public:
    Data() {};
    Data(type_ptr_t type) : type_(type) {}
    virtual ~Data() = default;

    type_ptr_t type() const { return type_; }
    entity_ptr_t entity() {
        if (entity_.expired()) {
            entity_ptr_t entity = std::make_shared<Entity>(shared_from_this());
            entity_ = entity;
            return entity;
        }
        return entity_.lock();
    }
    void setEntity(const entity_ptr_t &entity) { entity_ = entity; }

    bool variable() const { return mutable_; }
    void setVariable() { mutable_ = true; }

    virtual bool equals(const data_ptr_t &other) const {
        throw std::runtime_error("Base Data::equals() not implemented");
    }
    virtual data_ptr_t clone(bool deep = false) const {
        throw std::runtime_error("Base Data::clone() not implemented");
    }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) {
        throw std::runtime_error("Base Data::convert() not implemented");
    }

    virtual const std::string toString() const { throw std::runtime_error("Base Data::toString() not implemented"); }
};

// definition below is forwarded to type.h
// using data_ptr_t = std::shared_ptr<Data>;
// using data_wptr_t = std::weak_ptr<Data>;

class DanglingValue : public Data {
  private:
    std::string ref_;

  public:
    DanglingValue(const std::string &ref) : Data(refTypePtr), ref_(ref) {}
    virtual ~DanglingValue() = default;

    const std::string &ref() const { return ref_; }

    virtual bool equals(const data_ptr_t &other) const override { return false; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        throw ValueConvError("Cannot convert DanglingValue to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<DanglingValue>(ref_); }
    virtual const std::string toString() const override { return "DREF<" + ref_ + ">"; }
};

using dang_value_ptr_t = std::shared_ptr<DanglingValue>;

class NullValue : public Data {
  public:
    NullValue(type_ptr_t type = voidTypePtr) : Data(type) {}
    virtual ~NullValue() = default;

    virtual bool equals(const data_ptr_t &other) const override { return true; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        throw ValueConvError("Cannot convert null to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<NullValue>(); }
    virtual const std::string toString() const override { return "null"; }
};

class StringValue;

template <typename T> class PrimValue : public Data {
  private:
    T data_;

    template <typename Dest, typename Src> std::shared_ptr<PrimValue<Dest>> convertAndMakeShared(const Src &value) {
        return std::make_shared<PrimValue<Dest>>(static_cast<Dest>(value));
    }

  public:
    PrimValue() = delete;
    PrimValue(const T &data) : Data(), data_(data) {
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

    virtual bool equals(const data_ptr_t &other) const override {
        if (auto o = std::dynamic_pointer_cast<PrimValue<T>>(other)) {
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
        throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<PrimValue<T>>(data_); }
    virtual const std::string toString() const override { return std::to_string(data_); }
};

class StringValue : public Data {
  private:
    std::string data_;

  public:
    StringValue() = delete;
    StringValue(const std::string &data) : Data(stringTypePtr), data_(data) {}

    const std::string &data() const { return data_; }

    virtual bool equals(const data_ptr_t &other) const override {
        if (auto o = std::dynamic_pointer_cast<StringValue>(other)) {
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
        throw ValueConvError("Cannot convert " + type_->toString() + " to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<StringValue>(data_); }
    virtual const std::string toString() const override {
        std::regex re("\\n");
        return "\"" + std::regex_replace(data_, re, "\\n") + "\"";
    }
};

class StructValue : public Data {
  public:
    StructValue() = default;
    StructValue(type_ptr_t type) : Data(type) {}
    virtual ~StructValue() = default;

    virtual bool equals(const data_ptr_t &other) const override = 0;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override = 0;
    virtual data_ptr_t clone(bool deep = false) const override = 0;
    virtual const std::string toString() const override = 0;
};

class SetValue : public StructValue {
  private:
    std::unordered_set<data_ptr_t> data_;

  public:
    SetValue(type_ptr_t elType) : StructValue(std::make_shared<SetType>(elType)) {}
    SetValue(type_ptr_t elType, data_list_t data) : StructValue(std::make_shared<SetType>(elType)), data_(data) {}
    SetValue(type_ptr_t elType, const std::unordered_set<data_ptr_t> &data)
        : StructValue(std::make_shared<SetType>(elType)), data_(data) {}
    virtual ~SetValue() = default;

    bool add(const data_ptr_t &e) { return data_.insert(e).second; }
    bool del(const data_ptr_t &e) { return data_.erase(e) > 0; }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class MapValue : public StructValue {
  private:
    std::unordered_map<data_ptr_t, data_ptr_t> data_;

  public:
    MapValue(type_ptr_t keyType, type_ptr_t valueType) : StructValue(std::make_shared<MapType>(keyType, valueType)) {}
    MapValue(type_ptr_t keyType, type_ptr_t valueType, const std::unordered_map<data_ptr_t, data_ptr_t> &data)
        : StructValue(std::make_shared<MapType>(keyType, valueType)), data_(data) {}
    virtual ~MapValue() = default;

    bool set(const data_ptr_t &key, const data_ptr_t &value) {
        return data_.insert(std::make_pair(key, value)).second;
    }
    bool del(const data_ptr_t &key) { return data_.erase(key) > 0; }
    data_ptr_t get(const data_ptr_t &key) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return nullptr;
        }
        return it->second;
    }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class DictValue : public StructValue {
  private:
    std::unordered_map<std::string, data_ptr_t> data_;

  public:
    DictValue() : StructValue(std::make_shared<DictType>()) {}
    DictValue(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    DictValue(const std::unordered_map<std::string, data_ptr_t> &data);
    virtual ~DictValue() = default;

    bool add(const std::string &key, const data_ptr_t &e);

    bool del(const std::string &key);

    bool has(const std::string &key) const;

    void set(const std::string &key, const data_ptr_t &e);

    data_ptr_t get(const std::string &key) const;

    void clear() {
        DictType &dictType = *static_cast<DictType *>(type_.get());
        dictType.clear();
        data_.clear();
    }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class ListValue : public StructValue {
  private:
    std::vector<data_ptr_t> data_;

  public:
    ListValue() : StructValue(listTypePtr), data_() {}
    ListValue(data_list_t data) : StructValue(listTypePtr), data_(data) {}
    ListValue(const std::vector<data_ptr_t> &data) : StructValue(listTypePtr), data_(data) {}
    virtual ~ListValue() = default;

    void pushBack(const data_ptr_t &e) { data_.push_back(e); }
    data_ptr_t popBack() {
        if (data_.empty()) {
            return nullptr;
        }
        data_ptr_t back = data_.back();
        data_.pop_back();
        return back;
    }

    data_ptr_t get(size_t index) const {
        if (index >= data_.size()) {
            return nullptr;
        }
        return data_[index];
    }
    bool set(size_t index, const data_ptr_t &e) {
        if (index >= data_.size()) {
            return false;
        }
        data_[index] = e;
        return true;
    }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class TupleValue : public StructValue {
  private:
    std::vector<data_ptr_t> data_;

  public:
    TupleValue(data_list_t data) : data_(data) {
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<TupleType>(types);
    }
    TupleValue(const std::vector<data_ptr_t> &data) : data_(data) {
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<TupleType>(types);
    }
    virtual ~TupleValue() = default;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class ArrayValue : public StructValue {
  private:
    std::vector<data_ptr_t> data_;

  public:
    ArrayValue(const std::shared_ptr<ArrayType> &type, const std::vector<data_ptr_t> &data) : data_(data) {
        type_ = type_;
    }
    ArrayValue(const std::shared_ptr<ArrayType> &type, data_list_t data = {}) : data_(data) {
        type_ = type_;
    }
    ArrayValue(type_ptr_t type, size_t length, const std::vector<data_ptr_t> &data) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<ArrayType>(type, length);
        data_.resize(length);
    }
    ArrayValue(type_ptr_t type, size_t length, data_list_t data = {}) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<ArrayType>(type, length);
        data_.resize(length);
    }
    virtual ~ArrayValue() = default;

    data_ptr_t get(size_t index) const {
        if (index >= data_.size()) {
            return nullptr;
        }
        return data_[index];
    }
    bool set(size_t index, const data_ptr_t &e) {
        if (index >= data_.size()) {
            return false;
        }
        data_[index] = e;
        return true;
    }
    size_t size() const { return data_.size(); }
    size_t length() const { return data_.size(); }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class VectorValue : public StructValue {
  private:
    std::vector<data_ptr_t> data_;

  public:
    VectorValue(const std::shared_ptr<VectorType> &type, const std::vector<data_ptr_t> &data) : data_(data) {
        type_ = type_;
    }
    VectorValue(const std::shared_ptr<VectorType> &type, data_list_t data = {}) : data_(data) {
        type_ = type_;
    }
    VectorValue(type_ptr_t type, const std::vector<data_ptr_t> &data) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<VectorType>(type);
    }
    VectorValue(type_ptr_t type, data_list_t data = {}) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<VectorType>(type);
    }
    virtual ~VectorValue() = default;

    void pushBack(const data_ptr_t &e) { data_.push_back(e); }
    data_ptr_t popBack() {
        if (data_.empty()) {
            return nullptr;
        }
        data_ptr_t back = data_.back();
        data_.pop_back();
        return back;
    }

    data_ptr_t get(size_t index) const {
        if (index >= data_.size()) {
            return nullptr;
        }
        return data_[index];
    }
    bool set(size_t index, const data_ptr_t &e) {
        if (index >= data_.size()) {
            return false;
        }
        data_[index] = e;
        return true;
    }
    size_t size() const { return data_.size(); }
    size_t length() const { return data_.size(); }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class NamedTupleValue : public Data {
  private:
    bool typeResolved_ = false;
    std::vector<data_ptr_t> indexData_;
    std::map<std::string, data_ptr_t> namedData_;

  public:
    NamedTupleValue() : Data(std::make_shared<NamedTupleType>()) {}
    NamedTupleValue(const std::vector<data_ptr_t> &indexData,
                    const std::map<std::string, data_ptr_t> &namedData)
        : Data(std::make_shared<NamedTupleType>()), indexData_(indexData), namedData_(namedData) {
            std::shared_ptr<NamedTupleType> type = std::dynamic_pointer_cast<NamedTupleType>(type_);
            for (const auto &e : indexData) {
                type->add("", e->type(), nullptr);
            }
            for (const auto &e : namedData) {
                type->add(e.first, e.second->type(), nullptr);
            }
        }
    virtual ~NamedTupleValue() = default;

    bool setType(type_ptr_t type);

    bool add(const data_ptr_t &e, const std::string &key = "");

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class TensorValue : public StructValue {
  private:
    data_ptr_t data_; // TODO: support multi-dimensional tensor

  public:
    TensorValue(const type_ptr_t &elementType, const std::vector<size_t> &shape)
        : StructValue(std::make_shared<TensorType>(elementType, shape)) {}
    virtual ~TensorValue() = default;

    data_ptr_t at(const std::vector<size_t> &index) const;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};