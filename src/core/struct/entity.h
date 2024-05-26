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
#include "value.h"

class Entity : public std::enable_shared_from_this<Entity> {
  protected:
    size_t refs_ = 0;
    bool dangling_ = false;

    // nullptr type means null entity
    type_ptr_t type_ = nullptr;
    value_ptr_t meta_ = nullptr;
    value_ptr_t data_ = nullptr;

  public:
    Entity() : type_(nullptr), meta_(nullptr), data_(nullptr) {}
    Entity(type_ptr_t type, value_ptr_t data, value_ptr_t meta = nullptr) : type_(type), meta_(meta), data_(data) {}
    virtual ~Entity() = default;

    bool dangling() const { return dangling_; }

    type_ptr_t type() const { return type_; }
    value_ptr_t meta() const { return meta_; }
    value_ptr_t data() const { return data_; }

    virtual bool resolved() const { return data_->resolved(); }
    virtual void resolve() { data_->resolve(); }
    virtual void pending() { data_->pending(); }

    virtual std::string typeStr() const { return type_->toString(); }
    virtual std::string metaStr() const { return meta_->toString(); }
    virtual std::string dataStr() const { return data_->toString(); }

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
    bool invalid() const { return refs_ == 0; }

    bool isNull() const { return type_ == nullptr; }
    bool hasMeta() const { return meta_ != nullptr; }
    bool hasData() const { return data_ != nullptr; }

    void setMeta(value_ptr_t meta) { meta_ = meta; }
    void setData(value_ptr_t data) { data_ = data; }

    const entity_ptr_t convert(type_ptr_t target, bool inplace = false) {
        const value_ptr_t newData = data_->convert(target, inplace);
        if (inplace) {
            data_ = newData;
            return shared_from_this();
        } else {
            return std::make_shared<Entity>(target, newData, meta_);
        }
    }
};

// definition below is forwarded to value.h
// using entity_ptr_t = std::shared_ptr<const Entity>;

class DanglingEntity : public Entity {
  private:
    std::string ref_;

  public:
    DanglingEntity() = delete;
    DanglingEntity(const std::string &ref) : Entity() { dangling_ = true; }
    virtual ~DanglingEntity() = default;

    virtual std::string typeStr() const override { return "DREF<" + ref_ + ">"; }
    virtual std::string metaStr() const override { return "DREF<" + ref_ + ">"; }
    virtual std::string dataStr() const override { return "DREF<" + ref_ + ">"; }
};