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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __json_encode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_decode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// Decoding to the type of a shape argument; the result is a `T!`
/// (DESIGN-QUESTION(json-typed-decode)).
slot_t __json_decode_as__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// Looking into a Json (value.h): its kind, and its payload as a `T?` of the type the kind
/// has; `get<key>` / `get<index>` a member or an element as a `Json?`. All but kind and
/// is_null also take a `Json?`, and give null for a null.
slot_t __json_kind__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_is_null__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_bool__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_int__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_float__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_string__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_array__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_object__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_get__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// Whether decode_as can produce a value of `type`.
bool jsonTypeHasForm(camel::core::type::Type *type);
/// Whether encode can write a value of `type`: exactly the types camelToJson handles.
bool jsonTypeEncodable(camel::core::type::Type *type);

std::unordered_map<std::string, operator_t> getJsonOpsMap();
