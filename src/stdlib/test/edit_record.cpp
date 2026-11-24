// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The witness of the edit record: one edit through each entry, and what the record says about it.
 *
 * A graph is built here and edited here, because no program in the corpus can see a region, a
 * notification or a tier: they are facts about the session editing the graph, not about what the
 * program computes (`std::gir` and `std::stats` print the same document either way). So this case
 * asks the interface directly, and its acceptance is the reverse verification -- with the
 * notification in `GraphDraft::recordEdit` commented out, `notified` falls to 0 against a record
 * that still says what it made, and the line stops matching.
 *
 * Two drafts are read in one run, one per entry, so that neither can pass on an interface that does
 * only one of them:
 *
 *   * the low-level entry (a node made, an operand added) followed by the high-level one (an
 *     `erase`): the record must name both kinds, the region they changed, and every notification;
 *   * a draft whose only record is one named `replace`: that kind owes the effect and order
 *     questions neither, and that is the tier -- the one part of this case the record changes
 *     without a graph changing.
 */

#include "camel/passes/test/edit_record.h"

#include "../../compile/src/gir/builder_support.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/execute/pass/registration.h"
#include "camel/runtime/graph_txn.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace camel::compile::gir;

namespace {

void say(std::ostream &os, std::string_view line) { os << "[EDIT_RECORD] " << line << "\n"; }

/// A listener that only counts: the interface's own promise is that it is called once per edit, so
/// counting is the reading.
class CountingListener final : public camel::runtime::EditListener {
  public:
    void onEditRecorded(camel::runtime::EditKind /*kind*/, std::span<const uint32_t> /*regions*/)
        override {
        ++calls_;
    }

    size_t calls() const { return calls_; }

  private:
    size_t calls_ = 0;
};

/// The kinds the record names, in the order they were first used, as one word a plan can read.
std::string kindsOf(const camel::runtime::GraphDraft &draft) {
    std::string names;
    for (const camel::runtime::EditNote &note : draft.editNotes()) {
        const std::string_view name = camel::runtime::editKindName(note.kind);
        if (names.find(name) != std::string::npos) {
            continue;
        }
        if (!names.empty()) {
            names += ",";
        }
        names += name;
    }
    return names;
}

/// The regions the record names, in the order the plan reads them: `0`, or `0|4`.
std::string regionsOf(const camel::runtime::GraphDraft &draft) {
    std::string text;
    for (uint32_t region : draft.editedRegions()) {
        if (!text.empty()) {
            text += "|";
        }
        text += std::to_string(region);
    }
    return text;
}

} // namespace

const camel::runtime::Program *
EditRecordTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    const auto make = []() {
        return std::make_shared<DraftGraphBuilder>(camel::runtime::GraphTransaction::open(
            static_cast<camel::runtime::GraphId>(1),
            camel::core::type::FunctionType::create()));
    };

    // ---- both entries on one draft -----------------------------------------------------------
    compile_graph_ptr_t builder = make();
    auto &draft                 = builder->edit();
    CountingListener listener;
    draft.addEditListener(&listener);

    // The low-level entry: a node made, then an operand added to an existing node.
    const node_handle_t erased  = builder->addOperNode(camel::core::type::Type::Float32(), ":op/copy");
    const node_handle_t operand = builder->addOperNode(camel::core::type::Type::Float32(), ":op/copy");
    const camel::runtime::gc_node_ref_t erasedId  = nodeIdOf(erased);
    const camel::runtime::gc_node_ref_t operandId = nodeIdOf(operand);
    const camel::runtime::gc_node_ref_t reads[1]  = {operandId};
    draft.setNormInputs(erasedId, reads);

    // The high-level entry: the same graph, one named `erase`. What it changes in place is that
    // edit's, and it records once, when it closes.
    const size_t beforeErase      = draft.editNotes().size();
    const size_t callsBeforeErase = listener.calls();
    {
        const camel::runtime::GraphDraft::NamedEdit named(draft, camel::runtime::EditKind::Erase);
        draft.eraseNode(erasedId);
    }
    const bool erasedAlive                 = draft.alive(erasedId);
    const size_t namedNotes                = draft.editNotes().size() - beforeErase;
    const size_t namedCalls                = listener.calls() - callsBeforeErase;
    const camel::runtime::EditTier lowTier = draft.editTier();
    draft.removeEditListener(&listener);

    const size_t notified = listener.calls();
    const size_t notes    = draft.editNotes().size();
    say(os,
        std::format(
            "kinds={} regions={} notified={} notes={} same={} named_calls={} named_notes={} "
            "tier_effects={} tier_order={} alive_after_erase={}",
            kindsOf(draft),
            regionsOf(draft),
            notified,
            notes,
            notified == notes ? 1 : 0,
            namedCalls,
            namedNotes,
            lowTier.effects ? 1 : 0,
            lowTier.order ? 1 : 0,
            erasedAlive ? 1 : 0));

    // ---- the tier of a record that names one named edit and nothing else -----------------------
    // No graph is built for this one: the question is what the record says about one edit, and an
    // edit that changed no region is still an edit of its kind -- the count the tier reads is the
    // kinds', not the notes'. That is also why this is a control and not a repeat: the draft above
    // recorded the low-level entry too, so its tier owes both questions.
    compile_graph_ptr_t namedOnly = make();
    auto &second                  = namedOnly->edit();
    CountingListener secondListener;
    second.addEditListener(&secondListener);
    {
        const camel::runtime::GraphDraft::NamedEdit named(
            second,
            camel::runtime::EditKind::Replace);
    }
    const bool namedKindRecorded             = second.editedWith(camel::runtime::EditKind::Replace);
    const camel::runtime::EditTier namedTier = second.editTier();
    const size_t namedOnlyCalls              = secondListener.calls();
    const size_t namedOnlyEdits              = second.editCount();
    const std::string namedRegions           = regionsOf(second);
    second.removeEditListener(&secondListener);
    say(os,
        std::format(
            "named_kind_recorded={} named_regions={} named_calls={} named_edits={} "
            "named_tier_effects={} named_tier_order={}",
            namedKindRecorded ? 1 : 0,
            namedRegions.empty() ? "none" : namedRegions,
            namedOnlyCalls,
            namedOnlyEdits,
            namedTier.effects ? 1 : 0,
            namedTier.order ? 1 : 0));

    // A witness that could not run its own case must say so instead of reporting nothing wrong.
    if (notified == 0 || notified != notes) {
        say(os, "MISSING the record and the listeners do not agree");
        return program;
    }
    if (namedCalls != 1 || namedNotes != 1) {
        say(os, "MISSING the named erase did not record exactly once");
        return program;
    }
    if (erasedAlive) {
        say(os, "MISSING the named erase left the node it erased");
        return program;
    }
    if (lowTier.effects != 1 || lowTier.order != 1) {
        say(os, "MISSING the low-level entry owes neither question");
        return program;
    }
    if (namedTier.effects != 0 || namedTier.order != 0) {
        say(os, "MISSING a named replace owes both questions");
        return program;
    }
    if (namedOnlyCalls != 1 || namedOnlyEdits != 1) {
        say(os, "MISSING the named edit was not one edit");
        return program;
    }
    say(os, "ok");
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header declares, so
/// the name a pipeline writes and the class that answers it are one fact.
const PassKernel editRecordKernel(kEditRecordTestPass, makePass<EditRecordTestPass>());

} // namespace