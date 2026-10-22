// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"

#include <iostream>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;

slot_t __this_zen__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string zen =
#include "ZEN"
        ;
    String *str = String::from(zen, mm::autoSpace());
    return toSlot(str);
}

std::unordered_map<std::string, operator_t> getThisOpsMap() { return {{"zen", __this_zen__}}; }
