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

class Entity : public std::enable_shared_from_this<Entity> {
  protected:
    size_t refs_ = 0;

    // nullptr type means null entity
    type_wptr_t type_;
    value_ptr_t meta_ = nullptr;
    value_ptr_t data_ = nullptr;

  public:
    Entity() = delete;
    Entity(value_ptr_t data);
    virtual ~Entity() = default;

    type_ptr_t type() const { return type_.lock(); }
    value_ptr_t data() const { return data_; }
    value_ptr_t meta();

    virtual std::string typeStr() const;
    virtual std::string metaStr() const;
    virtual std::string dataStr() const;

    virtual std::string toString() const { return typeStr() + ", " + metaStr() + ", " + dataStr(); }

    virtual bool equals(const entity_ptr_t &other) const;

    void ref() { refs_++; }
    void unref() {
        if (refs_ > 0) {
            refs_--;
        }
    }
    size_t refs() const { return refs_; }

    bool unique() const { return refs_ == 1; }
    bool shared() const { return refs_ > 1; }
    bool expired() const { return refs_ == 0; }
    bool valid() const { return refs_ > 0; }

    bool isNull() const { return data_ != nullptr; }
    bool hasMeta() const { return meta_ != nullptr; }

    void setMeta(value_ptr_t meta);
    void setData(value_ptr_t data);

    const entity_ptr_t convert(type_ptr_t target, bool inplace);
};

// definition below is forwarded to type.h
// using value_ptr_t = std::shared_ptr<Value>;
// using value_wptr_t = std::weak_ptr<Value>;