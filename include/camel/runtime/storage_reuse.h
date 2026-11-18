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
 * `camel inspect --copies`: what the execution plan decided about every update node of the program,
 * printed as a reading. An update node is an operator that "returns a new value" and could reuse
 * the storage of the value it is given -- `v = v->append(x)`, a path assignment (`xs[i] = x`), a
 * rebinding such as `xs = xs->sort` (camel/runtime/exec_plan.h, StorageReuse). The decision is made
 * once, in the plan, and every back end follows it; this function prints it, and decides nothing.
 *
 * It exists to be the ruler the direct-SSA plan's S3b is read with (docs/handover/plans/
 * 2026-11-16-direct-ssa-construction.cn.md): "the SSA value, at its last use, hands its storage to
 * an update; the criterion is `camel inspect --copies` reporting in-place for the `v =
 * v->append(x)` loop and for the case study's training step". The tool reports what the tree does
 * today; it does not make an update in-place, and both answers are readings.
 *
 * "In place" is two answers, not one: `in-place` (the plan decided at compile time that the storage
 * is reused) and `by-count` (the update asks the array's own count at run time, and reuses the
 * storage when it is the only holder -- design principles 10.5; it is in place, only decided
 * later). The criterion is "not `copied`": `in-place` and `by-count` both pass it. Read as "only
 * `in-place`", the criterion would fail every site in today's tree, including the ones that already
 * work in place, and it would fail them over a wording -- see
 * docs/handover/questions/2026-11-16-what-counts-as-an-update-for-a-training-step.cn.md.
 *
 * A fourth word, `declared`, is neither of the two: an operator that declares it reuses an operand
 * (see below) has been given the criterion's answer by its own claim, before anything asked a
 * count. It is printed so the claim is visible and is not read as the measurement: a row that says
 * `declared` does not satisfy a criterion written as "`in-place` or `by-count`", and the tool has
 * no way to make it -- what a run allocates is decided in the kernels and in the memory plan, not
 * here.
 *
 * One line per update node, in the plan's own order:
 *
 *   COPIES <file>:<line>:<column> <update> <verdict>
 *
 * `<update>` is the name the program writes, read off the operator table and not derived from the
 * URI's spelling (author review 2026-11-17, item 11): the call shape the operator's declaration
 * states (`OperatorTable::callFormOf` -- `append` for `:struct/append_arr` from
 * `stdlib/prelude.cml`'s `#op`, `sort_i` for `array:sort_i`), so the copying form of an operator
 * and the plain operator print the same name, and a form a program cannot write prints the name of
 * the operator it stands for (`OperatorTable::plainForm`: the in-place form `:op/set_index_in_place`
 * prints `set_index`). The loop-state append prints the plain append's name for the same reason: a
 * program wrote `xs->append<v>`, and the form is what the compiler made of it. The three forms are
 * the three ways `v = v->append(x)` reaches the graph, and all three are reported: a call that
 * copies is an update too, and the reading that says it allocates is the one it exists for. A
 * fourth shape is an operator that declares it reuses an operand
 * (`OperatorTable::declareReusesOperand`): `nn:sgd`'s `sgd(params, g, lr)` answers a parameter
 * tree whose storage is the one it was handed, and no in-place form can say that, because the value
 * is a tree the operator walks leaf by leaf rather than one array. It is reported the same way, by
 * the name the program writes (`sgd`), and its answer is `declared`, a fourth word: the plan
 * decided nothing (there is no cell holding one value it could prove something about), so what the
 * row states is the operator's claim that it reuses, and not a count anyone took. Which is why a
 * program whose only update is a training step's `sgd` reads one row and not `updates 0`.
 * `<verdict>` is the answer:
 *
 *   in-place  the plan proved the old value has no other holder and hands its storage to the
 *             update (StorageReuse::Input);
 *   by-count  the plan could not prove it -- the cell escapes, or the graph's own edges do not
 *             order the other users of the cell after this update -- so the update asks the array
 *             itself at run time: written in place when it is the only holder of its block,
 *             copied otherwise (StorageReuse::ByArrayCount). An in-place form the plan decided
 *             nothing about is read the same way, because that is what the back end runs
 *             (ExecPlan::operatorUriFor names the in-place form, which asks the array); so is the
 *             loop-state append (`:struct/append_arr_owned`), which has no cell to read a decision
 *             from and asks Array::writableInPlace by construction;
 *   declared  the operator declares it reuses an operand and has no in-place form
 *             (`OperatorTable::declareReusesOperand`: `nn:sgd`, `nn:adam`, whose value is a tree
 *             walked leaf by leaf). Nothing here proves the write is in place: the row records the
 *             claim, and whether the run really allocated is not answered by this tool. Kept apart
 *             from `by-count` on purpose -- one word for both would make a claim and a measurement
 *             indistinguishable in the column the S3b criterion is read with. This is the one word
 *             of the four that no decision of the plan can be: `storageReuseAt` never holds it, so
 *             it is this tool's word and is written here (src/runtime/storage_reuse.cpp,
 *             `CopyVerdict`), not a member of `StorageReuse` (camel/runtime/exec_plan.h; author
 *             review 2026-11-17, item 11);
 *   copied    the plan proved the old value has a holder the update must leave alone
 *             (StorageReuse::Copy), or the node is the copying form of an operator that has an
 *             in-place form -- `v = v->append(x)` on an array the compiler did not hand over, where
 *             the operator allocates the result by construction. This is the performance cliff the
 *             tool is named for.
 *
 * A source position the node does not have ({file, line, column} absent, which a synthetic node
 * has) falls back to `<graph>@<node>`, so a row is never nameless: an update is either `copied` or
 * in-place, and never silent.
 *
 * Then one line per graph, which is the count, and what makes a zero visible instead of silence:
 *
 *   COPIES <graph> updates <n> in-place <n> by-count <n> declared <n> copied <n>
 *
 * A graph with no update node prints that line with `updates 0`: "this program has no update to
 * reuse storage for" is a reading, and a missing line would not be. A graph there is nothing to
 * read at all for -- no body, or several writings per value, so no execution plan -- prints
 * `COPIES <graph> no-plan` rather than being passed over.
 */
void dumpStorageReuse(
    const Program &program, std::ostream &os,
    const camel::source::SourceContext *sources = nullptr);

} // namespace camel::runtime