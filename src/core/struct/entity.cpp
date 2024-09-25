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
 * Created: May. 5, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "entity.h"
#include "data.h"

using namespace std;

Entity::Entity(data_ptr_t data) : data_(data) {
    if (data_ != nullptr) {
        type_ = data_->type();
    }
}

data_ptr_t Entity::meta() {
    if (meta_ == nullptr) {
        // Create a new value
        // TODO
        throw runtime_error("Entity::meta() not implemented");
        meta_ = make_shared<Data>();
        return meta_;
    }
    return meta_;
}

entity_ptr_t Entity::clone(bool deep) const {
    entity_ptr_t entity = make_shared<Entity>(data_->clone(deep));
    entity->type_ = type_;
    entity->meta_ = meta_->clone(deep);
    return entity;
}

string Entity::typeStr() const {
    auto type = type_.lock();
    if (type == nullptr) {
        return "NULL";
    } else {
        return type->toString();
    }
}

string Entity::metaStr() const {
    if (meta_ == nullptr) {
        return "NULL";
    } else {
        return meta_->toString();
    }
}

string Entity::dataStr() const {
    if (data_ == nullptr) {
        return "NULL";
    } else {
        return data_->toString();
    }
}

const entity_ptr_t Entity::convert(type_ptr_t target, bool inplace = false) {
    const data_ptr_t newData = data_->convert(target, inplace);
    if (inplace) {
        data_ = newData;
        return shared_from_this();
    } else {
        auto entity = newData->entity();
        entity->setMeta(meta_);
        return entity;
    }
}

bool Entity::equals(const entity_ptr_t &other) const {
    // TODO: implement equals
    return data_ == other->data_ || (data_ != nullptr && other->data_ != nullptr && data_->equals(other->data_));
}

void Entity::setMeta(data_ptr_t meta) { meta_ = meta; }
void Entity::setData(data_ptr_t data) {
    data_ = data;
    if (data_ != nullptr) {
        type_ = data_->type();
        data_->setEntity(shared_from_this());
    }
}