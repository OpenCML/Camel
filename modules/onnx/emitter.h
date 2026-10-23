// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * ONNX graph construction for the exporter.
 *
 * The emitter owns the model graph under construction: it hands out unique
 * value names, appends nodes to the current scope (the main graph or an If
 * branch body), and turns exporter values into node operands. Constants are
 * materialized once as initializers of the main graph (visible from nested
 * scopes); symbolic operands of the wrong dtype get a Cast memoized per scope.
 *
 * Also hosts the value helpers shared by the evaluator and the lowerings:
 * static facts, inference types, and constant-argument views of values.
 */

#pragma once

#include "../tensor/ops/op_def.h"
#include "proto/onnx_writer.h"
#include "value.h"

#include "camel/core/mm/root_handle.h"

#include <array>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::onnx {

/// A model the exporter cannot express; reported to the user with its message.
class ExportError : public std::runtime_error {
  public:
    /// What stopped the export, where a caller needs to tell cases apart: a call that was not
    /// inlined is something an earlier pass should have removed (see passes/export.h).
    enum class Kind { Other, CallRemains };

    using std::runtime_error::runtime_error;
    ExportError(const std::string &what, Kind kind) : std::runtime_error(what), kind_(kind) {}
    ExportError(
        const std::string &what, std::string location, uint64_t origin = 0,
        Kind kind = Kind::Other)
        : std::runtime_error(what), location_(std::move(location)), origin_(origin), kind_(kind) {}

    /// Source position of the construct the export failed on ("file:line:col"), or empty.
    const std::string &location() const { return location_; }
    /// Source origin id of that construct (0 when unknown), for diagnostics.
    uint64_t origin() const { return origin_; }
    Kind kind() const { return kind_; }

  private:
    std::string location_;
    uint64_t origin_ = 0;
    Kind kind_       = Kind::Other;
};

/// ONNX element type of a Camel tensor storage dtype.
ElemType elemTypeOf(type::TypeCode dtype);

/// Tensor facts of a value: symbolic facts, a constant tensor's exact facts, a scalar's rank 0,
/// an int array's length.
tensor::ops::TensorFacts factsOf(const Value &value);

/// Type used for operator inference: exact for constant tensors, facts for symbolic tensors, the
/// Camel type for symbolic scalars and int arrays.
type::Type *inferenceTypeOf(const Value &value);

/// The value as a constant operator argument (scalars, strings, int arrays), when it is one.
std::optional<tensor::ops::ConstArg> constArgOf(const Value &value);

/// The export-time view of an nn OptimizerState (first and second moment trees plus the step
/// count) as one aggregate value, so it can flow between graph inputs, the nn:adam lowering,
/// and graph outputs. The struct type is fabricated for naming (fields m, v, t); the runtime
/// state object itself is opaque.
Value optimizerStateValue(Value m, Value v, Value t);

/// The (m, v, t) parts of a value built by optimizerStateValue.
std::array<Value, 3> optimizerStateParts(const Value &state);

class CapabilityTable;

class Emitter {
  public:
    /// `capabilities` (optional) is the target runtime's: every node is checked against it.
    explicit Emitter(int64_t opset, const CapabilityTable *capabilities = nullptr);

    int64_t opset() const { return opset_; }

    /// The main graph (inputs, outputs, initializers, top-level nodes).
    Graph &graph() { return scopes_.front().graph; }

    /// A value name unique within the model, derived from `hint`.
    std::string fresh(std::string_view hint);

    /// Appends a single-output node to the current scope and returns its output name. An identical
    /// node (same operator, inputs, and attributes) already visible from this scope is reused:
    /// every operator the exporter emits is deterministic, so this is structural CSE.
    std::string node(
        std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes = {},
        std::string_view hint = {});

    /// Appends a two-output node (e.g. MaxPool with its indices output) and returns both output
    /// names. Not memoized: multi-output nodes are emitted once per call site.
    std::pair<std::string, std::string> node2(
        std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes,
        ElemType secondElem, std::string_view hint = {});

