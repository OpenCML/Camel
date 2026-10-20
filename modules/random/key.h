/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The value type of keyed random operators: a 128-bit key, `{ hi: int, lo: int }`.
 *
 * The bits are the value; the two fields are the high and low halves, both printed and compared
 * as signed int64. The design's opaque `Key` (docs/design/training/README.cn.md 4.1) awaits a
 * nominal type declaration (H5); until then a struct with one canonical field layout is the
 * representation every back end can carry.
 */

#pragma once

#include "camel/core/type.h"

namespace camel::random {

/// The Key struct type, interned: every call, signature and back end sees the same object.
camel::core::type::Type *keyType();

} // namespace camel::random