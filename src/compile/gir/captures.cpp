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
    // A `var` is a cell a closure shares only in the shapes `decideCaptureByValue` names and
    // exempts (a module-level `var`, a `var` parameter, the generator machine's step function and a
    // decorated block); everywhere else the port is a value port: it carries the value the variable
    // had where the capture is written, its type is that value's, and nothing that reads the port
    // can write the variable (design principles 3.5, 197).
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
    if (isVar && !compilerMadeClosure(graph->name()) && !decoratedBlockGraph(graph)) {
        // Design principles 3.5 (198): from the making of this closure to its last use the name may
        // not be assigned, or the closure would go on holding a value the name no longer has. Where
        // the closure stands is the start of that stretch, and `closureUseOf_` (the scan before
        // any body is built) gives its end; where the end is not known the stretch is not reported
        // at all (requireCapturedNameWritable), because a stretch guessed wide refuses a program the
        // design allows. A name of the top level of a module is another rule's, and a capture whose
        // place is unknown cannot say where the stretch starts.
        const size_t at = sourceOffsetOf(refOrigin_);
        if (at != kNoSourceOffset && topLevelValues_.find(origin) == topLevelValues_.end()) {
            const auto gct        = graphGctOf_.find(graph.get());
            const auto knownUse   = gct != graphGctOf_.end() ? closureUseOf_.find(gct->second)
                                                             : closureUseOf_.end();
            CapturedVar &known = capturedVarOf_[origin];
            if (known.closure.empty() || at < known.capturedAt) {
                known.capturedAt = at;
                // The name the report shows: the one the program bound the closure to, where the
                // scan knows it, because an anonymous function's own name is one the compiler made.
                known.closure = knownUse != closureUseOf_.end() && !knownUse->second.boundName.empty()
                                    ? knownUse->second.boundName
                                    : graph->name();
                known.lastUse =
                    knownUse != closureUseOf_.end() ? knownUse->second.lastUse : kNoSourceOffset;
            }
        }
    }
    return port;
}

