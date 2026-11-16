// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What the execution plan decided about every update node, printed (`camel inspect --copies`).
 * See camel/runtime/storage_reuse.h for the shape of the lines and why they exist.
 */

#include "camel/runtime/storage_reuse.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "camel/core/error/diagnostics/range.h"
#include "camel/core/operator_table.h"
#include "camel/core/source/ids.h"
#include "camel/core/source/manager.h"
#include "camel/runtime/exec_plan.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

namespace camel::runtime {

namespace {

/// The name the program writes for an update, read off the operator's URI: the part after the
/// module separator (`:struct/append_arr` -> `append_arr`, `array:sort_i` -> `sort_i`), with the
/// spelling the built-in adds for a form dropped (`_arr` for an array operation, `_owned` for the
/// loop-state form, `_in_place` for the in-place form): `append_arr_owned` -> `append`. The name is
/// derived and never stored, so adding a form needs no entry here.
std::string updateName(std::string_view uri) {
    const size_t separator = uri.find_last_of("/:");
    std::string name(uri.substr(separator == std::string_view::npos ? 0 : separator + 1));
    for (const std::string_view suffix : {"_in_place", "_owned", "_arr"}) {
        if (name.size() > suffix.size() && name.ends_with(suffix)) {
            name.resize(name.size() - suffix.size());
        }
    }
    return name;
}

/// The loop-state append: `xs->append<v>` on a loop state the compiler proved no one else can see
/// (Builder::markLoopStateInPlace). It is not an in-place form and the plan decided nothing about
/// it, because there is no cell to take the array from: the operator asks the array itself whether
/// its storage can be written (Array::writableInPlace) and copies when it cannot, which is the
/// `by-count` reading.
constexpr std::string_view kAppendOwned = ":struct/append_arr_owned";

/// `file:line:column` of the node (1-based, the way a diagnostic writes a place), or
/// `<graph>@<node>` when the node has no source position. The file is the last component of the
/// path, so a reading does not depend on where the tree is checked out.
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

/// The plan's decision as the one word the reading uses. `None` is not asked about here.
const char *verdictOf(StorageReuse decision) {
    switch (decision) {
    case StorageReuse::Input:
        return "in-place";
    case StorageReuse::ByArrayCount:
        return "by-count";
    case StorageReuse::Copy:
        return "copied";
    case StorageReuse::None:
        break;
    }
    return "copied";
}

} // namespace

void dumpStorageReuse(
    const Program &program, std::ostream &os, const camel::source::SourceContext *sources) {
    const auto &table = camel::core::OperatorTable::instance();
    for (GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        // A graph with several writings per value has no plan until they are extracted
        // (GCGraph::execPlan throws), and a graph with no body has nothing to plan. Neither is
        // silence: the graph is named and says it has nothing to read.
        if (!graph->hasNodePayload() || !graph->executable()) {
            os << "COPIES " << graph->name() << " no-plan\n";
            continue;
        }
        const ExecPlan &plan = graph->execPlan();
        size_t inPlace       = 0;
        size_t byCount       = 0;
        size_t copied        = 0;
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
            const std::string_view uri                  = body->uri();
            const std::optional<std::string_view> plain = table.plainForm(uri);
            // The shapes an update node has. The in-place form of an operator: the plan
            // decided it, because its array comes from a cell's `:var/take`. The loop-state append:
            // no cell and no decision. And the *copying* form of an operator that has an in-place
            // form -- `v = v->append(x)` on an array the compiler did not hand over, or one it
            // could not prove unshared -- which allocates the new storage and hands it back. All
            // three are the update the program wrote; leaving the third one out would be the
            // silence this command exists to remove, and `copied` is the answer it must be able to
            // give. An operator that has neither form and declares nothing returns a new value and
            // never had the option of reusing its input's storage; that is not an update.
            const bool loopStateAppend = uri == kAppendOwned;
            const bool copyingForm =
                !plain.has_value() && !loopStateAppend && table.inPlaceForm(uri).has_value();
            // The fourth shape: an operator that declares it reuses an operand
            // (`OperatorTable::declareReusesOperand`). `nn:sgd` is the one the direct-SSA plan's
            // S3b names -- `sgd(params, g, lr)` answers a parameter tree whose storage is the one
            // it was handed, and `nn:adam` answers the tree and the state it was handed the same
            // way -- and no in-place form can say it: the value is a *tree* the operator walks
            // leaf by leaf, not the one array `setInPlaceForm` names. The declaration is the
            // operator's own promise that the write asks the array's count at the leaf (design
            // principles 10.5), so the node is an update and its answer is `by-count`; without it
            // the node was left out and a training step read `updates 0` -- silence about the one
            // update such a program makes.
            const bool declaredReuse = table.reusesOperand(uri);
            if (!plain.has_value() && !loopStateAppend && !copyingForm && !declaredReuse) {
                continue;
            }
            const StorageReuse decided = index < plan.storageReuseAt.size()
                                             ? plan.storageReuseAt[index]
                                             : StorageReuse::None;
            // An in-place form the plan decided nothing about is one whose array did not come from
            // a cell's own `:var/take`. The back end still runs the in-place form the node names,
            // and that form asks the array at run time (Array::canWriteInPlace), which is the
            // `by-count` reading. The loop-state append asks the array by construction, and so does
            // an operator that declared it reuses an operand: the plan has no cell to read a
            // decision for a tree it never sees as one value, so `decided` is `None` for both. The
            // copying form allocates by construction, which is `copied`.
            const StorageReuse read = copyingForm
                                          ? StorageReuse::Copy
                                          : (!plain.has_value() || decided == StorageReuse::None
                                                 ? StorageReuse::ByArrayCount
                                                 : decided);
            switch (read) {
            case StorageReuse::Input:
                ++inPlace;
                break;
            case StorageReuse::ByArrayCount:
                ++byCount;
                break;
            default:
                ++copied;
                break;
            }
            os << "COPIES " << placeOf(*graph, ref, sources) << " "
               << updateName(plain.has_value() ? *plain : uri) << " " << verdictOf(read) << "\n";
        }
        os << "COPIES " << graph->name() << " updates " << (inPlace + byCount + copied)
           << " in-place " << inPlace << " by-count " << byCount << " copied " << copied << "\n";
    }
}

} // namespace camel::runtime