    /// Appends a node with `outputCount` outputs (e.g. an If whose arms yield a tuple) and
    /// returns the output names. Not memoized: multi-output nodes are emitted once per call site.
    std::vector<std::string> nodeN(
        std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes,
        size_t outputCount, std::string_view hint = {});

    /// Appends an Identity node to `body` and returns its output name. Never merged: a graph
    /// output (of a branch arm, or of the model for a result that is also another output) needs
    /// a value of its own, even when its operand is the same value. Used for the outputs of a
    /// branch arm that yields several values (the arm subgraph is complete before the leaf count
    /// is known, so the output Identities are added afterwards) and for the model's outputs.
    std::string appendIdentity(Graph &body, const std::string &input, std::string_view hint);

    /// An Identity in the current scope that is never the one of an enclosing scope. A branch
    /// arm's output must be a node of the arm's own graph, but node() reuses a structurally
    /// equal node it finds in an enclosing scope.
    std::string armOutput(const std::string &input, std::string_view hint);

    /// Operand name for `value`, converted to `dtype` when given.
    std::string operand(const Value &value, std::optional<type::TypeCode> dtype = std::nullopt);

    /// Name of a constant 1-D int64 tensor (shapes, axes, slice bounds).
    std::string int64s(std::span<const int64_t> values);

    /// Declares a graph input.
    void addInput(ValueInfo info);

    /// Element type of a value emitted so far, when known.
    std::optional<ElemType> valueElemType(const std::string &name) const;

    /// Keeps a GC object produced during export alive until the export finishes.
    void retain(rtdata::Object *object, const type::Type *type);

    /// Records one metadata_props entry: a Camel node's attributes and numeric permissions,
    /// which the ONNX format cannot put on a node. The keys are `camel.node.<n>`; the exporter
    /// writes them once per node that carries anything.
    void addMetadata(std::string key, std::string value);
    /// The metadata recorded so far, in insertion order.
    const std::vector<MetadataEntry> &metadata() const { return metadata_; }

    /// Starts a nested scope (an If branch body); nodes go there until popScope().
    void pushScope(std::string name);
    Graph popScope();

    /// Speculation: while above 0, the nodes emitted compute an arm of a branch whose
    /// condition depends on the model input, and the model computes both arms and selects
    /// (Where). A node that can fault in ONNX where Camel would not run it must not be emitted
    /// then (see lowerSlice).
    void beginSpeculation() { ++speculation_; }
    void endSpeculation() { --speculation_; }
    bool speculative() const { return speculation_ > 0; }

  private:
    std::string constantOperand(const Value &value, std::optional<type::TypeCode> dtype);
    /// Element type of the output of `opType` applied to `inputs`, when the rules know it.
    std::optional<ElemType> outputElemType(
        const std::string &opType, const std::vector<std::string> &inputs,
        const std::vector<Attribute> &attributes) const;
    /// Throws ExportError when the target cannot run `opType` on `inputs`.
    void checkCapability(const std::string &opType, const std::vector<std::string> &inputs) const;
    std::string addInitializer(TensorValue tensor, const std::string &key);

    struct Scope {
        Graph graph;
        std::map<std::pair<std::string, type::TypeCode>, std::string> casts;
        std::unordered_map<std::string, std::string> nodes; // structural key -> output name
    };

    const CapabilityTable *capabilities_;
    std::unordered_map<std::string, ElemType> elemTypes_;
    int64_t opset_;
    int speculation_ = 0;
    std::deque<Scope> scopes_;
    std::unordered_set<std::string> usedNames_;
    std::unordered_map<std::string, size_t> nameCounts_;
    std::unordered_map<std::string, std::string> initializerByKey_;
    std::vector<MetadataEntry> metadata_;
    std::deque<core::mm::RootHandle> roots_;
};

} // namespace camel::onnx
