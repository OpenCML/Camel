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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "data.h"
#include "utils/assert.h"

using namespace std;

Data::Data() {};

Data::Data(Type *type) : type_(type) {}

Type *Data::type() const { return type_; }

data_ptr_t Data::null() {
    static data_ptr_t nullData = nullptr;
    if (nullData == nullptr) {
        nullData = make_shared<NullData>();
    }
    return nullData;
}
