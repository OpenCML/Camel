/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 11, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "cpp_export.h"

#include "runtime_emitter.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"

namespace {

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

/// Whether the entry function takes parameters: the entry graph has ports of its own, or
/// (their values were bound) it calls the entry function with arguments. A `main` that only
/// captures is called through a closure (FILL), which the emitter handles.
bool entryTakesParameters(const GCGraph *root) {
    if (!root->withPorts().empty() || !root->normPorts().empty()) {
        return true;
    }
    const auto callRef = root->returnNodeRef();
    const auto *call   = root->node(callRef);
    if (!call || call->kind != GCNodeKind::Call) {
        return false;
    }
    const auto with = root->withInputsOf(callRef);
    if (with.empty()) {
        return false;
    }
    const auto *callee = root->node(with.front());
    return callee && callee->kind != GCNodeKind::Fill &&
           (with.size() > 1 || !root->normInputsOf(callRef).empty());
}

} // namespace

camel::runtime::GCGraph *CppDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    ASSERT(graph != nullptr, "C++ dump requires a non-null runtime root graph.");

    // The generated program's `int main()` calls the entry function with nothing; where the
    // arguments of a Camel `main` with parameters would come from in the C++ program is not
    // defined yet.
    if (entryTakesParameters(graph)) {
        auto diag =
            context_->rtmDiags()->of(camel::core::error::RuntimeDiag::TranslationRefused);
        if (const uint64_t origin = camel::execute::entryFunctionOrigin(*context_);
            origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(
            "std::cpp",
            "'main' takes parameters, and std::cpp does not translate the parameters of the "
            "entry function yet (the C++ program it writes calls 'main' with no arguments)");
        return nullptr;
    }

    RuntimeCppEmitter emitter(context_, mode_);
    os << emitter.emit(graph);
    return nullptr;
}
