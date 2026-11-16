// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The plan's answer to "which references come back, and where" (camel/runtime/exec_plan.h, "The
 * references that come back"), printed so that it is a reading and not an intention. No back end
 * reads the plan's table yet, so nothing else in the tree can show it is right; this pass is what
 * makes a change to it visible.
 *
 * One line per graph, which is the balance:
 *   PLAN_REFS <graph> produced <n> copies <n> give-backs <n> kept <n> packed <n> tail <ref> ...
 *     ... tail-kind <n> balanced yes|no
 * then one line per producing position:
 *   PLAN_REFS <graph> at <position> value <ref> kind <n> copies <n> kept <0|1> uses <n>
 * and one line per reference that comes back: `give` when a use of the value ended, `packed` when
 * the value that packed it in died (a closure tuple or an aggregate), which is later than the use.
 *   PLAN_REFS <graph> at <position> give <ref> x<n>
 *   PLAN_REFS <graph> at <position> packed <ref> x<n>
 *
 * Route 4's column and the entries the same walk lays out are printed beside them, under their own
 * prefixes so that a reader (and the entries below) can tell the two readings apart:
 *   PLAN_LAST_USE <graph> at <position> value <ref> last-use <position|none>
 *   PLAN_REF_ENTRIES <graph> at <position> retain <ref> x<n>
 *   PLAN_REF_ENTRIES <graph> at <position> give <ref> x<n> [packed]
 *   PLAN_REF_ENTRIES <graph> retains <n> give-backs <n> kept <n> balanced yes|no
 * `last-use none` is an answer and not a gap: nothing reads that value, so its producing reference
 * comes back at its own position. The entries are the schedule one row per thing to do -- the holds
 * a producing node's output takes (`retain`) and the references that come back (`give`) -- and
 * their balance is `produced + retains == give-backs + kept`, recomputed here from the rows
 * themselves.
 *
 * `produced` is the number of values the plan counts (ports, constants and nodes that only forward
 * a value are not among them), `copies` the references beyond each value's own, `give-backs` the
 * references that come back when a use ends, `packed` the ones that come back when the value that
 * packed them in dies (a closure tuple or an aggregate), `kept` the values whose producing reference
 * does not come back inside the graph -- the program keeps it (the graph's result), or a loop reads
 * it again after the round that read it, so the round's give-back cannot be placed at the use
 * (exec_plan.cpp, `repeatsWithoutMakingIt`). The columns are read in this order on purpose: the
 * first five balance as
 * `produced + copies == give-backs + kept + packed`, and `balanced` is that arithmetic recomputed
 * here from the numbers the plan reports, not from the plan's own answer.
 *
 * A graph whose plan is empty prints only its summary line: a pass with nothing to say says it.
 */

#include "camel/passes/test/plan_refs.h"

#include <string>
#include <unordered_map>

#include "camel/runtime/exec_plan.h"

const camel::runtime::Program *
PlanRefsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    for (GCGraph *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        const ExecPlan &plan   = graph->execPlan();
        const std::string name = graph->name();
        const size_t produced  = plan.refProducedTotal();
        const size_t copies    = plan.refCopiesTotal();
        const size_t giveBacks = plan.refGiveBacksTotal();
        const size_t kept      = plan.refKeptTotal();
        const size_t packed    = plan.refPackedTotal();
        bool balanced          = plan.refBalanceHolds();
        // The same arithmetic, from the numbers printed: a reader must be able to check the line.
        balanced = balanced && produced + copies == giveBacks + kept + packed;
        os << "PLAN_REFS " << name << " produced " << produced << " copies " << copies
           << " give-backs " << giveBacks << " kept " << kept << " packed " << packed;
        if (plan.tailValueRef == kInvalidNodeRef) {
            os << " tail none";
        } else {
            const GCNode *tail = graph->node(plan.tailValueRef);
            os << " tail " << plan.tailValueRef << " tail-kind "
               << (tail == nullptr ? -1 : static_cast<int>(tail->kind));
        }
        os << " balanced " << (balanced ? "yes" : "no") << "\n";
        // The uses of each value, counted from the rows themselves: a reference comes back once per
        // use, and a value that nothing reads comes back at its own position instead.
        std::unordered_map<gc_node_ref_t, size_t> usesOf;
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(i)) {
                if (row.at != row.value) {
                    usesOf[row.value] += row.count;
                }
            }
        }
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            const gc_node_ref_t ref = plan.topo[i];
            if (i < plan.refProducedAt.size() && plan.refProducedAt[i] != 0) {
                const GCNode *node = graph->node(ref);
                os << "PLAN_REFS " << name << " at " << i << " value " << ref << " kind "
                   << (node == nullptr ? -1 : static_cast<int>(node->kind)) << " copies "
                   << plan.refCopiesAt[i] << " kept " << static_cast<int>(plan.refKeptAt[i])
                   << " uses " << usesOf[ref] << "\n";
            }
            for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(i)) {
                os << "PLAN_REFS " << name << " at " << i << (row.packed ? " packed " : " give ")
                   << row.value << " x" << row.count << "\n";
            }
            // The same schedule keyed the way an instruction is: what a back end emitted at this
            // position would do, one count for the holds taken and one for the ones given back. The
            // holds are `refHoldsAt` and not `refCopiesAt` because the two differ in position for a
            // value a CALL or a FILL makes: the node that makes it takes no hold itself, the first
            // operator position that writes one of its rows does.
            const uint32_t taken = i < plan.refHoldsAt.size() ? plan.refHoldsAt[i] : 0;
            const uint32_t given = i < plan.refGiveBackAt.size() ? plan.refGiveBackAt[i] : 0;
            if (taken != 0 || given != 0) {
                os << "PLAN_REFS " << name << " at " << i << " count +" << taken << " -" << given
                   << "\n";
            }
        }
        // Route 4's column, printed as it is: for every value the table counts, the position after
        // which it is never read again. `none` is an answer (nothing reads the value) and not a
        // gap.
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            if (i >= plan.refProducedAt.size() || plan.refProducedAt[i] == 0) {
                continue;
            }
            os << "PLAN_LAST_USE " << name << " at " << i << " value " << plan.topo[i]
               << " last-use ";
            const uint32_t last =
                i < plan.lastUseAt.size() ? plan.lastUseAt[i] : ExecPlan::kNoLastUse;
            if (last == ExecPlan::kNoLastUse) {
                os << "none";
            } else {
                os << last;
            }
            os << "\n";
        }
        // The same schedule as the entries a back end reads, one row per thing to do, and the
        // balance recomputed from the rows: `produced + retains == give-backs + kept`.
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            for (const ExecPlan::RefEntry &entry : plan.refEntriesAt(i)) {
                os << "PLAN_REF_ENTRIES " << name << " at " << i << " "
                   << (entry.kind == ExecPlan::RefEntryKind::Retain ? "retain" : "give")
                   << " value " << entry.value << " x" << entry.count
                   << (entry.packed ? " packed" : "") << "\n";
            }
        }
        os << "PLAN_REF_ENTRIES " << name << " retains "
           << plan.refEntriesOfKind(ExecPlan::RefEntryKind::Retain) << " give-backs "
           << plan.refEntriesOfKind(ExecPlan::RefEntryKind::GiveBack) << " kept "
           << plan.refKeptTotal() << " balanced " << (plan.refEntriesBalance() ? "yes" : "no")
           << "\n";
    }
    return program;
}