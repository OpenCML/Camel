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
 * Created: Dec. 07, 2025
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "./base.h"
#include "compile/gir.h"
#include "core/data/base.h"

slot_t makeSlotFromPrimitiveData(const data_ptr_t &data);
Object *makeGCRefFromGCTracedData(const data_ptr_t &data, IAllocator &allocator);

/** 根据类型为槽容器附着 layout，并递归处理嵌套的容器。仅当存储到带类型的 slot 时使用。 */
void attachLayoutFromType(Object *obj, Type *type);
