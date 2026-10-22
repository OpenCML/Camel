// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "./base.h"
#include "camel/core/data/base.h"

namespace data = camel::core::data;

slot_t makeSlotFromPrimitiveData(const data::data_ptr_t &data);
camel::core::rtdata::Object *
makeGCRefFromGCTracedData(const data::data_ptr_t &data, camel::core::mm::IAllocator &allocator);
