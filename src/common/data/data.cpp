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
 * Created: Oct. 6, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "data.h"

#include "common/entity.h"

using namespace std;

Data::Data() {};

Data::Data(type_ptr_t type) : type_(type) {}

type_ptr_t Data::type() const { return type_; }

entity_ptr_t Data::entity() {
    if (entity_.expired()) {
        entity_ptr_t entity = make_shared<Entity>(shared_from_this());
        entity_ = entity;
        return entity;
    }
    return entity_.lock();
}

void Data::setEntity(const entity_ptr_t &entity) { entity_ = entity; }

bool Data::variable() const { return mutable_; }

void Data::setVariable() { mutable_ = true; }

vector<string> Data::refs() const { return vector<string>(); }

bool Data::resolved() const { return true; }

void Data::resolve(const data_vec_t &dataList) {}

bool Data::equals(const data_ptr_t &other) const { throw runtime_error("Base Data::equals() not implemented"); }

data_ptr_t Data::clone(bool deep) const { throw runtime_error("Base Data::clone() not implemented"); }

data_ptr_t Data::convert(type_ptr_t target, bool inplace) {
    throw runtime_error("Base Data::convert() not implemented");
}

const string Data::toString() const { throw runtime_error("Base Data::toString() not implemented"); }