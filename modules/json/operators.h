// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include <string>
#include <unordered_map>

namespace ctx = camel::core::context;

// 算子实现
slot_t __json_encode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_decode__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// Decoding to the type the call names (`decode_as::<T>`); the result is a `T!`.
slot_t __json_decode_as__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
/// Looking into a Json (value.h): its kind, and its payload as a `T?` of the type the kind
/// has; `get<key>` / `get<index>` a member or an element as a `Json?`. All but kind and
/// is_null also take a `Json?`, and give null for a null.
slot_t __json_kind__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_bool__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_int__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_float__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_string__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_array__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_as_object__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __json_get__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
std::unordered_map<std::string, operator_t> getJsonOpsMap();
