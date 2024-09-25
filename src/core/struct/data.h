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

class DataConvError : public std::exception {
  private:
    std::string message_;

  public:
    DataConvError() = delete;
    DataConvError(const std::string &message) { message_ = "Data conversion failed: " + message; }

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

class RefData : public Data {
  private:
    std::string ref_;

  public:
    RefData(const std::string &ref) : Data(refTypePtr), ref_(ref) {}
    virtual ~RefData() = default;

    const std::string &ref() const { return ref_; }

    virtual bool equals(const data_ptr_t &other) const override { return false; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        throw DataConvError("Cannot convert RefData to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<RefData>(ref_); }
    virtual const std::string toString() const override { return "DREF<" + ref_ + ">"; }
};

using ref_data_ptr_t = std::shared_ptr<RefData>;

class NullData : public Data {
  public:
    NullData(type_ptr_t type = voidTypePtr) : Data(type) {}
    virtual ~NullData() = default;

    virtual bool equals(const data_ptr_t &other) const override { return true; }
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override {
        if (target == type_ || type_->code() == target->code()) {
            // same type, no need to convert
            return shared_from_this();
        }
        throw DataConvError("Cannot convert null to " + typeCodeToString(target->code()));
    }
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<NullData>(); }
    virtual const std::string toString() const override { return "null"; }
};

class StringData;

template <typename T> class PrimData : public Data {
  private:
    T data_;

    template <typename Dest, typename Src> std::shared_ptr<PrimData<Dest>> convertAndMakeShared(const Src &data) {
        return std::make_shared<PrimData<Dest>>(static_cast<Dest>(data));
    }

  public:
    PrimData() = delete;
    PrimData(const T &data) : Data(), data_(data) {
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
        if (auto o = std::dynamic_pointer_cast<PrimData<T>>(other)) {
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
                        return std::make_shared<PrimData<int32_t>>(static_cast<int32_t>(b));
                    case TypeCode::INT64:
                        return std::make_shared<PrimData<int64_t>>(static_cast<int64_t>(b));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimData<float>>(static_cast<float>(b));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimData<double>>(static_cast<double>(b));
                    case TypeCode::STRING:
                        return std::make_shared<StringData>(b ? "true" : "false");
                    case TypeCode::CHAR:
                        return std::make_shared<PrimData<char>>(static_cast<char>(b));

                    default:
                        throw UnsupportedConvError();
                    }
                } else if constexpr (std::is_same_v<T, char>) {
                    const char &c = data_;
                    switch (target->code()) {
                    case TypeCode::INT32:
                        return std::make_shared<PrimData<int32_t>>(static_cast<int32_t>(c));
                    case TypeCode::INT64:
                        return std::make_shared<PrimData<int64_t>>(static_cast<int64_t>(c));
                    case TypeCode::FLOAT:
                        return std::make_shared<PrimData<float>>(static_cast<float>(c));
                    case TypeCode::DOUBLE:
                        return std::make_shared<PrimData<double>>(static_cast<double>(c));
                    case TypeCode::STRING:
                        return std::make_shared<StringData>(std::string(1, c));
                    case TypeCode::BOOL:
                        return std::make_shared<PrimData<bool>>(c != 0);

                    default:
                        throw UnsupportedConvError();
                    }
                } else {
                    static_assert(!std::is_same_v<T, T>, "Unsupported type");
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
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<PrimData<T>>(data_); }
    virtual const std::string toString() const override { return std::to_string(data_); }
};

class StringData : public Data {
  private:
    std::string data_;

  public:
    StringData() = delete;
    StringData(const std::string &data) : Data(stringTypePtr), data_(data) {}

    const std::string &data() const { return data_; }

    virtual bool equals(const data_ptr_t &other) const override {
        if (auto o = std::dynamic_pointer_cast<StringData>(other)) {
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
                    return std::make_shared<PrimData<bool>>(data_.length() > 0);
                }

                default:
                    throw UnsupportedConvError();
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
    virtual data_ptr_t clone(bool deep = false) const override { return std::make_shared<StringData>(data_); }
    virtual const std::string toString() const override {
        std::regex re("\\n");
        return "\"" + std::regex_replace(data_, re, "\\n") + "\"";
    }
};

class StructData : public Data {
  public:
    StructData() = default;
    StructData(type_ptr_t type) : Data(type) {}
    virtual ~StructData() = default;

    virtual bool equals(const data_ptr_t &other) const override = 0;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override = 0;
    virtual data_ptr_t clone(bool deep = false) const override = 0;
    virtual const std::string toString() const override = 0;
};

class SetData : public StructData {
  private:
    std::unordered_set<data_ptr_t> data_;

  public:
    SetData(type_ptr_t elType) : StructData(std::make_shared<SetType>(elType)) {}
    SetData(type_ptr_t elType, data_list_t data) : StructData(std::make_shared<SetType>(elType)), data_(data) {}
    SetData(type_ptr_t elType, const std::unordered_set<data_ptr_t> &data)
        : StructData(std::make_shared<SetType>(elType)), data_(data) {}
    virtual ~SetData() = default;

