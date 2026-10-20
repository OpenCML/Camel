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
 * Created: Oct. 12, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

/**
 * @file program_index.h
 * @brief What the debugger knows about one compiled program, by source line and by graph.
 *
 * Two directions (docs/technical/35_debugger.md, sections 2, 3 and 6):
 *  - from a source line to the nodes whose origin starts on it, in three states of the program
 *    (as built, before the passes, as it runs), which is what verifies a line breakpoint or
 *    says why it cannot be, and what explains a line a step went past (4.2);
 *  - from a runtime graph to its nodes with their source locations, the function of the
 *    source each was written in, the names bound to them, and what the graph is to the user:
 *    a function, a lambda, or a subgraph of a function (the arm of a branch) that the stack
 *    folds into the function's frame (5.4).
 *
 * Names are full names (design principles 10.2): `module::path::name`, without the module for
 * the entry module's own, as the graph builder names graphs.
 *
 * Names come from the builders through the recorder of the semantic query layer
 * (camel/compile/semantic_recorder.h): attach recorder() to the context before compiling.
 *
 * Not thread-safe: the DebugCore that owns it serialises access.
 */

#include "camel/compile/semantic_recorder.h"
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace debugger::core {

using origin_t = camel::source::origin_id_t;

/// A source range, 1-based lines and columns (columns count code points). file < 0: none.
struct SourceLoc {
    int file         = -1;
    int line         = 0;
    int column       = 0;
    int endLine      = 0;
    int endColumn    = 0;
    size_t startByte = 0;
    size_t endByte   = 0;

    bool valid() const { return file >= 0; }
};

/// A function of the source: the span of its declaration.
struct SourceFunction {
    int file = -1;
    size_t startByte = 0;
    size_t endByte   = 0;
    int line         = 0; // first and last line of the declaration
    int endLine      = 0;
    /// The full name (`module::path::name`; a generic function without its type arguments).
    std::string name;
    /// A graph of the function is called in the program before the passes, or is a function
    /// value of it (see ProgramIndex::liveGraphs). False: nothing in the program calls it.
    bool reachable = false;
};

/// A name of the source and where its value lives (SemanticRecorder::valueBound).
struct Binding {
    std::string name;
    origin_t decl = 0;
    origin_t node = 0; // 0: a parameter or captured name (a port)
    camel::core::type::Type *type = nullptr;
    bool variable = false;
    int function  = -1; // the source function the declaration is in
    int file      = -1; // of the declaration
    int line      = 0;
    /// The stable id of the graph the builder made the node in: the function's graph, or the
    /// subgraph of the arm the declaration is written in.
    std::string graph;
};

struct NodeInfo {
    camel::runtime::gc_node_ref_t ref = camel::runtime::kInvalidNodeRef;
    camel::runtime::GCNodeKind kind   = camel::runtime::GCNodeKind::Data;
    int slot                          = 0;
    origin_t origin                   = 0;
    SourceLoc loc;
    int function = -1; // source function the node was written in
    /// The scheduler does work for the node (constants, ports and gates are not executed).
    bool executes = false;
    std::string label;
};

/// What a graph is to the user (docs/technical/35_debugger.md, 5.4).
enum class GraphRole {
    Function, // a function of the source (or the entry graph)
    Lambda,   // a function value written in another function: a frame of its own
    /// The arm of a branch (`if`, `match`, `&&`, `||`, `??`, `!!`, `?->`, narrowing): code of
    /// the function that branches, which a call makes a frame of its own. Folded into the
    /// function's frame unless the user asks for the subgraphs. The condition and the body of
    /// a `LOOP` will be subgraphs of this kind too (design principles 3.10): they belong
    /// to the function the loop is written in, and fold the same way.
    Arm,
};

struct GraphInfo {
    camel::runtime::GCGraph *graph = nullptr;
    /// The graph's name as the builder gave it: the full name of a function.
    std::string name;
    /// The name shown to the user: `name`, or for a graph the compiler named itself what it
    /// is, e.g. `fact [arm at line 4]`, `main [lambda at line 14]`.
    std::string display;
    GraphRole role = GraphRole::Function;
    /// For a subgraph that folds (GraphRole::Arm): the graphs whose branch nodes call it, so
    /// the graphs it is part of when a frame of one of them calls it.
    std::vector<camel::runtime::GCGraph *> owners;
    std::string stableId;
    /// The ports that hold captured values (closure nodes, or `with` ports after the declared
    /// ones of a function called directly), named by the full names of those values.
    std::unordered_set<camel::runtime::gc_node_ref_t> capturePorts;
    int function = -1; // the source function the graph was built from
    std::vector<NodeInfo> nodes; // in the order of the node blob
    std::unordered_map<camel::runtime::gc_node_ref_t, uint32_t> byRef;
    std::unordered_multimap<origin_t, uint32_t> byOrigin;
    std::unordered_map<int, uint32_t> bySlot;

    const NodeInfo *node(camel::runtime::gc_node_ref_t ref) const {
        auto it = byRef.find(ref);
        return it == byRef.end() ? nullptr : &nodes[it->second];
    }
    /// The node of a bytecode instruction: by the origin recorded for its pc and the slot it
    /// writes; null for an instruction that is no node (a jump).
    const NodeInfo *nodeOfInstruction(origin_t origin, int resultSlot) const;

    /// A frame of this graph is part of the frame of the function that branches into it.
    bool folds() const { return role == GraphRole::Arm; }
};

/// Why a source line can or cannot hold a breakpoint.
enum class LineState {
    Executable,  // nodes of the running graph start on it
    NoCode,      // nothing was built from this line
    NotDepended, // built, but not in the graph: nothing depends on its result
    NotCalled,   // built, in a function nothing in the program calls
    Removed,     // in the graph before the passes, not in the one that runs
    ConstantOnly, // only constants: nothing to execute
    UnknownFile, // the file is not part of the program
};

struct LineResolution {
    LineState state = LineState::NoCode;
    std::vector<origin_t> origins; // of executing nodes, when Executable
    int line = 0;                  // the line asked for
    /// NotDepended: the names declared on the line whose values nothing uses (may be empty:
    /// a call written as a statement). NotCalled, and Executable when `called` is false: the
    /// full name of the function.
    std::vector<std::string> names;
    /// Executable: some node of the line is in a graph the program calls. A function nothing
    /// calls is still in the program (every function of a module is a subgraph of its root),
    /// so its lines hold breakpoints, which are never hit.
    bool called = true;
};

/// A line whose code the graph builder left out of the graph because nothing uses its result
/// (design principles 3.1): what a line step that goes past it says (35_debugger.md, 4.2).
struct DroppedLine {
    int file     = -1;
    int line     = 0;
    int function = -1; // the source function it is written in
    std::vector<std::string> names; // as in LineResolution
};

/// How a function name given by the user (a function breakpoint) was resolved.
struct FunctionResolution {
    bool found = false;
    /// Stable ids of the graphs of the function (one per instance of a generic function).
    std::vector<std::string> graphs;
    std::string fullName; // when found
    std::string message;  // why not, when not found
};

class ProgramIndex {
  public:
    ProgramIndex();
    ~ProgramIndex();

    /// Forgets everything and starts recording for a compilation in `ctx`.
    void begin(const camel::core::context::context_ptr_t &ctx);
    camel::compile::SemanticRecorder *recorder();
    /// After the compilation: reads what the builders created; `root` is the graph before any
    /// pass (may be null when the program did not compile).
    void compiled(camel::runtime::GCGraph *root);
    /// When the scheduler starts: `root` is the graph it was given.
    void running(camel::runtime::GCGraph *root);
    bool hasRunningGraph() const { return runningKnown_; }
    bool isCompiled() const { return compiledKnown_; }

    const SourceLoc &loc(origin_t origin);
    const std::string &filePath(int file) const;
    /// The file of the program with this path (compared after normalisation), or -1.
    int fileOf(const std::string &path);
    /// The innermost function of the source that strictly contains `loc`, or -1.
    int functionOf(const SourceLoc &loc) const;
    const SourceFunction *function(int index) const {
        return index >= 0 && static_cast<size_t>(index) < functions_.size() ? &functions_[index]
                                                                            : nullptr;
    }

    GraphInfo &graph(camel::runtime::GCGraph *graph);
    /// Every graph reachable from the running root (empty before running()).
    const std::vector<camel::runtime::GCGraph *> &runningGraphs() const { return runningGraphs_; }
    /// A graph with this stable id is part of the program that runs.
    bool runs(const std::string &stableId) const { return runningStableIds_.count(stableId) != 0; }
    /// A node with this origin is in the program before the passes.
    bool inInitialGraph(origin_t origin) const { return initialOrigins_.count(origin) != 0; }
    /// A graph with this stable id is an arm of a branch of the program that runs.
    bool isArm(const std::string &stableId) const { return armStableIds_.count(stableId) != 0; }

    LineResolution resolveLine(const std::string &path, int line);
    /// The lines of `function` from `firstLine` to `lastLine` (inclusive) that the builder left
    /// out because nothing uses their result, in the order of the text.
    std::vector<DroppedLine> droppedLines(int function, int firstLine, int lastLine) const;
    /// A function by the name a user writes: the full name `module::path::name`, or without
    /// the module when only one module has it; a generic function by its name (every
    /// instance) or one instance (`first<int>`). Against the graph that runs once it is known.
    /// A function nothing calls is found, with a message that says so.
    FunctionResolution resolveFunction(const std::string &name) const;

    /// The full name of a value `graph` captures, from the name the capturing module writes
    /// (GCGraph::captureNames): with the module, unless it is the entry module's own.
    std::string capturedName(camel::runtime::GCGraph *graph, std::string_view shown);

    /// The bindings whose value is the node with this origin.
    std::vector<const Binding *> bindingsOfNode(origin_t node) const;
    /// The bindings declared in a source function.
    std::vector<const Binding *> bindingsOfFunction(int function) const;

    const camel::core::context::context_ptr_t &context() const { return ctx_; }

  private:
    class Recorder;
    struct LineKey {
        int file;
        int line;
        bool operator<(const LineKey &o) const {
            return file != o.file ? file < o.file : line < o.line;
        }
    };
    struct LineNodes {
        std::set<origin_t> all;
        std::set<origin_t> executing;
    };

    using ArmOwners =
        std::unordered_map<camel::runtime::GCGraph *, std::vector<camel::runtime::GCGraph *>>;
    /// Walks the graphs reachable from `root`: the located nodes by line, the graphs, the
    /// origins of all nodes (when asked) and the arms of the branches (when asked).
    void collect(
        camel::runtime::GCGraph *root, std::map<LineKey, LineNodes> &lines,
        std::vector<camel::runtime::GCGraph *> &graphs, std::unordered_set<origin_t> *origins,
        ArmOwners *arms);
    void finishBindings();
    /// Full names and reachability of the functions, from the graphs before the passes.
    void nameFunctions();
    /// The graphs the program can run: from `root`, the graphs it calls (FUNC nodes) and the
    /// graphs of the function values it holds (static references), and so on. A function
    /// nothing calls is a subgraph of the root but not among them.
    void findLiveGraphs(camel::runtime::GCGraph *root);
    void findModules();
    /// The module a file of the program belongs to; "" for the entry module (whose names are
    /// shown without it) and for a file of no user module.
    const std::string &moduleOfFile(int file);
    /// The names declared on a line whose values are in no graph of the program before the
    /// passes (nothing uses them).
    std::vector<std::string> unusedNamesOn(int file, int line, int function) const;

    camel::core::context::context_ptr_t ctx_;
    std::unique_ptr<Recorder> recorder_;
    std::vector<std::string> files_;
    std::unordered_map<uint32_t, int> fileBySourceId_;
    std::unordered_map<std::string, int> fileByPath_;
    std::unordered_map<origin_t, SourceLoc> locs_;
    std::vector<SourceFunction> functions_;
    std::vector<Binding> bindings_;
    std::unordered_multimap<origin_t, uint32_t> bindingByNode_;
    std::unordered_multimap<int, uint32_t> bindingByFunction_;
    std::unordered_map<camel::runtime::GCGraph *, std::unique_ptr<GraphInfo>> graphs_;
    std::map<LineKey, std::vector<origin_t>> builtLines_; // origins the builder created
    std::map<LineKey, LineNodes> initialLines_;
    std::map<LineKey, LineNodes> runningLines_;
    std::unordered_set<origin_t> initialOrigins_; // of the nodes of the graph before the passes
    /// Of the program before the passes, then of the one that runs (findLiveGraphs): the stable
    /// ids of the graphs it can run, and the origins of their nodes.
    std::unordered_set<std::string> liveStableIds_;
    std::unordered_set<origin_t> liveOrigins_;
    std::vector<camel::runtime::GCGraph *> initialGraphs_;
    std::vector<camel::runtime::GCGraph *> runningGraphs_;
    std::unordered_set<std::string> runningStableIds_;
    std::unordered_set<std::string> armStableIds_;
    /// The arms of the branches of the running program, with the graphs that branch to them.
    ArmOwners armOwners_;
    /// The module of each file of the program (by file index); "" for the entry module.
    std::unordered_map<int, std::string> moduleByFile_;
    std::vector<std::pair<std::string, std::string>> modulePaths_; // name, normalised path
    std::string entryPath_;                                         // normalised
    std::unordered_set<std::string> moduleNames_; // the user modules other than the entry one
    std::unordered_set<std::string> topLevelNames_; // full names of top-level values
    /// For a compiler-made graph: the names of what its callers pass, by port position.
    std::unordered_map<camel::runtime::GCGraph *, std::unordered_map<size_t, std::string>>
        inheritedPortNames_;
    bool compiledKnown_ = false;
    bool runningKnown_  = false;
};

/// "DATA", "OPER", ... as the graph dumps name node kinds.
const char *kindName(camel::runtime::GCNodeKind kind);

/// Why the code of a line in state NotDepended or NotCalled is not executed, in words, from
/// what LineResolution::names holds: "nothing uses the value of `y`", "nothing uses its
/// result", "nothing calls `f`". Only what the graph shows: no other state has a reason here.
std::string whyNotExecuted(LineState state, const std::vector<std::string> &names);

} // namespace debugger::core
