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

#include <cstdint>
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

bool Builder::enclosesOrigin(node_handle_t origin) const {
    if (origin == nullptr || currGraph_ == nullptr) {
        return false;
    }
    for (DraftGraphBuilder *outer = currGraph_.get(); outer != nullptr;
         outer = outer->outerGraph().get()) {
        if (nodeGraphOf(origin) == outer) {
            return true;
        }
    }
    return false;
}

node_handle_t Builder::captureInto(
    const compile_graph_ptr_t &graph,
    node_handle_t origin,
    const std::string &name,
    DraftGraphBuilder *capturing) {
    GUARD(graph != nullptr && origin != nullptr, "A capture needs a graph and a value.");
    // The graph the capture is made for: this frame's graph when the walk starts here, and
    // whatever the outermost frame was otherwise. It is not `currGraph_`: a call site captures
    // on a callee's behalf while the builder stands in another graph (`linkCaptureSites`).
    DraftGraphBuilder *const capturingGraph = capturing != nullptr ? capturing : graph.get();
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
    // The reading names the three graphs the walk had in hand, by name and by id. Five of this
    // function's six callers hand it an origin taken from a table -- the call's written captures
    // (`builder_operators.cpp`), a function value's captures (`builder_calls.cpp`),
    // `capturedUsesOf` (`builder_diagnostics.cpp`), name resolution (`builder.cpp`),
    // `linkCaptureSites` -- and ask no enclosure question first; only the name-lookup path
    // (`builder_expr.cpp`, `RestFunctionNeedsExpansion`) walks the ancestors itself. So when one of
    // the five meets a value its graph cannot reach, this report is everything a release build
    // says, and a sentence without the graphs cannot be followed. Printed:
    //   * the origin's graph -- where the captured node really lives;
    //   * the *capturing* graph, `capturingGraph`, which is the graph the port had to be made in
    //     and is *not* `currGraph_` when the caller captured on a callee's behalf;
    //   * `graph` -- where the walk stopped, the one with no enclosing graph;
    // and `currGraph_` as well, because when it differs from `capturingGraph` it names the code
    // that asked. The ids are what a graph is looked up by in a build that prints nothing else:
    // a name repeats (`__gen_step__`, `__sync__`), an id does not.
    // Every reading is safe here: `nodeGraphOf(origin)` was answered earlier in this call, so the
    // handle names a live draft by the time this line is reached, and the node id lives in the
    // handle rather than in the draft.
    GUARD(
        graph->outerGraph() != nullptr,
        std::format(
            "A captured value must belong to a graph that encloses the capturing one: node {} of "
            "graph '{}' (#{}) is captured into graph '{}' (#{}), and the walk up from it reached "
            "'{}' (#{}), which has no enclosing graph. The builder stands in graph '{}' (#{}).",
            nodeIdOf(origin),
            nodeGraphOf(origin)->name(),
            static_cast<uint32_t>(nodeGraphOf(origin)->graphId()),
            capturingGraph->name(),
            static_cast<uint32_t>(capturingGraph->graphId()),
            graph->name(),
            static_cast<uint32_t>(graph->graphId()),
            currGraph_ != nullptr ? currGraph_->name() : std::string("<no current graph>"),
            currGraph_ != nullptr ? static_cast<uint32_t>(currGraph_->graphId()) : 0u));
    (void)captureInto(graph->outerGraph(), origin, name, capturingGraph);

    const bool isVar = isVariable(origin);
    // A name the program declares with `var` is a name even where it stands for a value and not a
    // cell: design principles 3.5 (198) refuses assigning it from the making of this closure to its
    // last use, and that rule is about the name, not about where its value is kept. A `var` whose
    // value is a handle is a value and its cell is gone, so the record is kept for the
    // value the name stood for where the capture is written -- that value is what the closure holds.
    const bool namedVar = isVar || isSsaValue(origin, name);
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
    // wrote two names. A cell is not the value -- `var` is a name that may be bound again and a
    // capture is a capture of the value the name holds there (design principles 3.5), so the reads
    // of the name are judged where they stand, and the standard library's `tee` hands its source on
    // that way to key its report. What the rule refuses is the read the capture would make, so what the
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
    if (namedVar && !compilerMadeClosure(graph->name()) && !decoratedBlockGraph(graph)) {
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
                known.stretchToEnd =
                    knownUse != closureUseOf_.end() && knownUse->second.stretchToEnd;
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
    // A name the program hands to a call is NOT a reason to keep the cell. A `var` parameter is
    // "passed in by value, passed back by value" (design principles 3.5, 192): the callee writes
    // its own name, and the write-back is an assignment written at the call. Where a closure is
    // written AT the argument position of that very call, the closure's last use IS that call and
    // the write-back counts after it, so 3.5 (198) has no stretch to report and the program is
    // legal. `test/cases/linear/byref/var_param_reference.cml`'s `write_then_read` is that shape:
    // `var w = 0; write_then_read(w, (): int => w)` prints 0, because the closure captured the
    // value `w` had when it was made and `w = 7` in the callee only changes the callee's own name
    // until the call returns. 0 is the semantics, not a wrong value, so a name handed to a call
    // does not keep its cell on that account.

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

node_handle_t Builder::captureValueOf(const CapturedValue &captured) {
    const node_handle_t here = captureInto(currGraph_, captured.origin, captured.name);
    if (isVariable(captured.port)) {
        // A cell the closure shares: what it is worth as of this call is the value its nearest write
        // gave it, so a cell written before the call and again after it hands the first write on.
        const node_handle_t version = valueVersionOf(here);
        return version != nullptr ? version : here;
    }
    if (here != nullptr && isVariable(here)) {
        // A capture of a value is handed the value the variable holds where the call stands, never
        // the cell (design principles 3.5, 197). The read is the node the call takes as its input:
        // it is a reader of the cell like any other, so the fold at the end of the body hands it the
        // value the write wrote (collapseNonEscapingVariableReads) and the call is handed that value.
        return readVariable(here);
    }
    return here;
}

void Builder::addCaptureSite(node_handle_t call, const compile_graph_ptr_t &callee) {
    CaptureSite site;
    site.caller = currGraph_;
    site.call   = call;
    site.callee = callee;
    // The captures the callee has now are the call's `with` inputs from here on: the value a call is
    // handed is an edge of the graph, so the fold and the erasure follow it and no node has to be
    // kept alive beside the graph. A copy: capturing into the caller may add to the table.
    const std::vector<CapturedValue> captured = capturedValues_[callee.get()];
    site.firstCapture = withInputsOf(call).size();
    for (const CapturedValue &value : captured) {
        linkNodes(LinkType::With, captureValueOf(value), call);
        ++site.linked;
    }
    captureSites_.push_back(std::move(site));
}

void Builder::linkCaptureSites() {
    // The captures are already the call's `with` inputs (addCaptureSite). The one thing the call
    // could not place by itself is the order: its declared `with` arguments are written by the call's
    // own arguments, which are built after the callee is visited (visitLinkNode), so the captures
    // were appended before them. The callee's with-ports are the declared arguments first and the
    // captures after, and a call hands them over in that order (captureWriteMaskOf reads the written
    // versions out of the same one). Placed once, before anything reads these inputs: the link step
    // is the last thing the build does before the resource and linearity passes run.
    for (CaptureSite &site : captureSites_) {
        if (site.placed) {
            continue;
        }
        site.placed = true;
        const auto current = withInputsOf(site.call);
        const std::vector<draft_node_ref_t> inputs(current.begin(), current.end());
        const size_t total = inputs.size();
        if (site.linked == 0 || site.firstCapture + site.linked >= total) {
            // Nothing declared after the captures: they are the last inputs already.
            continue;
        }
        const auto first = inputs.begin() + static_cast<std::ptrdiff_t>(site.firstCapture);
        const auto last  = first + static_cast<std::ptrdiff_t>(site.linked);
        std::vector<draft_node_ref_t> ordered;
        ordered.reserve(total);
        ordered.insert(ordered.end(), inputs.begin(), first);
        ordered.insert(ordered.end(), last, inputs.end());
        ordered.insert(ordered.end(), first, last);
        nodeGraphOf(site.call)->edit().setWithInputs(nodeIdOf(site.call), ordered);
    }
    // Least fixpoint: passing a capture the callee made late may give the caller a capture
    // port of its own, which the calls of the caller then pass in turn.
    for (bool changed = true; changed;) {
        changed = false;
        for (CaptureSite &site : captureSites_) {
            const std::vector<CapturedValue> captured = capturedValues_[site.callee.get()];
            for (size_t k = site.linked; k < captured.size(); ++k) {
                const size_t before = capturedValues_[site.caller.get()].size();
                const node_handle_t value =
                    captureInto(site.caller, captured[k].origin, captured[k].name);
                if (capturedValues_[site.caller.get()].size() != before) {
                    changed = true;
                }
                // The With inputs of a FUNC node are its callee's with-ports in order: the
                // declared `with` arguments, then the captures. This one is a capture the callee
                // made after the call was built, so it is the call's last input. It is linked here
                // and not kept for later: the node exists for the length of this statement and is
                // an edge of the graph from the moment it does. A capture of a variable cannot
                // arrive this late -- captureInto refuses it (CaptureAfterRecursiveCall) -- so there
                // is no read to freeze where the program wrote the call.
                linkNodes(LinkType::With, value, site.call);
                ++site.linked;
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

bool Builder::isSsaValue(node_handle_t value, const std::string &name) const {
    if (value == nullptr || name.empty()) {
        return false;
    }
    const SsaEnv::Entry *entry = ssa_.find(nodeScope_, name);
    return entry != nullptr && ssa_.valueOf(entry) == value;
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
    // the end of the stretch is not known the report stays off -- see `CapturedVar::lastUse`. The one
    // end design principles 3.5 (198) names without an offset is a closure the program put into
    // another value: it runs to the rest of the scope, so a write anywhere after the making is
    // inside it (`CapturedVar::stretchToEnd`).
    if (!known.stretchToEnd && (known.lastUse == kNoSourceOffset || writtenAt >= known.lastUse)) {
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
