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
 * Updated: Nov. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/type.h"
#include "utils/type.h"

#include <ostream>

class UnsupportedConvError : public std::exception {};

class DataConvError : public std::exception {
  private:
    std::string message_;

  public:
    DataConvError() = delete;
    DataConvError(const std::string &message) { message_ = "Data conversion failed: " + message; }

    virtual const char *what() const noexcept override { return message_.c_str(); }
};

template <typename Derived, typename T> class RawData {
  protected:
    mutable T data_;

  public:
    RawData() : data_() {}

    template <typename... Args> RawData(Args &&...args) : data_(std::forward<Args>(args)...) {}

    RawData(const T &value) : data_(value) {}
    RawData(T &&value) : data_(std::move(value)) {}

    // 拷贝构造
    RawData(const RawData &other) : data_(other.data_) {}

    // 移动构造
    RawData(RawData &&other) noexcept : data_(std::move(other.data_)) {}

    Derived &operator=(const RawData &other) {
        data_ = other.data_;
        return static_cast<Derived &>(*this);
    }

    Derived &operator=(RawData &&other) noexcept {
        data_ = std::move(other.data_);
        return static_cast<Derived &>(*this);
    }

    const T &data() const { return data_; }
    T &data() { return data_; }

    std::string toString() const { return static_cast<Derived *>(this)->toString(); }
};

template <typename T> using data_rptr_t = RawData<T> *;

class Data : public std::enable_shared_from_this<Data> {
  protected:
    type_ptr_t type_;

  public:
    Data();
    Data(type_ptr_t type);
    virtual ~Data() = default;

    virtual type_ptr_t type() const;

    static data_ptr_t null();
    virtual bool isNull() const; // check if this is a null data
    virtual bool isZero() const; // check if this is a zero data
    virtual std::vector<std::string> refs() const;
    virtual bool resolved() const;                    // check if all data references are resolved
    virtual void resolve(const data_vec_t &dataList); // resolve data references by dataList

    virtual bool equals(const data_ptr_t &other) const;
    virtual data_ptr_t clone(bool deep = false) const;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false);

    template <typename Target> std::shared_ptr<Target> as(type_ptr_t target, bool inplace = false) {
        const data_ptr_t newData = convert(target, inplace);
        ASSERT(tt::as_shared<Target>(newData), "type cast failed");
        return tt::as_shared<Target>(newData);
    }

    virtual const std::string toString() const;
    virtual void print(std::ostream &os) const;
};

class AnyData;
class RefData;
class NullData;
class StringData;

class StructData;
class SetData;
class MapData;
class StructData;
class ListData;
class VectorData;
class TupleData;
class ArrayData;
class ParamsData;
class TensorData;
