/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Test region rewriting through ordinary CPU calls and explicit GPU execution.
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/function_call.h"
#include "camel/execute/pass/opt.h"
#include "tiles/internal/regions.h"
#include <cstdlib>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
namespace camel::tiles {
// Invoke twice and re-read the operator views after the first nested call. Regions
// are pure, so the discarded result is not an observable extra effect.
slot_t testNativeCall(ArgsView &with, ArgsView &norm, core::context::Context &context) {
    auto invoke = [&]() {
        auto *function = with.get<::Function *>(0);
        std::vector<slot_t> args;
        if (function->graph()->normPorts().size() == 1) {
            args.push_back(norm.slot(0));
        } else {
            auto *tuple = norm.get<::Tuple *>(0);
            for (size_t i = 0; i < function->graph()->normPorts().size(); ++i)
                args.push_back(tuple->get<slot_t>(i));
        }
        return execute::invokeFunction(context, function, {}, args);
    };
    (void)invoke();
    return invoke();
}
namespace {
class RegionOutlineTestPass final : public RuntimeGraphRewritePass {
    operator_t run_;

  public:
    RegionOutlineTestPass(const core::context::context_ptr_t &context, operator_t run = nullptr)
        : RuntimeGraphRewritePass(context), run_(run) {}
    runtime::GCGraph *apply(runtime::GCGraph *root, std::ostream &os) override {
        size_t count = 0;
        auto *result = outlineRegions(
            *context_,
            root,
            run_,
            run_ == &testNativeCall ? "tiles:test_call" : "tiles:run",
            count);
        os << "TILES_OUTLINED " << count << '\n';
        return result;
    }
};
} // namespace
void registerRegionTestPasses(operator_t run) {
    if (std::getenv("CAMEL_ENABLE_TEST_PASSES")) {
        core::OperatorTable::instance().set(
            "tiles:test_call",
            {.pure = false, .callsGraphs = true});
        registerModulePass(
            "tiles::test_outline_native",
            [](const core::context::context_ptr_t &ctx) {
                return std::make_unique<RegionOutlineTestPass>(ctx, &testNativeCall);
            });
        registerModulePass("tiles::test_outline", [](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx);
        });
        registerModulePass(
            "tiles::test_outline_gpu",
            [run](const core::context::context_ptr_t &ctx) {
                return std::make_unique<RegionOutlineTestPass>(ctx, run);
            });
    }
}
} // namespace camel::tiles
