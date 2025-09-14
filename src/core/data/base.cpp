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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"

#include "core/data/entity.h"
#include "other/null.h"
#include "utils/assert.h"

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

data_ptr_t Data::null() {
    static data_ptr_t nullData = nullptr;
    if (nullData == nullptr) {
        nullData = make_shared<NullData>();
    }
    return nullData;
}

vector<string> Data::refs() const { return vector<string>(); }

bool Data::resolved() const { return true; }

void Data::resolve(const data_vec_t &dataList) {
    ASSERT(false, "Base Data::resolve() not implemented");
}

bool Data::equals(const data_ptr_t &other) const {
    ASSERT(false, "Base Data::equals() not implemented");
    return false;
}

data_ptr_t Data::clone(bool deep) const {
    ASSERT(false, "Base Data::clone() not implemented");
    return nullptr;
}

data_ptr_t Data::as(type_ptr_t target, bool inplace) {
    ASSERT(false, "Base Data::as() not implemented");
    return nullptr;
}

const string Data::toString() const {
    ASSERT(false, "Base Data::toString() not implemented");
    return "<Data>";
}
