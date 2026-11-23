// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The records of a program written by std::cpp: a struct or a tuple, held as a plain C++
 * aggregate with one member per field (docs/design/cpp-standalone/README.cn.md, section 4).
 * A record is a value, so every change of one field makes another record (design principles 5.2).
 */

#pragma once

#include "camel/standalone/opt.h"

#include <utility>

namespace camel::standalone {

/// The range `a..b` as the value of its own type (`:range/to_range`, design principles 4.2, 5.4):
/// the record of the standard library's `Range`, whose two ends are `int?` and whose step is an
/// `int`. The array of the same integers is `range(a, b, s)`, and it keeps its own form (array.h);
/// the two are not one value, so this is the only place the record's shape is written in C++.
template <typename Record> Record makeRange(int64_t start, int64_t stop, int64_t step) {
    return Record{Opt<int64_t>(start), Opt<int64_t>(stop), step};
}

/// `s.field = value` (:op/set_field, src/builtin/operators/set_value.cpp): the record with that
/// field changed. The record the caller had is untouched -- it is a value, and the store is a
/// copy of it, as the runtime's plain form of the store is.
template <typename Record, typename Field, typename Value>
Record setField(Record record, Field Record::*field, Value &&value) {
    record.*field = std::forward<Value>(value);
    return record;
}

} // namespace camel::standalone