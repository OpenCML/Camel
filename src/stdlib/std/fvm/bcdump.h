// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/runtime_trans.h"

namespace ctx = camel::core::context;

class BytecodeDumpPass : public RuntimeGraphTranslatePass {
  public:
    BytecodeDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~BytecodeDumpPass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class LinkedBytecodeDumpPass : public RuntimeGraphTranslatePass {
  public:
    LinkedBytecodeDumpPass(const ctx::context_ptr_t &ctx) : RuntimeGraphTranslatePass(ctx) {}
    virtual ~LinkedBytecodeDumpPass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
