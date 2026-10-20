/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Public region compilation pass. Device selection happens at execution time.
#include "camel/execute/pass/opt.h"
#include "tiles/internal/regions.h"
#include <memory>
#include <utility>

namespace camel::tiles {
namespace {
class RegionPass final : public RuntimeGraphRewritePass {
    RegionDeviceOps preferred_;

  public:
    RegionPass(const core::context::context_ptr_t &context, RegionDeviceOps preferred)
        : RuntimeGraphRewritePass(context), preferred_(std::move(preferred)) {}
    const runtime::Program *apply(const runtime::Program *program, std::ostream &) override {
        size_t count = 0;
        return outlineRegions(
            *context_,
            program,
            program->root(),
            preferred_.execute,
            preferred_.executeUri,
            count,
            &preferred_);
    }
};
} // namespace
void registerRegionPass(RegionDeviceOps preferred) {
    registerModulePass("tiles::compile", [preferred](const core::context::context_ptr_t &context) {
        return std::make_unique<RegionPass>(context, preferred);
    });
}
} // namespace camel::tiles
