// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/type/base.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/graph.h"

#include <cstring>
#include <map>
#include <span>
#include <string>
#include <type_traits>

namespace camel::compile::gir {

using camel::runtime::GCNodeKind;
using camel::runtime::GraphId;
using camel::runtime::gc_cnt_t;
using camel::runtime::gc_node_ref_t;
using camel::runtime::gc_slot_idx_t;
using camel::runtime::kInvalidNodeRef;

/**
 * One graph, as the readers of it ask: this is the face `liftGraph` will use the day the lift
 * reads a build-time draft instead of a frozen graph.
 *
 * The lift (`modules/transforms/jvp.cpp`, `liftGraph`) reads a graph through `GCGraph` and the
 * `GCNode` records it hands out: a node's kind, its type, its ports, the graph its FUNC node calls,
 * the operator of its OPER node, and -- from beside the graph -- the graph a graph id names. A
 * draft has all of that in a different shape (a node header in a pool, ports in a node's tail,
 * callees by index into the graph's payload table), so a lift written against `GCGraph`
 * cannot be pointed at a draft without a face both answer. This is that face: every method is one
 * the lift already calls, with the signature it already calls it by, so moving the lift onto drafts
 * is a change of the type it holds and not a rewrite of what it asks.
 *
 * What it is *not*: a second model of a graph, or a place to decide anything. It answers only what
 * a frozen graph answers, and `firstDifference` proves the two answers agree (see below). A reader
 * that wants something a frozen graph does not answer is asking for a new graph fact, and that
 * belongs in the model first.
 *
 * Two facts the frozen side answers from the graph and a draft answers differently, both kept
 * here because the lift reads them:
 *  - a FUNC node's callee: an id in both, but the frozen graph keeps it in its reference table
 *    (`GCFuncRef` -> `refs_`) and a draft in its payload table, so the face resolves it here;
 *  - an OPER node's operator: both keep `GCOperBody` in the node's payload, so the face reads the
 *    payload off the draft's tail and answers the same URI.
 */
/// A node as the lift reads it: the record a frozen graph hands out (`GCNode`), answered for both
/// faces from one place so a reader that only wants these five facts does not walk two APIs.
struct NodeFace {
    GCNodeKind kind           = GCNodeKind::Data;
    gc_slot_idx_t dataIndex   = 0;
    const camel::core::type::Type *type = nullptr;
    uint8_t flags             = 0;
    /// The node has a payload (`payloadOf` answers its bytes).
    bool payload              = false;
    /// False when the id names no node of this graph: a removed node, or one it never had.
    bool alive                = false;

    constexpr bool hasFlag(uint8_t mask) const { return (flags & mask) != 0; }
};

class GraphRead {
  public:
    virtual ~GraphRead() = default;

    /// The node's record, as the lift asks for it.
    virtual NodeFace nodeFaceOf(gc_node_ref_t ref) const = 0;

    /// What the graph's type says (its parameters and what it answers); nullptr before it is
    /// complete, as on the frozen side.
    virtual camel::core::type::FunctionType *funcType() const = 0;

    /// Node ids are `0 .. nodeSlotCount() - 1`, a removed node's id kept (and not alive) --
    /// `nodeCount()` on a frozen graph, `nodeSlotCount()` on a draft.
    virtual gc_cnt_t nodeSlotCount() const = 0;
    /// The nodes that are there: `liveNodeCount()` on a frozen graph, `nodeCount()` on a draft.
    virtual gc_cnt_t liveNodeCount() const = 0;
    virtual bool alive(gc_node_ref_t ref) const = 0;

    virtual GCNodeKind kindOf(gc_node_ref_t ref) const = 0;
    virtual gc_slot_idx_t dataIndexOf(gc_node_ref_t ref) const = 0;
    /// The type of the node's value; nullptr when it has none. Types are interned, so the two
    /// faces answer the same pointer for the same type.
    virtual const camel::core::type::Type *typeOf(gc_node_ref_t ref) const = 0;
    /// The node's flags (`kGCNodeFlagMacro`, `kGCNodeFlagConstant`, ...).
    virtual uint8_t flagsOf(gc_node_ref_t ref) const = 0;
    virtual bool hasPayload(gc_node_ref_t ref) const = 0;

