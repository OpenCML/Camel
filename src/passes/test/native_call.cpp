/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Replace only the explicit fixture helper with an original-graph native callback.
#include "native_call.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/execute/executor.h"
#include "camel/execute/function_call.h"
#include "camel/runtime/draft_session.h"
#include <array>
#include <stdexcept>

namespace {
slot_t invokeOriginal(ArgsView &, ArgsView &norm, camel::core::context::Context &context) {
    const std::array<slot_t, 1> arguments{norm.slot(1)};
    return camel::execute::invokeFunction(context, norm.get<::Function *>(0), {}, arguments);
}
} // namespace

const camel::runtime::Program *
NativeCallbackTestPass::apply(const camel::runtime::Program *program, std::ostream &) {
    namespace rt       = camel::runtime;
    constexpr auto uri = "test:native_callback";
    camel::core::OperatorTable::instance().set(
        uri,
        {.pure = false, .callsGraphs = true, .inspectsGraphs = true});
    // Schedulers resolve the URI through the executor manager, independently of
    // the function pointer retained in the graph payload.
    if (!context_->execMgr().findWithoutImport(uri))
        context_->registerExecutorFactory("test", [context = context_] {
            return std::make_shared<Executor>(
                context,
                std::unordered_map<std::string, operator_t>{{"native_callback", &invokeOriginal}});
        });
    rt::RuntimeGraphDraftSession session(context_, program);
    bool changed = false;
    for (auto *owner : session.collectReachableRuntimeGraphs()) {
        for (rt::gc_node_ref_t id = 0; id < owner->nodeCount(); ++id) {
            if (owner->node(id)->kind != rt::GCNodeKind::Func)
                continue;
            auto *callee = session.work().calleeOf(*owner, id);
            if (!callee || callee->name() != "native_callback")
                continue;
            if (owner->normInputsOf(id).size() != 2 || !owner->withInputsOf(id).empty())
                throw std::runtime_error("test native_callback helper must take (function, int)");
            auto &draft = session.edit(owner);
            auto *type  = owner->node(id)->dataType();
            const auto call =
                draft.addOperNode(type, &invokeOriginal, uri, draft.allocateRuntimeSlot(type));
            // Earlier replacements in this owner may already have forwarded the
            // current call's inputs. The frozen owner still names erased nodes.
            draft.setNormInputs(call, draft.normInputsOf(id));
            draft.setCtrlInputs(call, draft.ctrlInputsOf(id));
            draft.setNodeOrigin(call, owner->nodeOrigin(id));
            draft.replaceAllValueUses(id, call);
            draft.replaceAllCtrlUses(id, call);
            if (draft.outputNode() == id)
                draft.setOutputNode(call);
            if (draft.exitNode() == id)
                draft.setExitNode(call);
            if (draft.returnNode() == id)
                draft.setReturnNode(call, draft.returnKind());
            draft.eraseNode(id);
            changed = true;
        }
    }
    return changed ? session.commit() : program;
}
