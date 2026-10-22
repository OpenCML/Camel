// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/trans.h"
#include "program_emitter.h"

class CppDumpPass : public GraphTranslatePass {
  public:
    CppDumpPass(
        const camel::core::context::context_ptr_t &ctx, camel::passes::cpp::CppEmitMode mode = camel::passes::cpp::CppEmitMode::Plain)
        : GraphTranslatePass(ctx), mode_(mode) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    camel::passes::cpp::CppEmitMode mode_;
};

class CppModuleDumpPass : public CppDumpPass {
  public:
    CppModuleDumpPass(const camel::core::context::context_ptr_t &ctx)
        : CppDumpPass(ctx, camel::passes::cpp::CppEmitMode::Module) {}
};

class CppInspectDumpPass : public CppDumpPass {
  public:
    CppInspectDumpPass(const camel::core::context::context_ptr_t &ctx)
        : CppDumpPass(ctx, camel::passes::cpp::CppEmitMode::Inspect) {}
};

