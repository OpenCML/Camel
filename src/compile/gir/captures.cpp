// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Capture ports and their binding (docs/design/graph-model/README.cn.md 3.7, rule C1).
 *
 * A graph that uses a value of an enclosing graph holds it in a capture port; the port is
 * bound to the node it stands for (its origin). A value of the top level of a module is a
 * node of the root graph, so every function of every module can capture it. Where a graph is
 * called directly (a FUNC node), the caller passes each capture of the callee as a With
 * input; where it becomes a function value, the captures are filled into the value. In both
 * cases the caller supplies the value of the same origin as the caller sees it, which may
 * give the caller a capture port of its own: captures travel up the call chain to the graph
 * the value is computed in, the root graph for a top-level value.
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/utils/assert.h"

#include <format>

using namespace camel::core::error;

namespace camel::compile::gir {

std::string Builder::captureNameOf(node_handle_t origin, const std::string &fallback) const {
    const auto it = topLevelValues_.find(origin);
    if (it == topLevelValues_.end()) {
        return fallback;
    }
    // A value of the current module is shown as the module writes it; one of another module
    // with its module's name, the way it is named from outside (design principles 10.2).
    const TopLevelValue &value = it->second;
    if (module_ && value.module == module_.get()) {
        return value.name;
    }
    return std::format("{}::{}", value.module->name(), value.name);
}

node_handle_t Builder::captureInto(
    const compile_graph_ptr_t &graph, node_handle_t origin, const std::string &name) {
    GUARD(graph != nullptr && origin != nullptr, "A capture needs a graph and a value.");
    if (nodeGraphOf(origin) == graph.get()) {
        return origin;
    }
    if (!name.empty()) {
        originNames_.try_emplace(origin, name);
    }
    if (auto known = capturedValues_.find(graph.get()); known != capturedValues_.end()) {
        for (const CapturedValue &captured : known->second) {
            if (captured.origin == origin) {
                return captured.port;
            }
        }
    }
    // The origin is a node of a graph that encloses this one (the root graph for a value of
    // the top level of a module): the enclosing graphs between them capture it as well, so
    // that whoever calls or makes the value of this graph can pass it on.
    GUARD(
        graph->outerGraph() != nullptr,
        "A captured value must belong to a graph that encloses the capturing one.");
    (void)captureInto(graph->outerGraph(), origin, name);

    const bool isVar = isVariable(origin);
    // A function value holds the captures its graph had when it was made; a direct call is
    // given a capture made later at the link step (linkCaptureSites), but not one of a
    // variable: the call has been ordered among the uses of the variables it was known to use
    // (recordWrite), and that cannot be redone once the caller is built. Function bodies are
    // built before they are used (ensureFunctionBuilt), so this is only met in recursion.
    if (valueUsedGraphs_.contains(graph.get()) || (graph->parameterized() && isVar)) {
        // Once per function and value: the next use of the value fails the same way.
        if (!lateCaptures_.insert({graph.get(), origin}).second) {
            throw BuildAbortException();
        }
        const std::string shownName =
            captureNameOf(origin, name.empty() ? originNames_[origin] : name);
        diags_->of(SemanticDiag::CaptureAfterRecursiveCall)
            .atOrigin(refOrigin_)
            .commit(shownName, graph->name());
        throw BuildAbortException();
    }

    const std::string shown = captureNameOf(origin, name.empty() ? originNames_[origin] : name);
    // A capture of a value, where the value is what the port holds: the capture a closure makes
    // of a variable it reads, and nothing writes. A `var` is still a cell a closure may share
    // (`escapingVariables_`, below), so the port is a value port only where nothing can write the
    // name while the capturing body holds it: no function of the program writes it (the
    // conservative writer set, scanProgramWriters), the cell is one the program declared
    // (`:var/new`) and not a `var` parameter a caller handed in, and the graph is one the program
    // is making a value of (its captures are closure ports, values it holds) rather than one a
    // direct call is being built for (whose capture ports become with-ports). Where that holds the
    // port carries the value the variable had where the capture is written, its type is that
    // value's, and nothing that reads the port can write the variable.
    const bool byValue = captureByValue(graph, origin);
    Type *portType     = byValue ? valueTypeOf(nodeTypeOf(origin)) : nodeTypeOf(origin);
    node_handle_t port = graph->addPortNode(portType != nullptr ? portType : nodeTypeOf(origin), shown, false);
    graph->edit().removeNormPort(nodeIdOf(port));
    if (graph->parameterized()) {
        // Called directly already: its captures are with-ports, after the declared ones.
        graph->edit().appendWithPort(nodeIdOf(port));
    } else {
        graph->edit().appendClosureNode(nodeIdOf(port));
    }
    graph->addCaptureName(shown);
    capturedValues_[graph.get()].push_back({origin, port, shown});
    captureOriginOf_[port] = origin;
    // A stream a closure captures is refused where the closure is built (design principles 5.4):
    // the body of a closure runs after the path that made it has finished, so the read such a
    // capture would make is not that path's one read. It is refused here, at the one place a
    // capture is made, and not by the pass that counts reads: inside the closure the capture port
    // and what it captured are one value, so counting edges would see one read where the program
    // wrote two names. A cell is not the value -- a captured `var` is a resource the closure
    // shares, and the reads of it are judged where they stand (the standard library's `tee` holds
    // its source that way). What the rule refuses is the read the capture would make, so what the
    // closure makes of the port is settled with the pass, once its body is built: a closure that
    // only hands the value on -- into a cell, into a report, into the captures of a call -- reads
    // nothing, and the standard library's `tee` holds its source that way to key its report.
    if (!isVar && camel::compile::gir::streamReadOnce(nodeTypeOf(origin))) {
        StreamViolation violation;
        violation.kind   = StreamViolation::Kind::Captured;
        violation.graph  = graph.get();
        violation.port   = port;
        violation.origin = nodeDraftOf(origin)->nodeOrigin(nodeIdOf(origin));
        Type *valueType  = valueTypeOf(nodeTypeOf(origin));
        violation.what   = valueType != nullptr ? valueType->toString() : std::string("a stream");
        streamViolations_.push_back(std::move(violation));
    }
    if (isVar && !byValue) {
        // A cell the closure shares: the port is the variable, reads of it are `:var/get`, and the
        // cell has to be kept because a body reads it as a cell.
        variables_.insert(port);
        captures_[graph.get()].variables.push_back(origin);
        escapingVariables_.insert(origin);
    } else if (isVar) {
        // A value port names the variable of the capturing graph's scope without making it one:
        // a read of it reads a value (`variables_` is what makes `:var/get` of a name), and the
        // cell keeps no share in it, so no unit is kept for a body that only reads.
        captures_[graph.get()].variables.push_back(origin);
        // Where the cell is, so that the graph that declares it drops the unit nobody reads any
        // more (dropCarriedVariableUnits).
        valueCapturedCells_.insert(origin);
    }
    return port;
}

bool Builder::captureByValue(const compile_graph_ptr_t &graph, node_handle_t origin) {
    if (!isVariable(origin)) {
        return false;
    }
    // One answer per cell: every capture of it has to agree (see `captureByValueOf_`). The first
    // capture is the earliest one written, so a write written past it is a write written past all
    // of them, which is the direction that keeps the cell.
    if (const auto decided = captureByValueOf_.find(origin); decided != captureByValueOf_.end()) {
        return decided->second;
    }
    const bool byValue        = decideCaptureByValue(graph, origin);
    captureByValueOf_[origin] = byValue;
    return byValue;
}

bool Builder::decideCaptureByValue(const compile_graph_ptr_t &graph, node_handle_t origin) const {
    // A closure written where it stands captures a cell of the body it stands in by value (design
    // principles 3.5, 197): a lambda and a function declared inside a body alike. A value of the
    // top level of a module keeps the earlier step's rule -- it is a value where the function is
    // used as a value -- because `uses var` and `pure` are judged on the resources a body reaches,
    // and a module `var` a body reads is one of them until the module `var` work lands.
    const bool localCell = topLevelValues_.find(origin) == topLevelValues_.end();
    if (!localCell && !valuePortGraphs_.contains(graph.get()) &&
        !programMakesValueOf(graph->name())) {
        return false;
    }
    // A `var` parameter is a cell a caller handed in: it is the caller's, and the callee's writes
    // go through to it. Only a cell the program declared itself is a candidate.
    if (nodeKindOf(origin) != camel::runtime::GCNodeKind::Oper) {
        return false;
    }
    // The name the graph knows the value by. A capture with no name (`originNames_` holds what
    // `captureInto` was handed) cannot be looked up in the writer sets, so it keeps its cell.
    const auto known = originNames_.find(origin);
    const std::string name =
        known != originNames_.end() ? known->second : captureNameOf(origin, std::string{});
    if (name.empty()) {
        return false;
    }
    // A name some function of the program writes is shared: that write would have to reach the
    // body through the cell, and a value port cannot carry it. This is the conservative set, so a
    // write of another binding that happens to share the name costs the capture its value port --
    // the safe direction, because it only keeps a cell that could have been a value.
    if (programWrites(name)) {
        return false;
    }
    // A name the program hands to a call may be written by that call: a `var` parameter and a
    // `var` `with` parameter are the caller's cell, so the write reaches this cell from a body the
    // closure's capture does not stand for. Until the scan can tell the `var` positions from the
    // ordinary ones, every name passed as an argument keeps its cell (program.cpp, `handedOver_`).
    if (programMayWriteThroughCall(name)) {
        return false;
    }
    // Design principles 3.5 (197) makes a name a closure mentions unassignable from the closure's
    // creation to its last use, and the two writes that shape allows are exactly the two cases
    // above and below: a write from a closure (counted by `programWrites`) and a write from the
    // body the closure is written in, past it. That second rule is not reported yet, so a name a
    // write is written past keeps its cell: a value port would silently hand the closure the value
    // the name had where it was captured, and the program would answer something else than it does
    // today. Where the capture itself stands is not known, the cell is kept for the same reason.
    const size_t captureAt = sourceOffsetOf(refOrigin_);
    if (captureAt == kNoSourceOffset) {
        return false;
    }
    const size_t writtenAt = lastWriteOffsetOf(name);
    if (writtenAt != kNoSourceOffset && writtenAt > captureAt) {
        return false;
    }
    // Where neither write exists, the cell's value cannot change after the capture, and the capture
    // is the value the name holds there (design principles 3.5).
    return true;
}

void Builder::noteMachinePortOrigin(node_handle_t port, node_handle_t origin) {
    GUARD(port != nullptr && origin != nullptr, "A port stands for a value.");
    machinePortOrigin_[port] = origin;
}

void Builder::addCaptureSite(node_handle_t call, const compile_graph_ptr_t &callee) {
    CaptureSite site;
    site.caller = currGraph_;
    site.call   = call;
    site.callee = callee;
    // A copy: capturing into the caller may add to the table.
    const std::vector<CapturedValue> captured = capturedValues_[callee.get()];
    site.values.reserve(captured.size());
    for (const CapturedValue &value : captured) {
        node_handle_t here = captureInto(currGraph_, value.origin, value.name);
        if (!isVariable(value.port) && here != nullptr && isVariable(here)) {
            // A callee whose capture port carries a value (a function the program also makes a
            // value of, `captureByValue`) is handed the value the variable holds here, never the
            // cell: the read is the node the capture table holds for this call, and the control
            // edge keeps it alive to the link step (linkCaptureSites turns it into the With input,
            // tryRemoveCtrlLink). A read nothing uses is erased when the body ends, and the call
            // would be linked to a node that is gone -- which is where this port crashed.
            node_handle_t read = readVariable(here);
            linkNodes(LinkType::Ctrl, read, call);
            here = read;
        }
        site.values.push_back(here);
        // What the capture is worth as of this call: a capture of a variable already written here
        // passes that write's version on, not the cell. The call is linked to it later
        // (linkCaptureSites), when the caller may already have written again.
        site.versions.push_back(isVariable(value.port) ? valueVersionOf(here) : nullptr);
    }
    captureSites_.push_back(std::move(site));
}

void Builder::linkCaptureSites() {
    // Least fixpoint: passing a capture the callee made late may give the caller a capture
    // port of its own, which the calls of the caller then pass in turn.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < captureSites_.size(); ++i) {
            const std::vector<CapturedValue> captured =
                capturedValues_[captureSites_[i].callee.get()];
            for (size_t k = captureSites_[i].linked; k < captured.size(); ++k) {
                CaptureSite &site = captureSites_[i];
                node_handle_t value = nullptr;
                if (k < site.values.size()) {
                    value = site.versions[k] != nullptr ? site.versions[k] : site.values[k];
                } else {
                    const size_t before = capturedValues_[site.caller.get()].size();
                    value = captureInto(site.caller, captured[k].origin, captured[k].name);
                    if (capturedValues_[site.caller.get()].size() != before) {
                        changed = true;
                    }
                }
                // The With inputs of a FUNC node are its callee's with-ports in order: the
                // declared `with` arguments, linked where the call was built, then the
                // captures, linked here. A capture of a variable this caller wrote is that
                // write's version, which is also what the call's value carries it as
                // (callValueTypeOf, Builder::valueVersionOf).
                tryRemoveCtrlLink(value, site.call);
                linkNodes(LinkType::With, value, site.call);
                site.linked = k + 1;
            }
        }
    }
}

} // namespace camel::compile::gir
