// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The rollback self-check's test (test::undo_selfcheck), see `GraphDraft::setUndoSelfCheck`.
 *
 * It walks the classes the journal records, one round per class: the class is written through the
 * setter that records it inside a segment, the segment is rolled back, and the draft has to be the
 * one the mark found. Then the same write is made again with that class's hook made a no-op --
 * which is what a hook nobody remembered to call amounts to -- and the check has to name *that*
 * class and no other. The second round is the one that can go red: a check that answers "fine"
 * whatever the journal did is a check that proves nothing.
 *
 * Every round uses a session of its own and never commits: the program the pass was given is
 * passed on unchanged, so a draft left half written by a deliberately broken round cannot reach
 * the graph the rest of the run reads.
 */

#include "camel/passes/test/undo_selfcheck.h"

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

using camel::runtime::GCGraph;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphDraft;
using camel::runtime::UndoClass;
using camel::runtime::undoClassName;

/// The classes in the order the check compares them, which is the order a report names them in.
/// The equivalence log is not one of them: it is the store's (see
/// `DraftWritings::ApplicationMark`), and a draft that has values with several writings refuses
/// every edit anyway (`requireOneWriting`), so there is no round to run for it.
constexpr UndoClass kClasses[] = {
    UndoClass::Nodes,
    UndoClass::Regions,
    UndoClass::Scalars,
    UndoClass::Ports,
    UndoClass::Types,
    UndoClass::LowLevel,
};

/// One way to write a class, and the write that puts it back **outside** a segment: the inverse is
/// not the rollback (that is what is being tested) but the same setter called again with the value
/// the probe found.
struct Probe {
    UndoClass cls;
    std::function<void(GraphDraft &)> write;
    std::function<void(GraphDraft &)> unwrite;
};

gc_node_ref_t firstLive(const GraphDraft &draft) {
    for (size_t at = 0; at < draft.nodeSlotCount(); ++at) {
        if (draft.header(static_cast<gc_node_ref_t>(at)) != nullptr) {
            return static_cast<gc_node_ref_t>(at);
        }
    }
    return camel::runtime::kInvalidNodeRef;
}

/// A node that is none of the three port tables, so that `appendNormPort` changes the table.
std::optional<gc_node_ref_t> freeNodeForPort(const GraphDraft &draft) {
    const auto listed = [&](gc_node_ref_t id) {
        const auto holds = [&](std::span<const gc_node_ref_t> table) {
            return std::find(table.begin(), table.end(), id) != table.end();
        };
        return holds(draft.normPorts()) || holds(draft.withPorts()) ||
               holds(draft.closureNodes());
    };
    for (size_t at = 0; at < draft.nodeSlotCount(); ++at) {
        const gc_node_ref_t id = static_cast<gc_node_ref_t>(at);
        if (draft.header(id) != nullptr && !listed(id)) {
            return id;
        }
    }
    return std::nullopt;
}

