/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Oct. 6, 2024
 * Updated: Oct. 07, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../type.h"

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
    Data();
    Data(type_ptr_t type);
    virtual ~Data() = default;

    virtual type_ptr_t type() const;
    entity_ptr_t entity();
    void setEntity(const entity_ptr_t &entity);

    bool variable() const;
    void setVariable();

    virtual std::vector<std::string> refs() const;
    virtual bool resolved() const;      // check if all data references are resolved
    virtual void resolve(const data_vec_t &dataList); // resolve data references by dataList

    virtual bool equals(const data_ptr_t &other) const;
    virtual data_ptr_t clone(bool deep = false) const;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false);

    virtual const std::string toString() const;
};

class AnyData;
class RefData;
class NullData;
class StringData;

class StructData;
class SetData;
class MapData;
class DictData;
class ListData;
class ArrayData;
class TupleData;
class VectorData;
class ParamsData;
class TensorData;
