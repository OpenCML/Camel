// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The value type of keyed random operators: a 128-bit key, `{ hi: int, lo: int }`.
 *
 * The bits are the value; the two fields are the high and low halves, both printed and compared
 * as signed int64. The design's opaque `Key` (docs/design/training/README.cn.md 4.1) awaits a
 * nominal type declaration; until then a struct with one canonical field layout is the
 * representation every back end can carry.
 */

#pragma once

#include "camel/core/type.h"

namespace camel::random {

/// The Key struct type, interned: every call, signature and back end sees the same object.
camel::core::type::Type *keyType();

} // namespace camel::random