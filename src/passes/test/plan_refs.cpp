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
 * The marked operators are asked one question of their own, because they are the one kind of node
 * whose elements are called by an operator rather than by the plan's own shape:
 *   PLAN_REF_MARKED <graph> at <position> node <ref> gives <n>
 *   PLAN_REF_MARKED <graph> marked <n> with-give <n>
 * `gives` is how many references the plan says come back at that position, `with-give` how many of
 * the graph's marked positions have at least one. A position with `gives 0` is an answer: an
 * operator over integers packs nothing to give back. What the line is for is a position that *does*
 * carry a row -- a `foreach` whose closure names a `state::Ref` -- because that is a counted
 * reference a back end can be caught being short by
 * (docs/handover/reports/2026-11-16-what-the-missing-count-was-and-was-not.cn.md).
 *
 * `produced` is the number of values the plan counts (ports, constants and nodes that only forward
 * a value are not among them), `copies` the references beyond each value's own, `give-backs` the
 * references that come back when a use ends, `packed` the ones that come back when the value that
 * packed them in dies (a closure tuple or an aggregate), `kept` the values whose producing
 * reference does not come back inside the graph -- the program keeps it (the graph's result), or a
 * loop reads it again after the round that read it, so the round's give-back cannot be placed at
 * the use (exec_plan.cpp, `repeatsWithoutMakingIt`). The columns are read in this order on purpose:
 * the first five balance as `produced + copies == give-backs + kept + packed`, and `balanced` is
 * that arithmetic recomputed here from the numbers the plan reports, not from the plan's own
 * answer.
 *
 * A graph whose plan is empty prints only its summary line: a pass with nothing to say says it.
 */

#include "camel/passes/test/plan_refs.h"

#include <string>
#include <unordered_map>

#include "camel/core/operator_table.h"
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
        // The copies of each value, read from the holds the plan takes for it: the plan has one
        // column of the holds (`refHoldsAt`, by the position that takes them) and the entries are
        // the same schedule as rows, so a value's copies are the `Retain` rows that name it. One
        // table of the holds, one answer (author review 2026-11-16, item 7).
        std::unordered_map<gc_node_ref_t, size_t> copiesOf;
        for (const ExecPlan::RefEntry &entry : plan.refEntries) {
            if (entry.kind == ExecPlan::RefEntryKind::Retain) {
                copiesOf[entry.value] += entry.count;
            }
        }
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            const gc_node_ref_t ref = plan.topo[i];
            if (i < plan.refProducedAt.size() &&
                (plan.refProducedAt[i] != 0 ||
                 (i < plan.refTakesAt.size() && plan.refTakesAt[i] != 0))) {
                const GCNode *node = graph->node(ref);
                os << "PLAN_REFS " << name << " at " << i << " value " << ref << " kind "
                   << (node == nullptr ? -1 : static_cast<int>(node->kind)) << " copies "
                   << copiesOf[ref] << " kept " << static_cast<int>(plan.refKeptAt[i]) << " uses "
                   << usesOf[ref] << "\n";
            }
            for (const ExecPlan::RefGiveBack &row : plan.refGiveBacksAt(i)) {
                os << "PLAN_REFS " << name << " at " << i << (row.packed ? " packed " : " give ")
                   << row.value << " x" << row.count << "\n";
            }
            // The same schedule keyed the way an instruction is: what a back end emitted at this
            // position would do, one count for the holds taken and one for the ones given back.
            // `refHoldsAt` is the plan's only column of the holds (author review 2026-11-16, item
            // 7), so what is printed here is what a back end reads.
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
        // Every marked operator of this graph, and how many references the plan says come back at
        // its position. A marked operator calls its function once per element, and a value an
        // element captures is held and given back at the operator's position like any other value's
        // (a `foreach` whose closure names a `state::Ref` is the shape the readings of 2026-11-16
        // were taken on). A position with `gives 0` is an answer and not a gap -- an operator over
        // integers holds nothing to give back -- so the counts are printed rather than argued
        // about, and `scripts/check-plan-refs.mjs` reads them over every case the tree has.
        size_t marked         = 0;
        size_t withGive       = 0;
        const auto &operators = camel::core::OperatorTable::instance();
        for (size_t i = 0; i < plan.topo.size(); ++i) {
            const gc_node_ref_t ref = plan.topo[i];
            const GCNode *node      = graph->node(ref);
            if (node == nullptr || node->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
            if (body == nullptr || !operators.isMarked(body->oper)) {
                continue;
            }
            const uint32_t given = i < plan.refGiveBackAt.size() ? plan.refGiveBackAt[i] : 0;
            os << "PLAN_REF_MARKED " << name << " at " << i << " node " << ref << " gives " << given
               << "\n";
            ++marked;
            if (given != 0) {
                ++withGive;
            }
        }
        os << "PLAN_REF_MARKED " << name << " marked " << marked << " with-give " << withGive
           << "\n";
    }
    return program;
}