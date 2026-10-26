// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The derivative registry is one per process, like the allocator. The core registers its tangent
 * spaces every time a `Context` is built; a module (the tensor module's `tensor:add` space for the
 * values it owns) registers its own once. This pass reads what the registry holds before and after
 * a second context is created beside the running one, and prints one line for the test plan:
 *
 *   DERIVATIVES tensor=<n> before_total=<n> after_total=<n>
 *
 * `tensor` is how many registered spaces carry `add=tensor:add` and `owns=subset` -- the module's
 * own entry, which the second context must leave alone -- and the totals are every registered
 * space, which the second context must not add to either: the core's three entries (f64, f32, the
 * array code) are replaced in place.
 *
 * Before the fix a registration that owned every value of its code cleared the whole code, so the
 * second context dropped `tensor:add` and the values the tensor module owns fell back to the
 * generic slot-wise array space for the rest of the process: `tensor` read 0 and the totals grew
 * by the core's three entries.
 */

#include "camel/passes/test/derivatives.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/diagnostics.h"

#include <string>
#include <string_view>

namespace {

constexpr std::string_view kModuleSpace = "add=tensor:add";

size_t countModuleSpaces() {
    size_t count = 0;
    for (const std::string &space :
         camel::core::DerivativeRegistry::instance().tangentSpaceDescriptions()) {
        if (space.find(kModuleSpace) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

size_t countSpaces() {
    return camel::core::DerivativeRegistry::instance().tangentSpaceDescriptions().size();
}

} // namespace

const camel::runtime::Program *
DerivativesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::core::context;
    const size_t tensor       = countModuleSpaces();
    const size_t beforeTotal  = countSpaces();
    size_t afterTensor        = 0;
    size_t afterTotal         = 0;
    {
        // A second context: it registers the core's spaces again, and the module's entries are
        // registered once and cannot be taken back.
        context_ptr_t other = Context::create(EntryConfig{}, camel::core::error::DiagsConfig{});
        afterTensor         = countModuleSpaces();
        afterTotal          = countSpaces();
    }
    os << "DERIVATIVES tensor=" << tensor << " before_total=" << beforeTotal
       << " after_total=" << afterTotal << " after_tensor=" << afterTensor << "\n";
    return program;
}