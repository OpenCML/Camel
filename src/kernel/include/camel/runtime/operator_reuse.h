// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <ostream>

namespace camel::source {
class SourceContext;
}

namespace camel::runtime {

class Program;

/*
 * `camel inspect --reuse`: what each operator node's result was told about the storage it gets, as
 * a reading. `--copies` (camel/runtime/storage_reuse.h) answers for *update* nodes -- the ones that
 * could hand their input's storage to their result. This one answers for every operator node, which
 * is the question a plan moving storage reuse into an operator's own kernel has to be read with:
 * after which position is this value never read again, how many places read it at all, and does
 * anything else hold the bytes behind it.
 *
 * One line per operator node, in the plan's own order:
 *
 *   REUSE <file>:<line>:<column> <op> uses=<n> last-use=<position|-> alias=<yes|no|unknown>
 *
 * `<op>` is the name the program writes (OperatorTable::callFormOf), as in `--copies`. `uses`
 * counts the nodes that read the value -- its norm, with and control users, which is the reverse of
 * the input table (GCGraph::uses()); sync and order edges are not reads and are left out.
 * `last-use` is the topo position after which the planner says the value is never read again (`-`
 * when nothing reads it, ExecPlan::kNoLastUse). `alias` is the third column and it is `unknown`:
 * whether another live value shares the bytes behind this one is a run-time fact -- the number of
 * holders of a block, which `Array::canWriteInPlace` asks. A compile-time reading cannot answer it
 * and this one does not guess; `dumpObservedReuse` prints what the run itself recorded.
 *
 * Then one line per graph, so a graph with no operator node is a reading and not silence:
 *
 *   REUSE <graph> operators <n>
 *
 * and one line for the whole program, which is the run-time half:
 *
 *   REUSE observed plans <n> reuse-input <n> by-array-count <n> copy <n> counts in-place <n> copied
 * <n>
 *
 * The first four numbers are what the plans built so far decided (profile::planUpdateMarks: the
 * count of plans, then the nodes it marked StorageReuse::Input, ByArrayCount and Copy); the last
 * two are what the arrays themselves answered when an in-place update asked
 * (profile::arrayCountAnswer). Under `inspect` nothing runs, so the counts are 0 and the line says
 * the runs it counted: none. Under a run with `--reuse` the same line is the run's answer.
 *
 * The mark is `by-array-count` and not `by-count` on purpose. `--copies` prints `by-count` for
 * every update whose storage the plan left to the array -- which includes the in-place forms the
 * plan decided nothing about (StorageReuse::None), because that is what the back end runs. The
 * plan's mark is the narrower thing: a node it actually decided ByArrayCount. One word for both
 * would make the two readings look like they count the same set, and a program whose only updates
 * are the decided-by-nothing kind would read as a contradiction (rows in `--copies`, zeros here)
 * instead of what it is: no marks, no question asked.
 *
 * A program can print zeros here and still reuse storage: the counts are answers to a question, and
 * a program whose only update is `nn:sgd` (a `declared` row -- the operator says it reuses, nothing
 * asks the array) never asks it. That is a reading of this mechanism and not a measure of how much
 * reuse a run did; `--copies` says which of the two a row is.
 */
void dumpOperatorReuse(
    const Program &program, std::ostream &os,
    const camel::source::SourceContext *sources = nullptr);

/// Just the `REUSE observed ...` line: what the run's plans decided and what the arrays answered,
/// as recorded by profile::planUpdateMarks and profile::arrayCountAnswer. Printed at the end of a
/// run when `--reuse` was given, and by `dumpOperatorReuse` after its rows.
void dumpObservedReuse(std::ostream &os);

} // namespace camel::runtime