// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Extract eligible regions and replace them with typed calls, independent of device backend.

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include <algorithm>
#include <format>
#include <mutex>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::tiles {
namespace {
/// `file:line:column` for an origin, with the file name and not its directory: an error value
/// must not differ with where the program is stored (the same reading `Builder::sourceLocationOf`
/// gives a literal; the report that prints it, `reportUnhandledError`, reads it back).
std::string sourceLocationOf(core::context::Context &context, camel::source::origin_id_t origin) {
    const auto *sourceContext = context.sourceContext().get();
    const auto *record        = sourceContext ? sourceContext->origin(origin) : nullptr;
    const auto *span          = record ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file = span && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->startOffset > file->content.size())
        return {};
    size_t line = 1, lineStart = 0;
    for (size_t i = 0; i < span->startOffset; ++i) {
        if (file->content[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    const size_t slash = file->path.find_last_of("/\\");
    return std::format(
        "{}:{}:{}",
        slash == std::string::npos ? file->path : file->path.substr(slash + 1),
        line,
        span->startOffset - lineStart + 1);
}

// Transfers are leaf operations. Preserve the source aggregate's structure in
// graph values instead of inventing a Device<tuple> or flattening field order.
runtime::gc_node_ref_t transferAggregate(
    runtime::GraphDraft &draft, runtime::gc_node_ref_t input, core::type::Type *host, bool upload,
    operator_t operation, const std::string &uri) {
    namespace rt   = runtime;
    namespace ty   = core::type;
    auto *resident = residentType(host);
    if (!resident)
        throw std::runtime_error("Tiles region transfer has an unsupported aggregate leaf");
    const bool tuple = host->code() == ty::TypeCode::Tuple;
    if (tuple || host->code() == ty::TypeCode::Struct) {
        auto *source       = upload ? host : resident;
        const size_t count = tuple ? static_cast<ty::TupleType *>(host)->size()
                                   : static_cast<ty::StructType *>(host)->size();
        std::vector<rt::gc_node_ref_t> fields;
        for (size_t i = 0; i < count; ++i) {
            auto *fieldType  = tuple ? static_cast<ty::TupleType *>(source)->typeAt(i)
                                     : static_cast<ty::StructType *>(source)->typeAt(i);
            auto *hostField  = tuple ? static_cast<ty::TupleType *>(host)->typeAt(i)
                                     : static_cast<ty::StructType *>(host)->typeAt(i);
            const auto field = tuple ? draft.addAccsNode(
                                           fieldType,
                                           static_cast<uint32_t>(i),
                                           draft.allocateRuntimeSlot(fieldType))
                                     : draft.addAccsNode(
                                           fieldType,
                                           static_cast<ty::StructType *>(host)->fieldName(i),
                                           draft.allocateRuntimeSlot(fieldType));
            draft.setNormInputs(field, std::span(&input, 1));
            fields.push_back(transferAggregate(draft, field, hostField, upload, operation, uri));
        }
        return packGraphAggregate(draft, upload ? resident : host, fields);
    }
    auto *type        = upload ? resident : host;
    const auto result = draft.addOperNode(type, operation, uri, draft.allocateRuntimeSlot(type));
    draft.setNormInputs(result, std::span(&input, 1));
    return result;
}
} // namespace

const runtime::Program *outlineRegions(
    core::context::Context &context, const runtime::Program *program, runtime::GCGraph *root,
    operator_t run, const std::string &uri, size_t &count, const SegmentDeviceOps *device,
    bool refuseGpuScopes, slot_t *error) {
    namespace rt = runtime;
    if (device &&
        (!device->upload || !device->execute || !device->download || run != device->execute))
        throw std::invalid_argument(
            "resident region rewriting requires matching transfer and execution operators");
    // Decorators can run on different Taskflow workers. A rewrite publishes detached
    // graphs into Context's graph registry, whose vectors are not concurrently mutable.
    // Serialize Tiles rewrites (not kernel execution) until that registry supports
    // concurrent publication. Acquire before suppressing collection for extraction.
    static std::mutex rewriteMutex;
    std::lock_guard rewriteLock(rewriteMutex);
    core::mm::NoCollectionScope noCollection;
    rt::RuntimeGraphDraftSession session(context.shared_from_this(), program, root);
    count             = 0;
    const auto graphs = session.collectReachableRuntimeGraphs();
    // A `#gpu` scope promises that the device runs everything written in it, and (GPU design 2.3,
    // 2.4) the functions it calls are part of that scope. Code written in an arm or a loop of a
    // covered graph is that region's segment, not a reason to fall back to the host (2.5), so the
    // scope map is built before the analysis and answers it.
    const GpuScopes scopes(*program);
    std::unordered_map<rt::GCGraph *, SegmentAnalysis> analyses;
    for (auto *graph : graphs) {
        analyses.emplace(graph, analyzeRegions(*program, *graph, context.facts(), &scopes));
    }
    // Whether `id` is inside a `#gpu` scope: a device answer places it there directly, and a graph
    // the scope reaches places every node it did not explicitly tag for the host. A `#cpu` fact
    // is the host escape the design keeps, so it is never part of a device promise.
    const auto nodeInScope = [&](rt::GCGraph &graph, rt::gc_node_ref_t id) {
        return scopes.covers(graph, id);
    };
    const auto scopeNameOf = [&](rt::GCGraph &graph) {
        const auto *scope = scopes.scopeOf(graph);
        return std::string((scope == nullptr ? &graph : scope)->name());
    };
    const auto regionInScope = [&](rt::GCGraph &graph, const Segment &region) {
        if (!refuseGpuScopes)
            return false;
        for (const auto member : region.members) {
            if (nodeInScope(graph, static_cast<rt::gc_node_ref_t>(member)))
                return true;
        }
        return false;
    };
    if (refuseGpuScopes) {
        // One answer per scope, naming every operator it cannot run and why each one cannot (GPU
        // design 2.4). Reporting per node would stop at the first: a scope is one promise, and the
        // compiler answers for it once. The answer is the pass's error value
        // (`GpuScopeUnsupported`), because a pass is `(f: F) => F!` (design principles 9.1).
        //
        // Two reasons survive from the two diagnostics this replaced, and both are the same
        // refusal of the same promise: an operator no module lowers for the device at all, and an
        // operator that has a device implementation the device compiler still refuses at this use.
        // Each one's own wording is kept (see `list`), so an operator that has no device program
        // of its own still reads as it read before.
        struct Refusal {
            std::string name;   // The operator's URI, as the message writes it.
            std::string reason; // Why this use cannot run on the device.
            camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
        };
        struct ScopeRefusals {
            std::vector<Refusal> unsupported; // No device implementation for it at all.
            std::vector<Refusal> noProgram;   // A device program exists, but not for this use.
            // A macro operator (`grad`, `with_grad`) no pass expanded: the defect is the pipeline,
            // and its repair is a pass, so it is its own group and its own diagnostic
            // (design principles 9.2).
            std::vector<Refusal> notExpanded;
        };
        std::vector<std::pair<rt::GCGraph *, ScopeRefusals>> refusals;
        const auto groupOf = [&](rt::GCGraph &graph) -> ScopeRefusals & {
            for (auto &entry : refusals)
                if (entry.first == &graph)
                    return entry.second;
            refusals.emplace_back(&graph, ScopeRefusals{});
            return refusals.back().second;
        };
        // Report every node the promise cannot keep and leave the graphs alone; the pass fails on
        // the diagnostics rather than running part of the program somewhere else.
        for (auto *graph : graphs) {
            const auto &analysis = analyses.at(graph);
            for (rt::gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
                if (graph->node(id)->kind != rt::GCNodeKind::Oper)
                    continue;
                if (!nodeInScope(*graph, id))
                    continue;
                const auto oper = graph->nodeBodyAs<rt::GCOperBody>(id)->oper;
                const std::string name(
                    core::OperatorTable::instance().uri(oper));
                const auto origin = graph->nodeOrigin(id);
                // A macro operator (`grad`, `with_grad`) is not an operator the device cannot
                // run: it should not be in the graph at all. The derived graph has to exist while
                // the passes run, and the pass that builds it is `std::macro` (design principles
                // 8), so a scope that still holds one is refused before anything is asked of the
                // device (docs/design/gpu/plan.cn.md 2.6).
                if (graph->node(id)->isMacro()) {
                    auto &group = groupOf(*graph);
                    // The function is the operator's ordinary operand; a call reads its callee as
                    // a `with` argument, so both are looked at. A function only the run produces
                    // names no graph, and no pass can expand the call that reads it: that is the
                    // scope's own defect, not a missing pass.
                    const auto operandIsKnown = [&](const auto &operands) {
                        return std::any_of(
                            operands.begin(), operands.end(), [&](rt::gc_node_ref_t operand) {
                                return holdsStaticFunction(*graph, operand);
                            });
                    };
                    const auto normOperands = graph->normInputsOf(id);
                    bool expandable         = operandIsKnown(normOperands);
                    if (!expandable)
                        expandable = operandIsKnown(graph->withInputsOf(id));
                    (expandable ? group.notExpanded : group.unsupported)
                        .push_back({name,
                                    expandable
                                        ? "a prerequisite pass is missing"
                                        : "needs a function known at compile time",
                                    origin});
                    continue;
                }
                // A host effect (`print`, `save_npy`, a file write) is the boundary of a device
                // scope, not an operator the device must run: the pass leaves it on the host and
                // the value it observes is downloaded there (docs/design/gpu/plan.cn.md 2.4).
                const auto &table = core::OperatorTable::instance();
                if (!table.isPure(oper) || table.hasExternalEffect(oper))
                    continue;
                if (!deviceCapable(oper)) {
                    // A device answer on the node itself is the operator having no device
                    // implementation; an untagged graph the scope reaches adds this operator to a
                    // device unit, and the module that provides it has no device program.
                    const bool own = placementOf(*graph, id).onDevice();
                    auto &group    = groupOf(*graph);
                    (own ? group.unsupported : group.noProgram)
                        .push_back({name,
                                    own ? "no device implementation" : "the module provides none",
                                    origin});
                    continue;
                }
                // A lowering exists; the use may still have no program (a shape or element type
                // the back end refuses). An unresolved type or index instance is not a refusal:
                // the run specializes it from the actual arguments before it decides.
                const char *reason = gpuRefusal(*graph, analysis, id);
                if (reason && *reason)
                    groupOf(*graph).noProgram.push_back({name, reason, origin});
            }
        }
        const auto list = [](const std::vector<Refusal> &entries, bool predicateInName) {
            // The first group is written "operator 'x' cannot run on the device ({reason})", as the
            // refusal has always read, and takes the operator as its subject; the second carries the
            // whole predicate in its name ("operator 'x' has no device program") because that is what
            // the reason says, so the one-operator wording is the one it has always had too.
            std::string names;
            if (entries.size() == 1) {
                names = "operator '" + entries[0].name + "'";
            } else {
                names = "operators ";
                for (size_t i = 0; i < entries.size(); ++i) {
                    if (i != 0)
                        names += ", ";
                    names += "'" + entries[i].name + "'";
                }
            }
            if (predicateInName)
                names +=
                    entries.size() == 1 ? " has no device program" : " have no device program";
            std::set<std::string> distinct;
            for (const auto &entry : entries)
                distinct.insert(entry.reason);
            if (distinct.size() == 1)
                return std::pair{names, entries.front().reason};
            // Several reasons: each one names the operator it belongs to, so the list of reasons
            // is readable on its own.
            std::string reasons;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i != 0)
                    reasons += "; ";
                reasons += entries[i].name + ": " + entries[i].reason;
            }
            return std::pair{names, reasons};
        };
        for (auto &[graph, scope] : refusals) {
            if (!scope.notExpanded.empty()) {
                // The repair is a pass, so the message names it (design principles 9.2: a library
                // checks its own preconditions and diagnoses the pass that is missing). This is the
                // one refusal that stays a diagnostic: the promise is not kept because the pipeline
                // is incomplete, and the compiler can say so before anything is asked of the device.
                auto diag = context.rtmDiags()->of(
                    core::error::RuntimeDiag::GpuScopeMacroNotExpanded);
                const auto origin = scope.notExpanded.front().origin;
                if (origin != camel::source::kInvalidOriginId)
                    diag.atOrigin(origin);
                diag.commit(
                    scopeNameOf(*graph), list(scope.notExpanded, false).first, "std::macro");
            }
        }
        // Everything else a scope cannot run is one error value, not a diagnostic (design
        // principles 9.1): `tiles::compile` is `(f: F) => F!`, and what a scope can run is only
        // known once the pass has looked at the graph. The message names the scope and every
        // operator it cannot run, with the reason each one gives, so a scope that fails is answered
        // once (GPU design 2.4) and the value carries the first operator's position.
        std::string refusalsText;
        camel::source::origin_id_t refusalsOrigin = camel::source::kInvalidOriginId;
        for (auto &[graph, scope] : refusals) {
            const std::string scopeName = scopeNameOf(*graph);
            const auto add = [&](const std::vector<Refusal> &entries, bool predicateInName,
                                 const char *tail) {
                if (entries.empty())
                    return;
                if (!refusalsText.empty())
                    refusalsText += "; ";
                const auto text = list(entries, predicateInName);
                refusalsText +=
                    "`#gpu` scope '" + scopeName + "': " + text.first + tail + " (" + text.second + ")";
                if (refusalsOrigin == camel::source::kInvalidOriginId)
                    refusalsOrigin = entries.front().origin;
            };
            add(scope.unsupported, false, " cannot run on the device");
            add(scope.noProgram, true, "");
        }
        if (!refusalsText.empty() && error != nullptr) {
            *error = core::rtdata::makeError(
                "GpuScopeUnsupported", refusalsText, sourceLocationOf(context, refusalsOrigin));
        }
        if (!refusals.empty())
            return program;
    }
    bool regionRefused = false;
    for (auto *graph : graphs) {
        const auto &analysis = analyses.at(graph);
        if (analysis.regions.empty())
            continue;
        auto &draft = session.edit(graph);
        std::unordered_map<size_t, rt::gc_node_ref_t> resident;
        const rt::ControlRegions control(*graph);
        // Where a segment runs: the control region of its members (docs/design/gpu/plan.cn.md 2.5). An operator
        // written in an arm runs only when that arm is taken, so its call, its uploads and its
        // downloads are written there too; writing them in the body would run the untaken arm.
        const auto whereOf = [&](const Segment &region) {
            return region.members.empty()
                       ? 0u
                       : control.controlOf(static_cast<rt::gc_node_ref_t>(region.members.front()));
        };
        // An upload is read by every region that takes that value as an input, so it is written in
        // the innermost control region that encloses all of them: two arms of one branch share one
        // upload in the body, because an arm may not read what another arm wrote.
        std::unordered_map<size_t, uint32_t> uploadControl;
        for (const auto &region : analysis.regions) {
            const uint32_t where = whereOf(region);
            for (auto value : region.inputs) {
                if (std::find(
                        analysis.residency.uploads.begin(),
                        analysis.residency.uploads.end(),
                        value) == analysis.residency.uploads.end())
                    continue;
                const auto found = uploadControl.find(value);
                if (found == uploadControl.end()) {
                    uploadControl.emplace(value, where);
                    continue;
                }
                uint32_t common = found->second;
                while (common != 0 && !control.within(where, common))
                    common = control.parentControl(common);
                found->second = common;
            }
        }
        if (device) {
            for (auto value : analysis.residency.uploads) {
                const auto input  = static_cast<rt::gc_node_ref_t>(value);
                const auto found  = uploadControl.find(value);
                draft.setInsertRegion(found == uploadControl.end() ? 0 : found->second);
                const auto upload = transferAggregate(
                    draft,
                    input,
                    graph->node(input)->dataType(),
                    true,
                    device->upload,
                    device->uploadUri);
                draft.setNodeOrigin(upload, graph->nodeOrigin(input));
                draft.setInsertRegion(0);
                resident.emplace(value, upload);
            }
        }
        std::vector<std::pair<rt::gc_node_ref_t, rt::gc_node_ref_t>> replacements;
        for (const auto &region : analysis.regions) {
            auto extracted  = extractRegion(*graph, region);
            const auto name = graph->name() + "/tiles.region/" + std::to_string(count++);
            extracted.graph->setId(context.graphIds().allocate());
            auto *callee = extracted.graph->encode(name, name);
            auto *fn     = ::Function::create(
                rt::makeProgram(context, program, std::span(&callee, 1)),
                callee->id(),
                core::type::TupleType::create(std::vector<core::type::Type *>{}),
                core::mm::autoSpace());
            if (run) {
                // Publish the owned compiled template as a graph fact. Draft commit
                // preserves this unedited callee's id and revision, including copies.
                try {
                    (void)compileTypes(fn, context.facts());
                } catch (const UnsupportedComputation &e) {
                    // A region is compiled from the static types its nodes carry; unlike a direct
                    // call there is no later run with actual shapes that could fix it. Inside a
                    // `#gpu` scope that is the refusal the design asks for; outside one the host
                    // may still run the region, which is what the pass did before.
                    if (!regionInScope(*graph, region))
                        continue;
                    const auto origin =
                        graph->nodeOrigin(static_cast<rt::gc_node_ref_t>(region.members.front()));
                    const bool shape =
                        e.oper().empty() || e.reason() == UnsupportedComputation::Reason::Shape;
                    const std::string who = e.oper().empty() ? std::string("<region>") : e.oper();
                    // The pass cannot keep the scope's promise for this region, so it fails with
                    // the same error value the grouped refusal makes (design principles 9.1): what
                    // a scope can run is only known once the pass has looked at the graph. The
                    // message takes the whole predicate, as the grouped report does.
                    if (error != nullptr) {
                        *error = core::rtdata::makeError(
                            "GpuScopeUnsupported",
                            std::format(
                                "`#gpu` scope '{}': operator '{}' has no device program ({})",
                                scopeNameOf(*graph),
                                who,
                                shape ? "no device program for this shape"
                                      : "no device program for this operator"),
                            sourceLocationOf(context, origin));
                    }
                    regionRefused = true;
                    break;
                }
            }
            if (regionRefused)
                break;
            auto fnNode =
                draft.materializeStaticValue(core::rtdata::toSlot(fn), callee->funcType());
            auto *type = callee->funcType()->exitType();
            std::vector<rt::gc_node_ref_t> args(
                extracted.parameters.begin(),
                extracted.parameters.end());
            if (device) {
                // A parameter the conclusion wrote nothing for is already where the segment runs:
                // it keeps the source value, and the segment reads it in place (a call between two
                // graphs of one class moves nothing, `tiles/internal/residency.h`).
                for (auto &arg : args) {
                    const auto found = resident.find(arg);
                    if (found != resident.end())
                        arg = found->second;
                }
                if (region.outputs.size() == 1)
                    type = residentType(type);
                else {
                    std::vector<core::type::Type *> fields;
                    for (auto value : region.outputs)
                        fields.push_back(residentType(
                            graph->node(static_cast<rt::gc_node_ref_t>(value))->dataType()));
                    type = core::type::TupleType::create(fields);
                }
            }
            rt::gc_node_ref_t call;
            // The call is written where its members were: in the region that holds them, so an
            // arm's segment runs in the arm and a loop's segment runs where the loop runs.
            draft.setInsertRegion(whereOf(region));
            if (run) {
                const auto packed = args.size() == 1 ? args.front() : packGraphTuple(draft, args);
                call              = draft.addOperNode(
                    type,
                    run,
                    device ? device->executeUri : uri,
                    draft.allocateRuntimeSlot(type));
                draft.setNormInputs(call, std::span(&packed, 1));
            } else {
                call = draft.addCallNode(type, {}, draft.allocateRuntimeSlot(type));
                draft.setNormInputs(call, args);
            }
            draft.setWithInputs(call, std::span(&fnNode, 1));
            // That context argument is the segment's own function constant, so this call is the
            // segment's exec node and the template published above is its plan: a segment is an
            // exec call carrying its graph, and the report operators read the graph, not the
            // wrapper (the target form of the GPU design, docs/design/gpu/plan.cn.md 2.1-2.3).
            // The members were statements of a chain (an operator written in an arm, a loop
            // written in the body). The call takes their place in it, so the host statements
            // written around the segment keep their order against it; erasing the members then
            // closes the chains over the call. What a replaced statement was ordered by, and what
            // it ordered, goes to the call in every kind that carries that order: the sync kind and
            // the order kind (takeStatementPlace), and the control kind, which closeChainOver writes
            // beside the sync one and which this outline moves itself below
            // (docs/design/effects/edges.cn.md 4). An effect written around the segment is
            // ordered by this edge alone, so a kind left behind takes the effect with it: with no
            // edge to the graph it is not executed, and the statement it observed is not
            // transferred either.
            const auto isMember = [&](rt::gc_node_ref_t ref) {
                return std::find(
                           region.members.begin(), region.members.end(),
                           static_cast<size_t>(ref)) != region.members.end();
            };
            std::vector<rt::gc_node_ref_t> controls;
            for (auto member : region.members) {
                const auto ref = static_cast<rt::gc_node_ref_t>(member);
                for (auto pred : graph->waitInputsOf(ref)) {
                    if (isMember(pred) ||
                        std::find(controls.begin(), controls.end(), pred) != controls.end())
                        continue;
                    controls.push_back(pred);
                }
                draft.takeStatementPlace(ref, call);
                // What the replaced statement ordered follows the call. Only its own place moves:
                // a user keeps every other reference it has to the node (the graph's exit, a
                // region's value, a branch's join), which the outline moves below with the values.
                const std::vector<rt::gc_node_ref_t> users = graph->waitUsersOf(ref);
                for (auto user : users) {
                    if (isMember(user) || !draft.alive(user))
                        continue;
                    std::vector<rt::gc_node_ref_t> ordered(
                        draft.ctrlInputsOf(user).begin(), draft.ctrlInputsOf(user).end());
                    bool moved = false;
                    for (auto &input : ordered) {
                        if (input != ref)
                            continue;
                        input = call;
                        moved = true;
                    }
                    if (moved)
                        draft.setCtrlInputs(user, ordered);
                }
            }
            for (auto pred : controls) {
                auto ctrl = draft.ctrlInputsOf(call);
                if (std::find(ctrl.begin(), ctrl.end(), pred) != ctrl.end())
                    continue;
                std::vector<rt::gc_node_ref_t> grown(ctrl.begin(), ctrl.end());
                grown.push_back(pred);
                draft.setCtrlInputs(call, grown);
            }
            for (size_t i = 0; i < region.outputs.size(); ++i) {
                const auto old   = static_cast<rt::gc_node_ref_t>(region.outputs[i]);
                auto replacement = call;
                if (region.outputs.size() > 1) {
                    auto *fieldType = draft.header(old)->dataType;
                    if (device)
                        fieldType = residentType(fieldType);
                    replacement = draft.addAccsNode(
                        fieldType,
                        static_cast<uint32_t>(i),
                        draft.allocateRuntimeSlot(fieldType));
                    draft.setNormInputs(replacement, std::span(&call, 1));
                }
                draft.setNodeOrigin(replacement, graph->nodeOrigin(old));
                if (device) {
                    resident.emplace(old, replacement);
                    if (std::find(
                            analysis.residency.downloads.begin(),
                            analysis.residency.downloads.end(),
                            old) != analysis.residency.downloads.end()) {
                        auto *hostType      = graph->node(old)->dataType();
                        const auto download = transferAggregate(
                            draft,
                            replacement,
                            hostType,
                            false,
                            device->download,
                            device->downloadUri);
                        draft.setNodeOrigin(download, graph->nodeOrigin(old));
                        replacement = download;
                    }
                }
                replacements.emplace_back(old, replacement);
            }
            draft.setInsertRegion(0);
        }
        // Host uses still refer to original IDs. Resident calls already use the
        // device map, so replacing host uses cannot route them through a download.
        for (auto [old, replacement] : replacements) {
            draft.replaceAllValueUses(old, replacement);
            if (draft.outputNode() == old)
                draft.setOutputNode(replacement);
            if (draft.exitNode() == old)
                draft.setExitNode(replacement);
            if (draft.returnNode() == old)
                draft.setReturnNode(replacement, draft.returnKind());
        }
        // The nodes of the condition and of the body of a loop went with the LOOP node.
        for (const auto &region : analysis.regions)
            for (auto id : region.members)
                if (graph->node(id)->kind == rt::GCNodeKind::Loop)
                    for (auto inner : rt::LoopView(*program, *graph, id).regionNodes())
                        draft.eraseNode(inner);
        for (const auto &region : analysis.regions)
            for (auto it = region.members.rbegin(); it != region.members.rend(); ++it)
                draft.eraseNode(static_cast<rt::gc_node_ref_t>(*it));
    }
    if (regionRefused)
        return program;
    return count ? session.commit() : program;
}
bool isRegionExec(core::OperId oper) {
    // An operator that calls the graph of the function value it receives is one this module
    // writes for a segment (`SegmentDeviceOps::execute`, op-semantics 11.4). The module's own
    // namespace is the guard: another module's graph-calling operator is not a segment of a
    // Tiles scope.
    const auto &table = core::OperatorTable::instance();
    return table.callsGraphs(oper) && table.uri(oper).starts_with("tiles:");
}
std::vector<RegionSegment> regionSegments(const runtime::GCGraph &graph) {
    namespace td = core::rtdata;
    std::vector<RegionSegment> segments;
    for (runtime::gc_node_ref_t id = 0; id < graph.nodeCount(); ++id) {
        const auto *node = graph.node(id);
        if (node->kind != runtime::GCNodeKind::Oper)
            continue;
        if (!isRegionExec(graph.nodeBodyAs<runtime::GCOperBody>(id)->oper))
            continue;
        // The pass writes exactly one context argument: the function value that owns the
        // segment's graph. A call without one is not a segment call and is left to the caller.
        const auto with = graph.withInputsOf(id);
        if (with.size() != 1)
            continue;
        const auto *carried = graph.node(with.front());
        if (carried->kind != runtime::GCNodeKind::Data || carried->dataIndex >= 0)
            continue;
        const auto slot = static_cast<size_t>(-carried->dataIndex);
        if (slot >= graph.staticSlots().size())
            continue;
        auto *type = carried->dataType();
        if (!type || type->code() != core::type::TypeCode::Function)
            continue;
        auto *function = td::fromSlot<::Function *>(graph.staticSlots()[slot]);
        if (!function || !function->graph())
            continue;
        segments.push_back(RegionSegment{id, function});
    }
    return segments;
}
} // namespace camel::tiles
