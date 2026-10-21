/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The program snapshot (docs/design/graph-store, 4.2): a table from GraphId to the current
 * version of that graph. The pipeline hands a Program from pass to pass, a Context holds the
 * one that is installed, and a function value holds the Program it was made in together with
 * the GraphId of its graph and finds the graph through the table.
 *
 * A Program is built once, while the encoder or a commit is still producing the graphs it
 * holds (bind, setRoot), and is not changed after it is published; a rewrite makes a new one.
 * Programs are owned by the GCGraphManager of the context and live as long as it does; the
 * graphs a Program names are freed when a later Program replaces the one they belong to
 * (graph.h, GCGraphManager). Graphs a transform creates while the program runs (the gradient
 * of a function) form a Program of their own, holding just those graphs.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "camel/runtime/graph_id.h"

#include <span>
#include <vector>

namespace camel::core::context {
class Context;
}

namespace camel::runtime {

class Program {
  public:
    /// The version of graph `id` this program holds; nullptr when it holds none.
    GCGraph *graph(GraphId id) const {
        const size_t index = toValue(id);
        return index < byId_.size() ? byId_[index] : nullptr;
    }
    /// The graphs a graph refers to, read in this program (graph.h, GCGraph::refs): the graph
    /// of FUNC node `ref`; of LOOP node `ref` the condition (nullptr for a For loop) and the
    /// body; the graph a graph is written in. nullptr where the id names no graph of the
    /// program.
    GCGraph *calleeOf(const GCGraph &graph, gc_node_ref_t ref) const {
        return this->graph(graph.calleeIdOf(ref));
    }
    GCGraph *loopCondOf(const GCGraph &graph, gc_node_ref_t ref) const {
        return this->graph(graph.loopCondIdOf(ref));
    }
    GCGraph *loopBodyOf(const GCGraph &graph, gc_node_ref_t ref) const {
        return this->graph(graph.loopBodyIdOf(ref));
    }
    GCGraph *outerOf(const GCGraph &graph) const { return this->graph(graph.outerGraphId()); }
    /// The graphs `ids` name that the program holds, in order.
    std::vector<GCGraph *> resolve(std::span<const GraphId> ids) const {
        std::vector<GCGraph *> graphs;
        graphs.reserve(ids.size());
        for (GraphId id : ids) {
            if (GCGraph *found = this->graph(id)) {
                graphs.push_back(found);
            }
        }
        return graphs;
    }
    /// Views of the reference table of `graph` by role (GCGraphRefRole), resolved.
    std::vector<GCGraph *> dependencies(const GCGraph &graph) const {
        return resolve(graph.dependencies());
    }
    std::vector<GCGraph *> subGraphs(const GCGraph &graph) const {
        return resolve(graph.subGraphs());
    }
    std::vector<GCGraph *> staticGraphRefs(const GCGraph &graph) const {
        return resolve(graph.staticGraphRefs());
    }
    /// Calls `visitor` for every graph of the reference table of `graph`.
    template <typename Visitor> void traceGraphs(const GCGraph &graph, Visitor &&visitor) const {
        for (GraphId id : graph.refs()) {
            if (GCGraph *found = this->graph(id)) {
                visitor(found);
            }
        }
    }
    /// True when `graph` is the version this program holds of its graph.
    bool contains(const GCGraph *graph) const {
        return graph != nullptr && this->graph(graph->id()) == graph;
    }
    /// The entry of the program: the graph the pipeline starts from. A program made by a
    /// rewrite of a function that is not the program's entry (a function being exported) is
    /// rooted at that function's new version.
    GCGraph *root() const { return root_; }
    /// Every graph of the program, in the order of their ids.
    std::vector<GCGraph *> graphs() const {
        std::vector<GCGraph *> graphs;
        graphs.reserve(count_);
        for (GCGraph *graph : byId_) {
            if (graph != nullptr) {
                graphs.push_back(graph);
            }
        }
        return graphs;
    }
    size_t size() const { return count_; }
    /// One more than the largest id the table has room for.
    size_t tableSize() const { return byId_.size(); }
    /// Advances with every bind: equal versions of one program mean the same table.
    uint64_t version() const { return version_; }
    /// A satellite is a program made outside a commit (makeProgram, a draft group): the graphs
    /// of the program it was made over, so that every id resolves, and the ones it adds. Only
    /// those it adds are its own; the others are the versions the base had then, which a later
    /// commit may have replaced and freed. A program that is not a satellite owns all it holds.
    bool owns(GraphId id) const {
        if (!satellite_) {
            return graph(id) != nullptr;
        }
        const size_t index = toValue(id);
        return index < own_.size() && own_[index] != 0;
    }
    /// Binds `graph` as one the satellite adds.
    void bindOwn(GraphId id, GCGraph *graph) {
        satellite_ = true;
        bind(id, graph);
        const size_t index = toValue(id);
        if (index >= own_.size()) {
            own_.resize(index + 1, 0);
        }
        own_[index] = 1;
    }
    /// Makes the program a satellite of what it holds.
    void makeSatellite() { satellite_ = true; }
    /// Binds the graphs `other` owns that this program does not hold.
    void adoptMissing(const Program &other) {
        if (&other == this) {
            return;
        }
        for (size_t index = 0; index < other.byId_.size(); ++index) {
            const GraphId id = static_cast<GraphId>(index);
            if (other.byId_[index] != nullptr && other.owns(id) && graph(id) == nullptr) {
                bind(id, other.byId_[index]);
            }
        }
    }
    /// The program this one is a private copy of, nullptr when it is not one. A back end that
    /// runs a rewritten copy of the program (lowerLoopsToTailRecursion, which commits its
    /// session detached) gets a program whose original is the program it rewrote, followed
    /// back to the first program that is not a copy. Operators that read the graphs of
    /// function values (OperatorTraits::inspectsGraphs) are handed the original, so that they
    /// see the program as it was written (op-semantics 11.4).
    const Program *original() const { return original_; }

    /// Construction; see the file comment.
    void bind(GraphId id, GCGraph *graph) {
        ++version_;
        const size_t index = toValue(id);
        if (index >= byId_.size()) {
            byId_.resize(index + 1, nullptr);
        }
        if (byId_[index] == nullptr && graph != nullptr) {
            ++count_;
        } else if (byId_[index] != nullptr && graph == nullptr) {
            --count_;
        }
        byId_[index] = graph;
    }
    void setRoot(GCGraph *root) { root_ = root; }
    void setOriginal(const Program *original) { original_ = original; }

  private:
    std::vector<GCGraph *> byId_; // indexed by the value of the GraphId; nullptr where absent
    std::vector<uint8_t> own_;    // satellite: 1 where the graph is one it adds
    bool satellite_          = false;
    uint64_t version_        = 0;
    size_t count_            = 0;
    GCGraph *root_           = nullptr;
    const Program *original_ = nullptr;
};

/// A program of its own, owned by `context`, for graphs created while the program runs and
/// belonging to no commit (the gradient of a function, an outlined region): the graphs of
/// `base` and `graphs`, the first of them its root. The graphs are rooted for the context's
/// lifetime.
const Program *makeProgram(
    camel::core::context::Context &context, const Program *base, std::span<GCGraph *const> graphs);

} // namespace camel::runtime
