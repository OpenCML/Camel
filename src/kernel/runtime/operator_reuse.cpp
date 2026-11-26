// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `camel inspect --reuse` (camel/runtime/operator_reuse.h): one line per operator node saying how
 * many places read its value and after which position it is never read again, plus what the run
 * itself recorded about storage reuse. Nothing here decides anything and nothing here allocates for
 * the program: it reads the plan the compiler already built and the counters the profile already
 * keeps.
 */

#include "camel/runtime/operator_reuse.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "camel/core/error/diagnostics/range.h"
#include "camel/core/operator_table.h"
#include "camel/core/source/ids.h"
#include "camel/core/source/manager.h"
#include "camel/profile/profile.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

namespace camel::runtime {

namespace {

/// `file:line:column` of the node, or `<graph>@<node>` when it has no source position. The same
/// shape `--copies` prints, so a row of either reading can be found in the other by its first
/// field.
std::string
placeOf(const GCGraph &graph, gc_node_ref_t ref, const camel::source::SourceContext *sources) {
    const uint64_t origin = graph.nodeOrigin(ref);
    if (sources != nullptr && origin != 0) {
        const camel::source::OriginRecord *record = sources->origin(origin);
        if (record != nullptr && record->primarySpan != camel::source::kInvalidSpanId) {
            const camel::core::error::CharRange range = sources->resolveOrigin(origin);
            std::string path                          = sources->pathForOrigin(origin);
            const size_t separator                    = path.find_last_of("/\\");
            if (separator != std::string::npos) {
                path.erase(0, separator + 1);
            }
            return path + ":" + std::to_string(range.start.line + 1) + ":" +
                   std::to_string(range.start.character + 1);
        }
    }
    return graph.name() + "@" + std::to_string(ref);
}

/// The name the program writes for an operator: its call shape when the table states one, its URI
/// as written otherwise. `--copies` answers the same question the same way
/// (`updateName`, src/kernel/runtime/storage_reuse.cpp, which is that file's own helper); this one
/// is here rather than shared because the two readings answer different questions and neither owns
/// the table.
std::string operatorName(const camel::core::OperatorTable &table, std::string_view uri) {
    const auto writtenName = [&](std::string_view at) -> std::optional<std::string> {
        if (const std::optional<camel::core::OperatorTable::CallForm> form = table.callFormOf(at);
            form.has_value() && !form->name.empty()) {
            return form->name;
        }
        return std::nullopt;
    };
    if (std::optional<std::string> name = writtenName(uri); name.has_value()) {
        return *name;
    }
    return std::string(uri);
}

} // namespace

void dumpOperatorReuse(
    const Program &program, std::ostream &os, const camel::source::SourceContext *sources) {
    const auto &table = camel::core::OperatorTable::instance();
    for (auto *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        // The two shapes with nothing to read: a graph with no body, and one whose values have
        // several writings and so no plan yet. Neither is silence (--copies says the same of
        // itself, in its own words).
        if (!graph->hasNodePayload() || !graph->executable()) {
            os << "REUSE " << graph->name() << " no-plan\n";
            continue;
        }
        const ExecPlan &plan    = graph->execPlan();
        const GCUseIndex &users = graph->uses();
        size_t operators        = 0;
        for (size_t index = 0; index < plan.topo.size(); ++index) {
            const gc_node_ref_t ref = plan.topo[index];
            const GCNode *node      = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body == nullptr) {
                continue;
            }
            // What reads the value: the reverse of the input table. A sync or an order edge names
            // statements that have to run after this node, not values read from it, so neither is
            // a use; the three port groups are.
            const size_t uses =
                users.norm(ref).size() + users.with(ref).size() + users.ctrl(ref).size();
            const uint32_t lastUse = plan.lastUseOf(ref);
            ++operators;
            os << "REUSE " << placeOf(*graph, ref, sources) << " "
               << operatorName(table, body->uri()) << " uses=" << uses << " last-use=";
            if (lastUse == ExecPlan::kNoLastUse) {
                os << "-";
            } else {
                os << lastUse;
            }
            // The third column is the run's answer and not the compiler's: whether another live
            // value shares the bytes behind this one is the number of holders of a block, which
            // only the array can be asked (Array::canWriteInPlace). A compile-time reading says
            // `unknown`; `REUSE observed` below is what the run recorded.
            os << " alias=unknown\n";
        }
        os << "REUSE " << graph->name() << " operators " << operators << "\n";
    }
    dumpObservedReuse(os);
}

void dumpObservedReuse(std::ostream &os) {
    const camel::profile::UpdateReuseCounters counters = camel::profile::updateReuseCounters();
    os << "REUSE observed plans " << counters.plans << " reuse-input " << counters.reuseInput
       << " by-array-count " << counters.byArrayCount << " copy " << counters.copy
       << " counts in-place " << counters.inPlace << " copied " << counters.copied << "\n";
}

} // namespace camel::runtime