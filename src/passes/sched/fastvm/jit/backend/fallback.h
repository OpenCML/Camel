// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "backend.h"

namespace camel::jit {

class FallbackBackend : public IJitBackend {
  public:
    std::unique_ptr<CompiledCode>
    compile(const CompilationUnit &, std::string *failureReason = nullptr) override {
        if (failureReason)
            *failureReason = "JIT disabled (fallback backend)";
        return nullptr;
    }
    std::vector<JitBlocker> blockers(const CompilationUnit &) const override {
        return {{0, "no-backend", "JIT disabled (fallback backend)", {}}};
    }
    std::vector<JitBlocker> runtimeConstructs(const CompilationUnit &) const override {
        return {};
    }
    void registerTrampoline(const char *, void *) override {}
    JitEntryFn load(std::unique_ptr<CompiledCode>) override { return nullptr; }
    void unload(JitEntryFn) override {}
};

} // namespace camel::jit