bool Builder::captureByValue(const compile_graph_ptr_t &graph, node_handle_t origin) {
    if (!isVariable(origin)) {
        return false;
    }
    // A graph whose write has to land in the cell (`decideCaptureByValue`) records its answer
    // before the cache is read: a cell one such graph captures is a cell for every graph that
    // captures it, and an earlier "value" from a program closure must not hand the generator's step
    // function a value where it writes. This is the direction that keeps the cell.
    if (compilerMadeClosure(graph->name()) || decoratedBlockGraph(graph)) {
        captureByValueOf_[origin] = false;
        return false;
    }
    // One answer per cell: every capture of it has to agree (see `captureByValueOf_`).
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
    // The name the graph knows the value by: what the writer sets below are looked up with.
    const auto known = originNames_.find(origin);
    const std::string name =
        known != originNames_.end() ? known->second : captureNameOf(origin, std::string{});
    if (name.empty()) {
        return false;
    }
    // A module-level `var` keeps its cell where some function of the program writes it: `uses var`
    // and `pure` are judged on the resources a body reaches, and a function the program made a value
    // of still reaches the module cell it reads, so the account rides on the cell until the module
    // `var` work lands (`test/cases/linear/toplevel_var/err_pure_parameter.cml`: `run(bump)` asks
    // whether `bump` uses `cnt`). A cell the body declared itself does not consult this set: the set
    // matches by NAME, and a write another function makes of a same-named variable cannot reach a
    // local cell, so consulting it there refused a capture the rule allows.
    if (!localCell && programWrites(name)) {
        return false;
    }
    // A name the program hands to a call may be written by that call: a `var` parameter and a
    // `var` `with` parameter are the caller's cell, so the write reaches this cell from a body the
    // closure's capture does not stand for. Design principles 3.5 (198) refuses that write
    // (`CapturedVarHandedToVarParameter`), but not in the shape where the closure is made at the
    // argument position of the very call that hands the name over: there the closure's last use IS
    // that call, and the write happens inside it, so the stretch the rule needs is empty and the
    // report stays off. `test/cases/linear/byref/var_param_reference.cml`'s `write_then_read` is
    // that shape, and its reading is 7 (`w = 7` then `read()`): by value it would be 0. The gate
    // stays until either that shape is reported or the case is settled the other way.
    if (programMayWriteThroughCall(name)) {
        return false;
    }
    // The cell is kept where the rule above is not the one that governs the write. Design
    // principles 3.5 (197) makes a name a closure mentions unassignable, and the write a closure
    // makes of it is refused by `requireCapturedNameWritable` -- except in two shapes, which that
    // function names and exempts for the same reason: the generator machine's own step function,
    // which writes the cells of the body it was cut from between two `yield`s (visitGeneratorBody),
    // and a decorated block, whose write is the block's own. In both, the write has to land in the
    // cell the closures around it share, so the capture stays a capture of the cell.
    if (compilerMadeClosure(graph->name()) || decoratedBlockGraph(graph)) {
        return false;
    }
    // Where neither shape holds, the cell's value cannot change after the capture -- a write of
    // the name is now refused, in the closure and in the body around it alike (3.5, 197 and 198) --
    // and the capture is the value the name holds there.
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

bool Builder::heldByCaptureSite(node_handle_t value) const {
    for (const CaptureSite &site : captureSites_) {
        for (size_t k = site.linked; k < site.values.size(); ++k) {
            if (site.values[k] == value) {
                return true;
            }
        }
    }
    return false;
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

bool Builder::compilerMadeClosure(const std::string &graphName) {
    // The generator machine builds one function of its own -- the step function that runs the body
    // from one `yield` to the next (`__gen_step__`, gct/builder.cpp visitGeneratorBody) -- and that
    // function writes the cells of the body it was cut from. The program wrote no closure there, so
    // the rule of design principles 3.5 is not asked about it: `ok_generator_body_is_not_a_program
    // _closure` pins that this shape keeps working.
    return graphName.starts_with("__gen_");
}

bool Builder::decoratedBlockGraph(const compile_graph_ptr_t &graph) const {
    const auto found = graph == nullptr ? graphGctOf_.end() : graphGctOf_.find(graph.get());
    return found != graphGctOf_.end() && found->second != nullptr && found->second->decoratedBlock();
}

void Builder::requireCapturedNameWritable(
    node_handle_t cell, const std::string &name, bool throughCall,
    camel::source::origin_id_t at) {
    if (cell == nullptr) {
        return;
    }
    // The compiler's own closures are not the program's (visitGeneratorBody's `__gen_step__` writes
    // the cells of the body it was cut from), and a decorated block's write is the block's own: the
    // rule asks about a closure the writer made.
    if (currGraph_ != nullptr &&
        (compilerMadeClosure(currGraph_->name()) || decoratedBlockGraph(currGraph_))) {
        return;
    }
    // The write stands inside a closure, and the name it writes is one the closure captured from the
    // body around it: a value has nowhere to write, so the assignment is refused (3.5, 197). A name
    // of the top level of a module is left to the module `var`'s own rule.
    if (const auto port = captureOriginOf_.find(cell); port != captureOriginOf_.end()) {
        if (topLevelValues_.find(port->second) != topLevelValues_.end()) {
            return;
        }
        const std::string shown = originNames_.contains(port->second)
                                    ? originNames_.at(port->second)
                                    : name;
        diags_->of(SemanticDiag::ClosureAssignsCapturedName).atOrigin(at).commit(shown);
        throw BuildAbortException();
    }
    // A write the body that declares the name makes: it changes a name a closure of that body
    // already holds the value of, and the write does not reach the closure (3.5, 198).
    const auto captured = capturedVarOf_.find(cell);
    if (captured == capturedVarOf_.end()) {
        return;
    }
    const CapturedVar &known = captured->second;
    const size_t writtenAt   = sourceOffsetOf(at);
    if (writtenAt == kNoSourceOffset || writtenAt <= known.capturedAt) {
        return;
    }
    // The rule runs to the closure's last use and not one byte further: a name assigned after the
    // closure is done with is a name nothing holds any more, and the design keeps that legal. Where
    // the end of the stretch is not known the report stays off -- see `CapturedVar::lastUse`.
    if (known.lastUse == kNoSourceOffset || writtenAt >= known.lastUse) {
        return;
    }
    const std::string shown = known.closure;
    if (throughCall) {
        // The write is a call's: a `var` parameter is the caller's cell, and its write-back is the
        // assignment written at the call (3.5, 192). Named apart from the assignment written here so
        // that the reader is told where the write actually stands.
        diags_->of(SemanticDiag::CapturedVarHandedToVarParameter).atOrigin(at).commit(name, shown);
    } else {
        diags_->of(SemanticDiag::CapturedVarWrittenAfterTheClosure).atOrigin(at).commit(name, shown);
    }
    throw BuildAbortException();
}

} // namespace camel::compile::gir
