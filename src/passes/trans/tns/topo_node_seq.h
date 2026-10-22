// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/trans.h"

#include <ostream>
#include <string>
#include <unordered_map>

class TopoNodeSeqDumpPass : public GraphTranslatePass {
    bool showRawPtr = false;
    std::unordered_map<std::string, size_t> ptrCnt_;
    std::unordered_map<std::string, std::unordered_map<uintptr_t, size_t>> ptrsMap_;
    std::string pointerToIdent(const void *ptr, const char *prefix = "N");
    std::string getPtrRepr(const std::string &prefix, uintptr_t ptrVal, bool showRawPtr);

  public:
    TopoNodeSeqDumpPass(const camel::core::context::context_ptr_t &ctx);
    virtual ~TopoNodeSeqDumpPass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
