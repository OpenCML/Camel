// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The test channel: the passes a plan names as `test::<name>`, which no program can reach.
 *
 * These passes were compiled into libcamel until 2026-11-25, by the `lib_module` block's glob over
 * this directory. That put the whole test surface inside the language's own library: every one of
 * them shipped with every program, and a symbol one of them needed became an export of libcamel
 * whether or not anything in the language used it. They are their own `.cmo` now, on the same
 * footing as `test_support`, and libcamel carries none of them.
 *
 * Two things make that safe, and neither is a registration written here:
 *
 *   * the passes register themselves when this library is loaded -- each implementation file ends
 *     with a `PassKernel` in an anonymous namespace, whose constructor calls
 *     `registerModulePass("test::<name>", ...)` against libcamel's registry, and a `.cmo` is a
 *     shared library whose static initialisers run when it is opened;
 *   * the pipeline opens it on demand: a pass name of the form `<module>::<pass>` that no loaded
 *     implementation answers is looked up by importing that module and asking again
 *     (`src/kernel/execute/pass.cpp#resolvePassWithModule`). So `camel program.cml
 *     test::bounds_flip` loads `test.cmo` the same way `tiles::compile` loads `tiles.cmo`, and a
 *     program that names no `test::` pass never loads it.
 *
 * This module declares nothing. The passes are not names a program exports into a module, no `.cml`
 * declares them, and the pass-declaration check exempts the `test` module for exactly that reason:
 * `isTestOnly` reads the module name (`test/tools/check-pass-declarations.mjs`). A `.cml` here
 * would have to spell out fifty signatures that exist to be called by a plan and nothing else.
 */

#include "camel/core/context/context.h"
#include "camel/core/module/builtin.h"

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

class TestPassesModule final : public BuiltinModule {
  public:
    explicit TestPassesModule(context_ptr_t ctx) : BuiltinModule("test", ctx) {}
    ~TestPassesModule() override = default;

    /// Nothing to set up: the passes are in this library's static initialisers, and this module
    /// exists so the loader has a name to answer and a file to open. It is not a formality -- the
    /// load is what the loader counts as "this module is usable", and a `test::` name that resolves
    /// to a module which refused to load is an error the pipeline reports, not a pass it skips.
    bool load() override {
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" {
Module *camel_module_create(Context *ctx) { return new TestPassesModule(ctx->shared_from_this()); }
}