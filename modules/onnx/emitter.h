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
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    using std::runtime_error::runtime_error;
    ExportError(const std::string &what, std::string location)
        : std::runtime_error(what), location_(std::move(location)) {}

    /// Source position of the construct the export failed on ("file:line:col"), or empty.
    const std::string &location() const { return location_; }

  private:
    std::string location_;
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

class Emitter {
  public:
    explicit Emitter(int64_t opset);

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

    /// Operand name for `value`, converted to `dtype` when given.
    std::string operand(const Value &value, std::optional<type::TypeCode> dtype = std::nullopt);

    /// Name of a constant 1-D int64 tensor (shapes, axes, slice bounds).
    std::string int64s(std::span<const int64_t> values);

    /// Keeps a GC object produced during export alive until the export finishes.
    void retain(rtdata::Object *object, const type::Type *type);

    /// Starts a nested scope (an If branch body); nodes go there until popScope().
    void pushScope(std::string name);
    Graph popScope();

  private:
    std::string constantOperand(const Value &value, std::optional<type::TypeCode> dtype);
    std::string addInitializer(TensorValue tensor, const std::string &key);

    struct Scope {
        Graph graph;
        std::map<std::pair<std::string, type::TypeCode>, std::string> casts;
        std::unordered_map<std::string, std::string> nodes; // structural key -> output name
    };

    int64_t opset_;
    std::deque<Scope> scopes_;
    std::unordered_set<std::string> usedNames_;
    std::unordered_map<std::string, size_t> nameCounts_;
    std::unordered_map<std::string, std::string> initializerByKey_;
    std::deque<core::mm::RootHandle> roots_;
};

} // namespace camel::onnx
