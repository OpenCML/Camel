// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What the execution plan decided about every update node, printed (`camel inspect --copies`).
 * See camel/runtime/storage_reuse.h for the shape of the lines and why they exist.
 */

#include "camel/runtime/storage_reuse.h"

#include <cstddef>
#include <cstdint>
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

/// The four words the reading prints, and the one place they are written. Three of them are the
/// plan's own answers (`StorageReuse`, camel/runtime/exec_plan.h), and the fourth -- `Declared` --
/// is not: the plan never writes it into `storageReuseAt`, because a node whose only claim is its
/// operator's declaration has no decision to record. So it is the tool's word and lives here, beside
/// the two words that are not the plan's either (an in-place form the plan decided nothing about,
/// and the copying form a plan decision cannot describe); keeping it in the core enum made a
/// member nobody could write look like a fourth decision (author review 2026-11-17, item 11).
enum class CopyVerdict : uint8_t {
    InPlace,  // the plan proved it and hands the storage over (StorageReuse::Input)
    ByCount,  // asks the array's own count at run time (StorageReuse::ByArrayCount, or no decision)
    Declared, // the operator claims it reuses an operand; no count was taken and none is claimed
    Copied,   // proven to copy, or the copying form of an operator that has an in-place one
};

/// The word the reading prints for a verdict.
const char *verdictOf(CopyVerdict verdict) {
    switch (verdict) {
    case CopyVerdict::InPlace:
        return "in-place";
    case CopyVerdict::ByCount:
        return "by-count";
    case CopyVerdict::Declared:
        return "declared";
    case CopyVerdict::Copied:
        return "copied";
    }
    return "copied";
}

/// The plan's decision as the reading's word, for the decisions a plan can actually write.
CopyVerdict verdictOfDecision(StorageReuse decision) {
    switch (decision) {
    case StorageReuse::Input:
        return CopyVerdict::InPlace;
    case StorageReuse::ByArrayCount:
        return CopyVerdict::ByCount;
    case StorageReuse::Copy:
    case StorageReuse::None:
        break;
    }
    return CopyVerdict::Copied;
}

/// The name the program writes for an update, read off the operator table and never derived from the
/// URI's spelling (author review 2026-11-17, item 11: the reading used to strip the suffixes
/// `_in_place`, `_owned` and `_arr`, which is a second spelling of the connection the table already
/// declares). Two declarations answer it, both written where the operator is registered:
///
///   * the call shape declared for the operator (`OperatorTable::callFormOf`):
///     `stdlib/prelude.cml` declares `export func append::<T>(xs: T[])` with `#op(':struct/append_arr')`,
///     so `append` is a fact of that declaration and not of the URI's tail; `array:sort_i` answers
///     `sort_i` the same way; and the loop-state append states its own shape
///     (`declareCallForm(":struct/append_arr_owned", ...)`) because a program writes `xs->append<v>`
///     for it -- which is why this reading no longer carries a URI constant for that form;
///   * for a form a program cannot write, the operator the form stands for (`plainForm`), whose own
///     call shape is then the name: the program wrote the plain call, and the verdict is the form's.
std::string updateName(const camel::core::OperatorTable &table, std::string_view uri) {
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
    if (const std::optional<std::string_view> plain = table.plainForm(uri); plain.has_value()) {
        if (std::optional<std::string> name = writtenName(*plain); name.has_value()) {
            return *name;
        }
    }
    // Nothing a program can write: the operator's own tail is all there is to name it by. That is a
    // fallback for an operator no reading should reach, and not a rule about how names are made.
    const size_t separator = uri.find_last_of("/:");
    return std::string(uri.substr(separator == std::string_view::npos ? 0 : separator + 1));
}

