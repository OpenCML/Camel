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
 * Updated: Dec. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "ref.h"
#include "utils/assert.h"

RefData::RefData(const std::string &ref) : Data(Type::Ref()), ref_(ref) {}

const std::string &RefData::ref() const { return ref_; }

bool RefData::equals(const data_ptr_t &other) const { return false; }

data_ptr_t RefData::clone(bool deep) const { return std::make_shared<RefData>(ref_); }

const std::string RefData::toString() const { return "DREF<" + ref_ + ">"; }