    bool add(const data_ptr_t &e) { return data_.insert(e).second; }
    bool del(const data_ptr_t &e) { return data_.erase(e) > 0; }

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class MapData : public StructData {
  private:
    std::unordered_map<data_ptr_t, data_ptr_t> data_;

  public:
    MapData(type_ptr_t keyType, type_ptr_t dataType) : StructData(std::make_shared<MapType>(keyType, dataType)) {}
    MapData(type_ptr_t keyType, type_ptr_t dataType, const std::unordered_map<data_ptr_t, data_ptr_t> &data)
        : StructData(std::make_shared<MapType>(keyType, dataType)), data_(data) {}
    virtual ~MapData() = default;

    bool set(const data_ptr_t &key, const data_ptr_t &data) { return data_.insert(std::make_pair(key, data)).second; }
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

class DictData : public StructData {
  private:
    std::unordered_map<std::string, data_ptr_t> data_;

  public:
    DictData() : StructData(std::make_shared<DictType>()) {}
    DictData(std::initializer_list<std::pair<std::string, data_ptr_t>> data);
    DictData(const std::unordered_map<std::string, data_ptr_t> &data);
    virtual ~DictData() = default;

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

class ListData : public StructData {
  private:
    std::vector<data_ptr_t> data_;

  public:
    ListData() : StructData(listTypePtr), data_() {}
    ListData(data_list_t data) : StructData(listTypePtr), data_(data) {}
    ListData(const std::vector<data_ptr_t> &data) : StructData(listTypePtr), data_(data) {}
    virtual ~ListData() = default;

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

class TupleData : public StructData {
  private:
    std::vector<data_ptr_t> data_;

  public:
    TupleData(data_list_t data) : data_(data) {
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<TupleType>(types);
    }
    TupleData(const std::vector<data_ptr_t> &data) : data_(data) {
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<TupleType>(types);
    }
    virtual ~TupleData() = default;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class ArrayData : public StructData {
  private:
    std::vector<data_ptr_t> data_;

  public:
    ArrayData(const std::shared_ptr<ArrayType> &type, const std::vector<data_ptr_t> &data) : data_(data) {
        type_ = type_;
    }
    ArrayData(const std::shared_ptr<ArrayType> &type, data_list_t data = {}) : data_(data) { type_ = type_; }
    ArrayData(type_ptr_t type, size_t length, const std::vector<data_ptr_t> &data) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<ArrayType>(type, length);
        data_.resize(length);
    }
    ArrayData(type_ptr_t type, size_t length, data_list_t data = {}) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<ArrayType>(type, length);
        data_.resize(length);
    }
    virtual ~ArrayData() = default;

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

class VectorData : public StructData {
  private:
    std::vector<data_ptr_t> data_;

  public:
    VectorData(const std::shared_ptr<VectorType> &type, const std::vector<data_ptr_t> &data) : data_(data) {
        type_ = type_;
    }
    VectorData(const std::shared_ptr<VectorType> &type, data_list_t data = {}) : data_(data) { type_ = type_; }
    VectorData(type_ptr_t type, const std::vector<data_ptr_t> &data) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<VectorType>(type);
    }
    VectorData(type_ptr_t type, data_list_t data = {}) : data_(data) {
        // TODO: check element TYPES
        std::vector<type_ptr_t> types;
        for (const auto &d : data) {
            types.push_back(d->type());
        }
        type_ = std::make_shared<VectorType>(type);
    }
    virtual ~VectorData() = default;

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

class ParamsData : public Data {
  private:
    bool typeResolved_ = false;
    std::vector<data_ptr_t> indexData_;
    std::map<std::string, data_ptr_t> namedData_;

  public:
    ParamsData() : Data(std::make_shared<ParamsType>()) {}
    ParamsData(const std::vector<data_ptr_t> &indexData, const std::map<std::string, data_ptr_t> &namedData)
        : Data(std::make_shared<ParamsType>()), indexData_(indexData), namedData_(namedData) {
        std::shared_ptr<ParamsType> type = std::dynamic_pointer_cast<ParamsType>(type_);
        for (const auto &e : indexData) {
            type->add("", e->type(), nullptr);
        }
        for (const auto &e : namedData) {
            type->add(e.first, e.second->type(), nullptr);
        }
    }
    virtual ~ParamsData() = default;

    bool setType(type_ptr_t type);

    bool add(const data_ptr_t &e, const std::string &key = "");

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

class TensorData : public StructData {
  private:
    data_ptr_t data_; // TODO: support multi-dimensional tensor

  public:
    TensorData(const type_ptr_t &elementType, const std::vector<size_t> &shape)
        : StructData(std::make_shared<TensorType>(elementType, shape)) {}
    virtual ~TensorData() = default;

    data_ptr_t at(const std::vector<size_t> &index) const;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};