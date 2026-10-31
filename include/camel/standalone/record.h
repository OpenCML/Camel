// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The records of a program written by std::cpp: a struct or a tuple, held as a plain C++
 * aggregate with one member per field (docs/design/cpp-standalone/README.cn.md, section 4).
 * A record is a value, so every change of one field makes another record (design principles 5.2).
 */

#pragma once

#include <utility>

namespace camel::standalone {

/// `s.field = value` (:op/set_field, src/builtin/operators/set_value.cpp): the record with that
/// field changed. The record the caller had is untouched -- it is a value, and the store is a
/// copy of it, as the runtime's plain form of the store is.
template <typename Record, typename Field, typename Value>
Record setField(Record record, Field Record::*field, Value &&value) {
    record.*field = std::forward<Value>(value);
    return record;
}

} // namespace camel::standalone