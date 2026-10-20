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
    /// The program this one is a private copy of, nullptr when it is not one. A back end that
    /// runs a rewritten copy of the program (lowerLoopsToTailRecursion, which commits its
    /// session detached) gets a program whose original is the program it rewrote, followed
    /// back to the first program that is not a copy. Operators that read the graphs of
    /// function values (OperatorTraits::inspectsGraphs) are handed the original, so that they
    /// see the program as it was written (op-semantics 11.4).
    const Program *original() const { return original_; }

    /// Construction; see the file comment.
    void bind(GraphId id, GCGraph *graph) {
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
    size_t count_            = 0;
    GCGraph *root_           = nullptr;
    const Program *original_ = nullptr;
};

/// A program of its own, owned by `context`, for graphs created while the program runs and
/// belonging to no commit (the gradient of a function, an outlined region): it holds exactly
/// `graphs`, the first being its root.
const Program *
makeProgram(camel::core::context::Context &context, std::span<GCGraph *const> graphs);

} // namespace camel::runtime
