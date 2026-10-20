/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file frames.cpp
 * @brief The frames the debug core shows (docs/technical/35_debugger.md, 5.4), and the lines
 *        a line step went past without executing them (4.2).
 *
 * The shadow stack of debug_core.cpp has a frame per activation of a graph, as the schedulers
 * run them. The arm of a branch is a graph of its own, so a function that branches is, on that
 * stack, its own frame plus a frame per arm it entered. Design principles 12.1: the debugger
 * shows the arms as part of the frame of the function they are written in, and can show the
 * real subgraphs instead. So every shadow frame belongs to an *activation*:
 *
 *  - a frame of an arm called by a frame of the graph that holds the branch (the arm's owner)
 *    is part of that frame's activation; so is the frame of an arm that replaced its owner's
 *    frame by a tail call (the branch was the last thing the graph did);
 *  - every other frame starts an activation: the call of a function or a lambda, and the tail
 *    call of one, which replaces an activation when the replaced frame was its only frame.
 *
 * With `frames` set to "functions" (the default) a frame of the stack is an activation: the
 * shadow frames of one activation are a run of adjacent frames, shown as one, named after the
 * function, with the locals of the function and of the arms it is in. With "subgraphs" every
 * shadow frame is shown as it is. Node steps and the graph view work on the shadow frames in
 * both cases: they are the graph-node view.
 */

#include "debug_core.h"

#include <algorithm>
#include <filesystem>

namespace debugger::core {

using namespace camel::runtime;
using json = nlohmann::json;

namespace {

bool contains(const std::vector<GCGraph *> &graphs, GCGraph *graph) {
    return std::find(graphs.begin(), graphs.end(), graph) != graphs.end();
}

} // namespace

// ---------------------------------------------------------------------------------------
// Activations
// ---------------------------------------------------------------------------------------

namespace {

/// The function an activation that starts with a frame of `info` runs: the graph itself, or
/// for a subgraph that folds (which normally joins the activation of its owner, so this is
/// for the frame of an arm called from elsewhere, after a pass moved its branch), the graph
/// it is part of. `graphOf` gives the GraphInfo of a graph.
template <typename GraphOf> GraphInfo *functionOf(GraphInfo &info, GraphOf &&graphOf) {
    GraphInfo *current = &info;
    // An arm of an arm is part of the outer arm; the chain ends at a function (bounded: a
    // malformed cycle stops at the last graph seen).
    for (int guard = 0; guard < 64 && current->folds() && !current->owners.empty(); ++guard) {
        current = &graphOf(current->owners.front());
    }
    return current;
}

} // namespace

void DebugCore::placeCallee(ShadowFrame &f, const ShadowFrame *caller) {
    if (caller != nullptr && f.info->folds() && contains(f.info->owners, caller->graph)) {
        // An arm of a branch of the caller's graph: code of the same function, on the line
        // the function is on.
        f.activation        = caller->activation;
        f.presentedDepth    = caller->presentedDepth;
        f.function          = caller->function;
        f.functionsReplaced = caller->functionsReplaced;
        f.activationLine    = caller->activationLine;
        return;
    }
    f.activation     = nextActivation_++;
    f.presentedDepth = (caller != nullptr ? caller->presentedDepth : 0) + 1;
    f.awaitingEntry  = true;
    std::lock_guard lock(indexMutex_);
    f.function = functionOf(*f.info, [this](GCGraph *g) -> GraphInfo & { return index_.graph(g); });
}

void DebugCore::placeReplacement(ShadowFrame &f, const ShadowFrame &old, const ShadowFrame *below) {
    if (f.info->folds() && contains(f.info->owners, old.graph)) {
        // The branch was the last thing the replaced graph did: its arm goes on with the
        // activation, in the graph's place.
        f.activation        = old.activation;
        f.presentedDepth    = old.presentedDepth;
        f.function          = old.function;
        f.functionsReplaced = old.functionsReplaced;
        f.activationLine    = old.activationLine;
        return;
    }
    f.activation    = nextActivation_++;
    f.awaitingEntry = true;
    {
        std::lock_guard lock(indexMutex_);
        f.function =
            functionOf(*f.info, [this](GCGraph *g) -> GraphInfo & { return index_.graph(g); });
    }
    if (below != nullptr && below->activation == old.activation) {
        // The replaced frame was an arm whose function goes on below it (the branch was not
        // the last thing the function did): as the user sees it, the function called this one.
        f.presentedDepth    = old.presentedDepth + 1;
        f.functionsReplaced = 0;
    } else {
        // The replaced frame was all there was of its activation.
        f.presentedDepth    = old.presentedDepth;
        f.functionsReplaced = old.functionsReplaced + 1;
    }
}

void DebugCore::handOnLine(size_t endingFrom) {
    if (endingFrom == 0 || endingFrom > stack_.size()) {
        return;
    }
    ShadowFrame &survivor = stack_[endingFrom - 1];
    // The innermost ending frame of the survivor's activation knows where it is now: the
    // activations of a stack are runs of adjacent frames, and a frame copies the position
    // when it joins one.
    for (size_t i = stack_.size(); i-- > endingFrom;) {
        if (stack_[i].activation == survivor.activation) {
            survivor.activationLine = stack_[i].activationLine;
            return;
        }
    }
}

DebugCore::Chain DebugCore::chainOf(ShadowFrame &top) {
    Chain chain{&top};
    if (!foldsFrames() || stack_.empty() || &top < stack_.data() ||
        &top >= stack_.data() + stack_.size()) {
        return chain;
    }
    for (size_t i = static_cast<size_t>(&top - stack_.data()); i > 0;) {
        --i;
        if (stack_[i].activation != top.activation) {
            break;
        }
        chain.push_back(&stack_[i]);
    }
    return chain;
}

json DebugCore::frameJson(const Chain &chain, bool top) {
    const ShadowFrame &f = *chain.front(); // the frame that runs
    // The graph that runs, whatever is shown: the graph view and node breakpoints use it.
    json j{{"id", f.id}, {"graph", f.info->name}, {"graphId", f.info->stableId}};
    std::string name;
    uint32_t replaced = 0;
    if (foldsFrames()) {
        const GraphInfo *function = f.function != nullptr ? f.function : f.info;
        name                      = function->display;
        replaced                  = f.functionsReplaced;
        j["function"]             = function->name;
        // The subgraphs folded into the frame, outermost first.
        json subgraphs = json::array();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if ((*it)->info != function) {
                subgraphs.push_back((*it)->info->display);
            }
        }
        if (!subgraphs.empty()) {
            j["subgraphs"] = std::move(subgraphs);
        }
    } else {
        name     = f.info->display;
        replaced = f.tailReplaced;
        if (f.info->folds()) {
            j["arm"] = true;
        }
    }
    if (replaced > 0) {
        // DESIGN-QUESTION(tail-call-stack): the callers a tail call replaced are gone; say so
        // instead of inventing them.
        name += " (tail call, " + std::to_string(replaced) +
                (replaced == 1 ? " frame replaced)" : " frames replaced)");
        j["tailReplaced"] = replaced;
    }
    if (f.loc.valid()) {
        int function;
        std::string path;
        {
            std::lock_guard lock(indexMutex_);
            function = index_.functionOf(f.loc);
            path     = index_.filePath(f.loc.file);
            if (function != f.info->function && function >= 0) {
                const std::string &from = index_.function(function)->name;
                name += " [inlined code of " + from + "]";
                j["inlinedFrom"] = from;
            }
        }
        j["path"]      = path;
        j["line"]      = f.loc.line;
        j["column"]    = f.loc.column;
        j["endLine"]   = f.loc.endLine;
        j["endColumn"] = f.loc.endColumn;
    }
    j["name"] = name;
    j["top"]  = top;
    if (top) {
        // A bounded, immutable snapshot for graph clients. Use declared parameter ports,
        // not every local of an arm; no variable handles survive a resume.
        j["activation"]           = f.activation;
        j["entry"]                = f.atEntry;
        const GraphInfo *function = f.function != nullptr ? f.function : f.info;
        j["functionId"]           = function->stableId;
        std::unordered_set<std::string> parameters;
        const auto addParameter = [&](gc_node_ref_t ref) {
            if (function->capturePorts.count(ref) == 0) {
                if (const NodeInfo *node = function->node(ref))
                    parameters.insert(node->label);
            }
        };
        for (auto ref : function->graph->withPorts())
            addParameter(ref);
        for (auto ref : function->graph->normPorts())
            addParameter(ref);
        json arguments = json::array();
        for (const Variable &v : localsOf(chain)) {
            if (parameters.erase(v.name) == 0)
                continue;
            const Rendered value = v.evaluated ? render(v.view, 120) : Rendered{};
            arguments.push_back(
                json{
                    {"name", v.name},
                    {"value", v.evaluated ? value.value : v.note},
                    {"type", v.evaluated ? value.type : typeText(v.view.type)},
                    {"evaluated", v.evaluated}});
        }
        j["arguments"] = std::move(arguments);
    }
    if (f.node != nullptr) {
        j["node"] = json{
            {"ref", f.node->ref},
            {"kind", kindName(f.node->kind)},
            {"label", f.node->label},
            {"origin", f.node->origin}};
    }
    return j;
}

// ---------------------------------------------------------------------------------------
// Lines a step went past (4.2)
// ---------------------------------------------------------------------------------------

json DebugCore::skippedLines(const ShadowFrame &f, const Arrival &a) {
    json out = json::array();
    if ((step_.kind != StepKind::Line && step_.kind != StepKind::Out) || a.loc == nullptr ||
        !a.loc->valid()) {
        return out;
    }
    // Frames are told apart as the step tells them apart: by activation or by shadow frame.
    const auto key = [&](const ShadowFrame &frame) {
        return step_.folded ? frame.activation : frame.id;
    };
    const auto depth = [&](const ShadowFrame &frame) {
        return step_.folded ? frame.presentedDepth
                            : static_cast<size_t>(&frame - stack_.data()) + 1;
    };
    const bool startGone = std::none_of(stack_.begin(), stack_.end(), [&](const ShadowFrame &s) {
        return key(s) == step_.frame;
    });

    std::lock_guard lock(indexMutex_);
    const auto add = [&](uint64_t activation, int function, int from, int to) {
        for (DroppedLine &line : index_.droppedLines(function, from, to)) {
            if (!explainedLines_[activation].insert({line.file, line.line}).second) {
                continue; // said at an earlier stop of the same activation
            }
            std::string where = "line " + std::to_string(line.line);
            if (line.file != a.loc->file) {
                where +=
                    " of " + std::filesystem::path(index_.filePath(line.file)).filename().string();
            }
            json j{
                {"path", index_.filePath(line.file)},
                {"line", line.line},
                {"names", line.names},
                {"text",
                 where +
                     " was not executed: " + whyNotExecuted(LineState::NotDepended, line.names)}};
            out.push_back(std::move(j));
        }
    };
    const auto firstLineOf = [&](int function) {
        const SourceFunction *fn = index_.function(function);
        return fn != nullptr ? fn->line : 0;
    };
    const auto lastLineOf = [&](int function) {
        const SourceFunction *fn = index_.function(function);
        return fn != nullptr ? fn->endLine : 0;
    };
    const int here = a.loc->line;

    if (key(f) == step_.frame) {
        // Still in the activation the step started in: the lines between the two, when the
        // step went forward in the text.
        if (a.function == step_.function && a.loc->file == step_.file) {
            add(f.activation, a.function, step_.line + 1, here - 1);
        }
        return out;
    }
    if (startGone && step_.function >= 0) {
        // The step left the function it started in: the rest of it.
        add(step_.startActivation, step_.function, step_.line + 1, lastLineOf(step_.function));
    }
    if (step_.caller != 0 && key(f) == step_.caller) {
        // Back in the caller: the lines between the call and here.
        if (a.function == step_.callerLine.function && a.loc->file == step_.callerLine.file) {
            add(f.activation, a.function, step_.callerLine.line + 1, here - 1);
        }
    } else if (depth(f) >= step_.depth && a.function >= 0) {
        // An activation the step entered (a call, a tail call): its lines before this one.
        add(f.activation, a.function, firstLineOf(a.function), here - 1);
    }
    return out;
}

} // namespace debugger::core
