// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The registry of pass kernels: **the Camel function's path -> the C++ implementation**.
 *
 * A pass is a function of the standard library (design principles 9.3): its name, its
 * documentation and its parameters are one body-less declaration in `stdlib/<module>.cml`, and
 * the pipeline resolves a name by module and function. What C++ contributes is the implementation
 * and nothing else, and it contributes it from the file that implements the pass:
 *
 *     // src/passes/trans/stats/stats.cpp
 *     const camel::execute::PassKernel statsKernel(
 *         "std::stats", camel::execute::makePass<GraphStatsPass>());
 *
 * The registration is made while the program loads, so no file that runs a pipeline names a pass:
 * the driver looks a name up in the registry (`registerModulePass`, camel/execute/pass/params.h)
 * and calls what it finds. A kernel of the core is registered under `std::`, a module's under the
 * module's name, a test-only pass under `test::`; the path is the Camel function's path, which is
 * the same rule for all three.
 */

#pragma once

#include "params.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace camel::execute {

/// Registers the implementation of the pass `path` (`std::gir`, `tensor::fuse`,
/// `test::bounds_flip`) in the pipeline's registry. A later registration of the same path replaces
/// the earlier one (`registerModulePass`).
///
/// This is what `PassKernel` calls; it is public because a file that has to decide at run time
/// which implementation it registers (the Camel pass of a module, `src/execute/camel_pass.cpp`)
/// cannot use the load-time object.
void registerPassKernel(const std::string &path, PassMaker make, bool readsTags = false);

/// Registers a pass whose parameters are a list in C++ and not a declaration: a test-only pass
/// (`test::params`), the one list the pass-declaration guard allows and pins by count. The name
/// and the implementation are registered the same way as any other kernel.
void registerPassKernel(const std::string &path, PassMaker make, std::vector<PassParam> params);

/// One kernel, registered when the program loads, from the source file that implements the pass.
/// The object is the whole of what a pass contributes from C++: `path` is the Camel function's
/// path, `make` builds the implementation from the parameters the declaration resolved, and
/// `readsTags` says the pass reads tag facts (so a tag's `read_by` may name it).
class PassKernel {
  public:
    PassKernel(const char *path, PassMaker make, bool readsTags = false) {
        registerPassKernel(path, std::move(make), readsTags);
    }
    PassKernel(const char *path, PassMaker make, std::vector<PassParam> params) {
        registerPassKernel(path, std::move(make), std::move(params));
    }
};

/// The maker of a pass constructed as `Pass(context, args...)`: the implementation's constructor
/// takes the context and whatever a `with`-parameter list was read into (`makePass<GcTupleMembers>(
/// true)`). A pass whose maker reads its parameters is written as the lambda itself:
///
///     PassKernel profileKernel("std::profile", [](const context_ptr_t &ctx, const PassArgs &args) {
///         ...
///     });
template <class Pass, class... Args> PassMaker makePass(Args &&...args) {
    return [arguments = std::tuple<Args...>(std::forward<Args>(args)...)](
               const camel::core::context::context_ptr_t &ctx,
               const PassArgs &) -> std::unique_ptr<GraphIRPass> {
        return std::apply(
            [&](const auto &...unpacked) { return std::make_unique<Pass>(ctx, unpacked...); },
            arguments);
    };
}

/// A file a pass writes to is a `string?` parameter: null (left out) writes none, which the passes
/// read as the empty path.
std::string pathArgument(const PassArgs &args, const std::string &name);

/// The `constant_budget` parameter of the passes that fold (design principles 9.6): the bytes of
/// constants folding may materialize in a graph. A negative value is read as 0, which materializes
/// none of them. Its declaration is in stdlib/std.cml.
size_t constantBudgetOf(const PassArgs &args);

/// The `settle_runs` parameter of `std::opt`: how many runs of the rewrite in a row may each change
/// the same graph before the pass reports that the rewrite does not settle (see SettleStreak in
/// camel/passes/opt/config.h).
size_t settleRunsOf(const PassArgs &args);

} // namespace camel::execute