/// A node with a normal input, so that `setNormInputs` relinks something.
std::optional<gc_node_ref_t> nodeWithNormInput(const GraphDraft &draft) {
    for (size_t at = 0; at < draft.nodeSlotCount(); ++at) {
        const gc_node_ref_t id = static_cast<gc_node_ref_t>(at);
        if (draft.header(id) != nullptr && !draft.normInputsOf(id).empty()) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<Probe> makeProbe(UndoClass cls, GraphDraft &draft) {
    switch (cls) {
    case UndoClass::Nodes: {
        // The node's own content, changed and put back: a flag rather than a type, because any
        // node has one and no two values of it are equal.
        const gc_node_ref_t id = firstLive(draft);
        if (id == camel::runtime::kInvalidNodeRef) {
            return std::nullopt;
        }
        const uint8_t old = draft.header(id)->runtimeFlags;
        return Probe{
            cls,
            [id, old](GraphDraft &target) { target.setNodeRuntimeFlags(id, old ^ 1u); },
            [id, old](GraphDraft &target) { target.setNodeRuntimeFlags(id, old); },
        };
    }
    case UndoClass::Regions: {
        // The body's region exists in every draft (region 0), so the table always has an entry to
        // change.
        const uint32_t old = draft.region(0).attrs;
        return Probe{
            cls,
            [old](GraphDraft &target) { target.setRegionAttrs(0, old ^ 0x100u); },
            [old](GraphDraft &target) { target.setRegionAttrs(0, old); },
        };
    }
    case UndoClass::Scalars: {
        const uint32_t old = draft.insertRegion();
        return Probe{
            cls,
            [old](GraphDraft &target) { target.setInsertRegion(old + 1); },
            [old](GraphDraft &target) { target.setInsertRegion(old); },
        };
    }
    case UndoClass::Ports: {
        if (const auto free = freeNodeForPort(draft)) {
            // The table gains a node. Putting it back means taking it out again -- but only when it
            // is still there, because a round whose hook ran has already rolled the table back.
            const gc_node_ref_t id = *free;
            return Probe{
                cls,
                [id](GraphDraft &target) { target.appendNormPort(id); },
                [id](GraphDraft &target) {
                    const auto table = target.normPorts();
                    if (std::find(table.begin(), table.end(), id) != table.end()) {
                        target.removeNormPort(id);
                    }
                },
            };
        }
        // Every node is already a port: take one out instead, and put it back the same way.
        if (!draft.normPorts().empty()) {
            const gc_node_ref_t id = draft.normPorts().front();
            return Probe{
                cls,
                [id](GraphDraft &target) { target.removeNormPort(id); },
                [id](GraphDraft &target) {
                    const auto table = target.normPorts();
                    if (std::find(table.begin(), table.end(), id) == table.end()) {
                        target.appendNormPort(id);
                    }
                },
            };
        }
        return std::nullopt;
    }
    case UndoClass::Types: {
        // The graph's own signature: the empty one is a different interned signature from any a
        // function has, so this is a change whatever the draft holds.
        auto *old    = draft.funcType();
        auto *probed = camel::core::type::FunctionType::create();
        if (probed == old) {
            // A draft that already holds the empty signature: a signature with one parameter is a
            // signature no function of the case has, and `setFuncType` writes it the same way, so
            // the class is still written and still has to come back.
            probed = camel::core::type::FunctionType::create(
                {{camel::core::type::Type::Int(), false}}, {}, nullptr);
        }
        return Probe{
            cls,
            [probed](GraphDraft &target) { target.setFuncType(probed); },
            [old](GraphDraft &target) { target.setFuncType(old); },
        };
    }
    case UndoClass::LowLevel: {
        const auto id = nodeWithNormInput(draft);
        if (!id) {
            return std::nullopt;
        }
        const std::vector<gc_node_ref_t> old(
            draft.normInputsOf(*id).begin(), draft.normInputsOf(*id).end());
        return Probe{
            cls,
            [id = *id](GraphDraft &target) { target.setNormInputs(id, {}); },
            [id = *id, old](GraphDraft &target) { target.setNormInputs(id, old); },
        };
    }
    }
    return std::nullopt;
}

/// The clean round: every class written inside one segment at once, and rolled back.
bool checkClean(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    GCGraph *graph, std::ostream &os) {
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    GraphDraft &draft = session.edit(graph).edit();
    draft.setUndoSelfCheck(GraphDraft::SelfCheckMode::Report);
    std::vector<Probe> probes;
    for (const UndoClass cls : kClasses) {
        auto probe = makeProbe(cls, draft);
        if (!probe) {
            os << std::format("SELFCHECK clean unavailable={} FAIL\n", undoClassName(cls));
            return false;
        }
        probes.push_back(std::move(*probe));
    }
    const auto mark = draft.beginUndo();
    for (Probe &probe : probes) {
        probe.write(draft);
    }
    draft.rollbackTo(mark);
    const auto verdict = draft.undoMismatch();
    draft.endUndo();
    for (Probe &probe : probes) {
        probe.unwrite(draft);
    }
    const bool good = !verdict.has_value();
    os << std::format(
        "SELFCHECK clean verdict={} {}\n",
        verdict ? undoClassName(*verdict) : std::string_view("none"),
        good ? "ok" : "FAIL");
    return good;
}

/// One class, written and rolled back with its hook: the check has to name that class and nothing
/// else. `skip` makes the hook a no-op, which is the round that can go red.
bool checkClass(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    GCGraph *graph, UndoClass cls, bool skip, std::ostream &os) {
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    GraphDraft &draft = session.edit(graph).edit();
    draft.setUndoSelfCheck(GraphDraft::SelfCheckMode::Report);
    auto probe = makeProbe(cls, draft);
    if (!probe) {
        os << std::format("SELFCHECK class={} unavailable FAIL\n", undoClassName(cls));
        return false;
    }
    if (skip) {
        draft.skipUndoClassForTest(cls);
    }
    const auto mark = draft.beginUndo();
    probe->write(draft);
    draft.rollbackTo(mark);
    const auto verdict = draft.undoMismatch();
    draft.endUndo();
    draft.clearSkippedUndoClassForTest();
    probe->unwrite(draft);
    // A round whose hook is skipped must report the class; a round whose hook runs must report
    // nothing -- the class is back, so nothing is missing.
    const bool good = skip ? (verdict.has_value() && *verdict == cls) : !verdict.has_value();
    os << std::format(
        "SELFCHECK class={} hook={} verdict={} {}\n",
        undoClassName(cls),
        skip ? "skipped" : "run",
        verdict ? undoClassName(*verdict) : std::string_view("none"),
        good ? "ok" : "FAIL");
    return good;
}

} // namespace

const camel::runtime::Program *
UndoSelfCheckTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The target: the case's own function, whose body holds a port, an operator and a result, so
    // every class has something to write.
    GCGraph *target = nullptr;
    for (GCGraph *candidate : program->graphs()) {
        if (candidate != nullptr && candidate->name() == "integer_chain") {
            target = candidate;
            break;
        }
    }
    if (target == nullptr) {
        os << "SELFCHECK FAIL no integer_chain graph to walk\n";
        return program;
    }
    bool ok = checkClean(context_, program, target, os);
    for (const UndoClass cls : kClasses) {
        ok = checkClass(context_, program, target, cls, false, os) && ok;
        ok = checkClass(context_, program, target, cls, true, os) && ok;
    }
    os << std::format("SELFCHECK {}\n", ok ? "ok" : "FAIL");
    return program;
}