/// Whether the node is the loop-state append: `xs->append<v>` on a loop state the compiler proved
/// no one else can see (Builder::markLoopStateInPlace). Read by the id the operator table fixes for
/// it (`camel::core::oper::AppendOwned`, operator_table.h), not by its URI's spelling -- the same
/// test the builder makes when it resolves the operator (builder_control.cpp).
///
/// It is not an in-place form and the plan decided nothing about it, because there is no cell to take
/// the array from: the operator asks the array itself whether its storage can be written
/// (Array::writableInPlace) and copies when it cannot, which is the `by-count` reading.
bool isLoopStateAppend(camel::core::OperId oper) noexcept {
    return oper == camel::core::oper::AppendOwned;
}

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
        size_t declared      = 0;
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
            // no cell and no decision, recognized by the id the table fixes for it and not by its
            // URI's spelling. And the *copying* form of an operator that has an in-place form --
            // `v = v->append(x)` on an array the compiler did not hand over, or one it could not
            // prove unshared -- which allocates the new storage and hands it back. All three are the
            // update the program wrote; leaving the third one out would be the silence this command
            // exists to remove, and `copied` is the answer it must be able to give. An operator that
            // has neither form and declares nothing returns a new value and never had the option of
            // reusing its input's storage; that is not an update.
            const bool loopStateAppend = isLoopStateAppend(body->oper);
            const bool copyingForm =
                !plain.has_value() && !loopStateAppend && table.inPlaceForm(uri).has_value();
            // The fourth shape: an operator that declares it reuses an operand
            // (`OperatorTable::declareReusesOperand`). `nn:sgd` is the one the direct-SSA plan's
            // S3b names -- `sgd(params, g, lr)` answers a parameter tree whose storage is the one
            // it was handed, and `nn:adam` answers the tree and the state it was handed the same
            // way -- and no in-place form can say it: the value is a *tree* the operator walks
            // leaf by leaf, not the one array `setInPlaceForm` names. The declaration is the
            // operator's own promise that the write asks the array's count at the leaf (design
            // principles 10.5), so the node is an update; without it the node was left out and a
            // training step read `updates 0` -- silence about the one update such a program makes.
            const bool declaredReuse = table.reusesOperand(uri);
            // Only a node whose only claim is the declaration reads `declared`. An in-place form
            // that also declares reuse (the sorts, `:op/set_index_in_place`) keeps the answer its
            // form and the plan give it: that answer was measured, and a measured answer outranks
            // a claimed one.
            const bool declaredOnly =
                declaredReuse && !plain.has_value() && !loopStateAppend && !copyingForm;
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
            // `declared` is the fourth answer and it is not the third: `by-count` is measured (the
            // leaf's count answered), `declared` is only claimed (the operator says it reuses, and
            // whether this run allocated cannot be told from here). Printing one word for both
            // would put a promise and a measurement in the same column, and the column is the one
            // the S3b criterion is read with.
            CopyVerdict read = verdictOfDecision(decided);
            if (!plain.has_value() || decided == StorageReuse::None) {
                // An in-place form the plan decided nothing about, the loop-state append, and an
                // operator that only declared reuse: the form asks the array at run time.
                read = CopyVerdict::ByCount;
            }
            if (declaredOnly) {
                read = CopyVerdict::Declared;
            }
            if (copyingForm) {
                read = CopyVerdict::Copied;
            }
            switch (read) {
            case CopyVerdict::InPlace:
                ++inPlace;
                break;
            case CopyVerdict::ByCount:
                ++byCount;
                break;
            case CopyVerdict::Declared:
                ++declared;
                break;
            case CopyVerdict::Copied:
                ++copied;
                break;
            }
            // Every shape answers its own name from the table: the loop-state form has a call shape
            // of its own now (`declareCallForm(":struct/append_arr_owned", ...)` beside the two
            // forms in src/builtin/executors/builtin.cpp), so there is no shape left whose name has
            // to be spelled out, and no URI constant in this reading.
            const std::string name = updateName(table, uri);
            os << "COPIES " << placeOf(*graph, ref, sources) << " " << name << " "
               << verdictOf(read) << "\n";
        }
        os << "COPIES " << graph->name() << " updates " << (inPlace + byCount + declared + copied)
           << " in-place " << inPlace << " by-count " << byCount << " declared " << declared
           << " copied " << copied << "\n";
    }
}

} // namespace camel::runtime