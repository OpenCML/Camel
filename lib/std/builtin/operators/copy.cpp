// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "copy.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"

using namespace camel::core::rtdata;
namespace mm = camel::core::mm;

// The COPY node kind's execution, written as an operator (plan 2.3, step 1). The three backends
// already agree on what a copy is -- `lib/std/nvm/nodevm.cpp` (the COPY arm), `lib/std/fvm/gotovm.cpp`
// (`label_COPY`), `lib/std/prl/taskflow.cpp` -- and all three ask the same two questions of the
// value: is its type one the collector traces, and if so what is its static type. An operator is
// given both (`ArgsView::code`/`ArgsView::type`), so the answer here is theirs, not a second rule:
// a traced value is cloned under its own type, and anything else is handed on as the slot it is.
slot_t __copy__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    (void)with;
    (void)ctx;
    const camel::core::type::Type *valueType = norm.type(0);
    const slot_t value                       = norm.slot(0);
    if (valueType == nullptr || !valueType->isGCTraced()) {
        return value;
    }
    Object *object = fromSlot<Object *>(value);
    if (object == nullptr) {
        return value;
    }
    return toSlot(object->clone(mm::autoSpace(), valueType, false));
}