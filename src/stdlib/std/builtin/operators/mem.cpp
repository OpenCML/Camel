// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "mem.h"

#include "camel/core/mm/block.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/type/composite/struct.h"
#include "camel/utils/assert.h"

using namespace camel::core::type;

StructType *memBlocksStructType() {
    // Built once: the resolver's result type and the value the operator constructs must be the
    // same StructType object (the run-time struct writes its fields through it).
    static StructType *type = [] {
        StructTypeFactory factory;
        factory.add("allocs", Type::Int64());
        factory.add("frees", Type::Int64());
        factory.add("live", Type::Int64());
        factory.add("bytes", Type::Int64());
        return factory.build();
    }();
    return type;
}

slot_t __builtin__mem_blocks__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    (void)with;
    (void)ctx;
    const auto *structType = dynamic_cast<const StructType *>(norm.resultType());
    ASSERT(
        structType != nullptr && structType->size() == 4,
        "std::mem::blocks() must have its four-field struct as its result type");
    Struct *result = Struct::create(structType->size(), camel::core::mm::autoSpace());
    result->set<rtdata::Int64>(
        0, static_cast<rtdata::Int64>(camel::core::mm::blockCreations()), structType);
    result->set<rtdata::Int64>(
        1, static_cast<rtdata::Int64>(camel::core::mm::blockFrees()), structType);
    result->set<rtdata::Int64>(
        2, static_cast<rtdata::Int64>(camel::core::mm::blockLiveCount()), structType);
    result->set<rtdata::Int64>(
        3, static_cast<rtdata::Int64>(camel::core::mm::blockBytesLive()), structType);
    return rtdata::toSlot(result);
}