    /// The node's ports, by role, in port order -- the three lists `liftGraph` walks. A vector
    /// rather than a span because a draft's ids are not the graph's ids: the draft face answers
    /// through the id table the encoder keeps (see the `kept` constructor below).
    virtual std::vector<gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const = 0;
    virtual std::vector<gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const = 0;
    virtual std::vector<gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const = 0;

    /// The graph a FUNC node calls, `kInvalidGraphId` for another kind of node or one that names
    /// no callee yet (the same answer as `GCGraph::calleeIdOf`).
    virtual GraphId calleeIdOf(gc_node_ref_t ref) const = 0;

    /// The node's payload bytes, empty when it has none: the tail of a draft's node, the payload
    /// area of a frozen one. A body is read out of them with `bodyOf`, which copies -- a draft
    /// keeps its payload at an arbitrary offset, so a body is never reinterpreted in place.
    virtual std::span<const std::byte> payloadOf(gc_node_ref_t ref) const = 0;

    /// The body of node `ref`, copied out of its payload; false when the node has none or the
    /// payload is too small for the body. This is the `nodeBodyAs` the lift calls on a frozen
    /// graph, answered for both faces from the bytes.
    template <typename Body> bool bodyOf(gc_node_ref_t ref, Body &out) const {
        static_assert(std::is_trivially_copyable_v<Body>, "A node body is trivially copyable.");
        const std::span<const std::byte> bytes = payloadOf(ref);
        if (bytes.size() < sizeof(Body)) {
            return false;
        }
        std::memcpy(&out, bytes.data(), sizeof(Body));
        return true;
    }

    /// The operator of an OPER node, empty for another kind of node.
    std::string operUriOf(gc_node_ref_t ref) const;
    /// How many arms a BRCH node has, 0 for another kind of node.
    gc_cnt_t armCountOf(gc_node_ref_t ref) const;
};

/// The face of a build-time draft: what a graph answers while it is being built.
class DraftGraphRead final : public GraphRead {
  public:
    explicit DraftGraphRead(const camel::runtime::GraphTransaction &txn) : txn_(txn) {}

    /// The face of a draft through the id table the encoder builds from it: `kept[i]` is the
    /// draft's node that the runtime graph's node `i` is built from. A draft an encoder has not
    /// seen yet answers its own ids instead, which is what a lift reading a draft it built itself
    /// wants; a draft read beside the graph it was encoded to must use this form, because encoding
    /// drops the front end's placeholders (`GCNodeKind::Dref`) and numbers what is left from zero.
    DraftGraphRead(
        const camel::runtime::GraphTransaction &txn,
        std::span<const gc_node_ref_t> kept)
        : txn_(txn), kept_(kept) {}

    camel::core::type::FunctionType *funcType() const override { return txn_.graph().funcType(); }
    gc_cnt_t nodeSlotCount() const override {
        return kept_.empty() ? static_cast<gc_cnt_t>(txn_.graph().nodeSlotCount())
                             : static_cast<gc_cnt_t>(kept_.size());
    }
    gc_cnt_t liveNodeCount() const override {
        return kept_.empty() ? txn_.graph().nodeCount() : static_cast<gc_cnt_t>(kept_.size());
    }
    bool alive(gc_node_ref_t ref) const override;
    GCNodeKind kindOf(gc_node_ref_t ref) const override;
    gc_slot_idx_t dataIndexOf(gc_node_ref_t ref) const override;
    const camel::core::type::Type *typeOf(gc_node_ref_t ref) const override;
    uint8_t flagsOf(gc_node_ref_t ref) const override;
    bool hasPayload(gc_node_ref_t ref) const override;
    std::vector<gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const override;
    std::vector<gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const override;
    std::vector<gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const override;
    GraphId calleeIdOf(gc_node_ref_t ref) const override;
    NodeFace nodeFaceOf(gc_node_ref_t ref) const override;
    std::span<const std::byte> payloadOf(gc_node_ref_t ref) const override;

