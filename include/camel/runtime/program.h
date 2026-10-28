// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The program snapshot (docs/design/graph-store, 4.2): a table from GraphId to the current
 * version of that graph. The pipeline hands a Program from pass to pass, a Context holds the
 * one that is installed, and a function value holds the Program it was made in together with
 * the GraphId of its graph and finds the graph through the table.
 *
 * A Program is built once, while the encoder or a commit is still producing the graphs it
 * holds (bind, setRoot), and is not changed after it is published; a rewrite makes a new one.
 * A Program is an object of the graph space, like the graphs: it traces the graphs of its
 * table, a function value traces the Program it was made in, and what no root, no function
 * value and no running call reaches is freed (graph.h, GCGraphManager). A graph version lives
 * as long as some Program holds it. Graphs a transform creates while the program runs (the
 * gradient of a function) form a Program of their own on top of the one they were made over.
 */

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_id.h"
#include "camel/runtime/specializations.h"

#include <span>
#include <vector>

namespace camel::core::context {
class Context;
}

namespace camel::runtime {

class GCGraphManager;

class Program final : public camel::core::rtdata::Object {
  public:
    Program() = default;
    /// A copy holds the same graphs; it is the manager's only when the caller reserved it (a
    /// session's work table is a Program of the context's, so that what it binds is freed).
    Program(const Program &other)
        : camel::core::rtdata::Object(), byId_(other.byId_), own_(other.own_),
          satellite_(other.satellite_), version_(other.version_), count_(other.count_),
          root_(other.root_), specializations_(other.specializations_) {}
    /// Assigning replaces the table and keeps whose program this is.
    Program &operator=(const Program &other) {
        byId_            = other.byId_;
        own_             = other.own_;
        satellite_       = other.satellite_;
        version_         = other.version_ + 1;
        count_           = other.count_;
        root_            = other.root_;
        specializations_ = other.specializations_;
        if (manager_ != nullptr) {
            noteAll();
        }
        return *this;
    }

    // Object: the collector traces the graphs of the table and the program this one copies.
    bool equals(
        const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
        bool deep = false) const override {
        (void)type;
        (void)deep;
        return other == this;
    }
    camel::core::rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(
        const camel::core::rtdata::Object::RefRelocator &relocate,
        const camel::core::type::Type *type) override;

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
    /// The graphs a declaration tag of `graph`'s declaration names (a derivative rule).
    std::vector<GCGraph *> ruleGraphs(const GCGraph &graph) const {
        return resolve(graph.ruleGraphs());
    }
    /// Calls `visitor` for every graph of the reference table of `graph`. The semantic body of an
    /// `#impl` function is not part of the ordinary program (design principles 10.4): a nested
    /// entry that leads to one is skipped, so an ordinary pass or a dump does not reach the
    /// body. A graph that takes the function's value still reaches it (a static or callee
    /// entry), and Program::implBodies lists the bodies for a consumer that expands on demand.
    template <typename Visitor> void traceGraphs(const GCGraph &graph, Visitor &&visitor) const {
        const auto refs = graph.refs();
        for (size_t i = 0; i < refs.size(); ++i) {
            if ((graph.refRoles(i) & camel::runtime::kGraphRefNested) != 0) {
                const GCGraph *target = this->graph(refs[i]);
                if (target != nullptr && target->isImplBody()) {
                    continue;
                }
            }
            if (GCGraph *found = this->graph(refs[i])) {
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
    /// The semantic bodies of the program's `#impl` functions (GCGraph::isImplBody): the graphs
    /// an expansion reads, in the order of their ids. An ordinary call of such a function is a
    /// call of its kernel operator, so these graphs are usually not reachable from the root and
    /// a dump of the root does not show them (design principles 10.4).
    std::vector<GCGraph *> implBodies() const {
        std::vector<GCGraph *> bodies;
        for (GCGraph *graph : byId_) {
            if (graph != nullptr && graph->isImplBody()) {
                bodies.push_back(graph);
            }
        }
        return bodies;
    }
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
    /// Construction; see the file comment.
    void bind(GraphId id, GCGraph *graph) {
        ++version_;
        if (manager_ != nullptr && graph != nullptr) {
            note(graph);
        }
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
    /// Gives up a construction that failed half way (the encoder ran out of memory): the program
    /// holds no graph afterwards, and each graph in `unconstructed` -- memory bound but never
    /// built into a graph -- is forgotten by the manager, so that nothing destructs or traces it.
    void abandonConstruction(std::span<GCGraph *const> unconstructed);

    /// The specializations made over the graphs of this program (specializations.h): copied
    /// with the program, so a rewrite that carries a program forward carries what it already
    /// made.
    const RuntimeSpecializationTable &specializations() const { return specializations_; }
    RuntimeSpecializationTable &specializations() { return specializations_; }

  private:
    friend class GCGraphManager;
    void note(GCGraph *graph);
    void noteAll();
    GCGraphManager *manager_ = nullptr; // set for a program the manager owns
    std::vector<GCGraph *> byId_; // indexed by the value of the GraphId; nullptr where absent
    std::vector<uint8_t> own_;    // satellite: 1 where the graph is one it adds
    bool satellite_          = false;
    uint64_t version_        = 0;
    size_t count_            = 0;
    GCGraph *root_           = nullptr;
    RuntimeSpecializationTable specializations_;
};

/// Makes the function values among the static values of `graph` name `to` where `to` holds the
/// version of their graph that they find today. A graph a commit left as it was keeps its static
/// values; they would otherwise name the program before, and through it every version that
/// program holds.
void retargetFunctions(GCGraph *graph, const Program &to);

/// A program of its own, owned by `context`, for graphs created while the program runs and
/// belonging to no commit (the gradient of a function, an outlined region): the graphs of
/// `base` and `graphs`, the first of them its root. The graphs are rooted for the context's
/// lifetime.
const Program *makeProgram(
    camel::core::context::Context &context, const Program *base, std::span<GCGraph *const> graphs);

} // namespace camel::runtime
