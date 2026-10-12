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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

/**
 * @file program_index.h
 * @brief What the debugger knows about one compiled program, by source line and by graph.
 *
 * Two directions (docs/technical/34_debugger.md, sections 2, 3 and 6):
 *  - from a source line to the nodes whose origin starts on it, in three states of the program
 *    (as built, before the passes, as it runs), which is what verifies a line breakpoint or
 *    says why it cannot be;
 *  - from a runtime graph to its nodes with their source locations, the function of the
 *    source each was written in, and the names bound to them.
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
    std::string name;
};

/// A name of the source and where its value lives (SemanticRecorder::valueBound).
struct Binding {
    std::string name;
    origin_t decl = 0;
    origin_t node = 0; // 0: a parameter or captured name (a port)
    camel::core::type::Type *type = nullptr;
    bool variable = false;
    int function  = -1; // the source function the declaration is in
    int line      = 0;  // of the declaration
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

struct GraphInfo {
    camel::runtime::GCGraph *graph = nullptr;
    std::string name;
    /// The name shown to the user: `name`, or for a graph the compiler named itself what it
    /// is, e.g. `fact [arm at line 4]`, `main [lambda at line 14]`.
    std::string display;
    bool arm = false; // the graph is an arm of a branch
    std::string stableId;
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
};

/// Why a source line can or cannot hold a breakpoint.
enum class LineState {
    Executable,  // nodes of the running graph start on it
    NoCode,      // nothing was built from this line
    NotDepended, // built, but not in the graph: nothing depends on its result
    Removed,     // in the graph before the passes, not in the one that runs
    ConstantOnly, // only constants: nothing to execute
    UnknownFile, // the file is not part of the program
};

struct LineResolution {
    LineState state = LineState::NoCode;
    std::vector<origin_t> origins; // of executing nodes, when Executable
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

    LineResolution resolveLine(const std::string &path, int line);

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

    void collect(camel::runtime::GCGraph *root, std::map<LineKey, LineNodes> &into,
                 std::vector<camel::runtime::GCGraph *> *graphs);
    void finishBindings();

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
    std::set<LineKey> builtLines_;
    std::map<LineKey, LineNodes> initialLines_;
    std::map<LineKey, LineNodes> runningLines_;
    std::vector<camel::runtime::GCGraph *> runningGraphs_;
    std::unordered_set<camel::runtime::GCGraph *> armGraphs_; // of the running program
    /// For a compiler-made graph: the names of what its callers pass, by port position.
    std::unordered_map<camel::runtime::GCGraph *, std::unordered_map<size_t, std::string>>
        inheritedPortNames_;
    bool compiledKnown_ = false;
    bool runningKnown_  = false;
};

/// "DATA", "OPER", ... as the graph dumps name node kinds.
const char *kindName(camel::runtime::GCNodeKind kind);

} // namespace debugger::core