  private:
    /// The draft's node the face's node `ref` is: `ref` itself when no id table was given, else
    /// `kept_[ref]`.
    gc_node_ref_t sourceIdOf(gc_node_ref_t ref) const {
        return kept_.empty() ? ref : (ref < kept_.size() ? kept_[ref] : kInvalidNodeRef);
    }
    /// One of the draft's ids as the graph's: where the encoder keeps it, or `kInvalidNodeRef`
    /// when it is a node the runtime graph does not have (a placeholder, or a node outside the
    /// reachable set). Only meaningful with an id table.
    gc_node_ref_t mappedIdOf(gc_node_ref_t id) const;
    std::vector<gc_node_ref_t> mappedListOf(std::span<const gc_node_ref_t> inputs) const;

    const camel::runtime::GraphTransaction &txn_;
    std::span<const gc_node_ref_t> kept_;
};

/// The face of a frozen graph: the one `liftGraph` reads today.
class FrozenGraphRead final : public GraphRead {
  public:
    explicit FrozenGraphRead(const camel::runtime::GCGraph &graph) : graph_(graph) {}

    camel::core::type::FunctionType *funcType() const override { return graph_.funcType(); }
    gc_cnt_t nodeSlotCount() const override { return graph_.nodeCount(); }
    gc_cnt_t liveNodeCount() const override { return graph_.liveNodeCount(); }
    bool alive(gc_node_ref_t ref) const override;
    GCNodeKind kindOf(gc_node_ref_t ref) const override;
    gc_slot_idx_t dataIndexOf(gc_node_ref_t ref) const override;
    const camel::core::type::Type *typeOf(gc_node_ref_t ref) const override;
    uint8_t flagsOf(gc_node_ref_t ref) const override;
    bool hasPayload(gc_node_ref_t ref) const override;
    std::vector<gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const override {
        return mappedListOf(graph_.normInputsOf(ref));
    }
    std::vector<gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const override {
        return mappedListOf(graph_.withInputsOf(ref));
    }
    std::vector<gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const override {
        return mappedListOf(graph_.ctrlInputsOf(ref));
    }
    GraphId calleeIdOf(gc_node_ref_t ref) const override { return graph_.calleeIdOf(ref); }
    NodeFace nodeFaceOf(gc_node_ref_t ref) const override;
    std::span<const std::byte> payloadOf(gc_node_ref_t ref) const override {
        return graph_.nodePayloadBytes(ref);
    }

  private:
    /// A frozen graph's ids are its own, so a list is copied and nothing is translated.
    static std::vector<gc_node_ref_t> mappedListOf(std::span<const gc_node_ref_t> inputs) {
        return std::vector<gc_node_ref_t>(inputs.begin(), inputs.end());
    }

    const camel::runtime::GCGraph &graph_;
};

/**
 * The first fact two faces of the same graph disagree on, as text; empty when they agree on
 * everything this face answers.
 *
 * This is the check that the face is a face and not a second reading: a draft and the frozen graph
 * built from it are the same graph, so every question above must have the same answer twice. The
 * encoder asks it of every graph of every program and refuses a graph whose two readings disagree
 * (src/compile/gir/graph_encode.cpp), so the claim is asked wherever a program is compiled and not
 * only where a test looks -- a program that compiles is a program the two answers agreed on.
 */
std::string firstDifference(const GraphRead &draft, const GraphRead &frozen);

/// The drafts a build answers, by graph id: the entry `Program::graph(id)` gives on the frozen
/// side, so a lift that walks from a graph to the graphs it calls walks the same way on a draft.
class DraftGraphs {
  public:
    void add(const camel::runtime::GraphTransaction &txn);
    const GraphRead *byId(GraphId id) const;

  private:
    std::map<GraphId, DraftGraphRead> byId_;
};

/// One line about a graph, for a reading: how many nodes, which kind each is, and what its FUNC
/// and OPER nodes name. The sequence is per node, in node id order, so two graphs are the same
/// line exactly when they are the same graph as far as this face can tell.
std::string describe(const GraphRead &graph);


} // namespace camel::compile::gir