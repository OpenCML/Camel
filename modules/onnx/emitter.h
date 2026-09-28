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
 * value names, appends nodes, and turns exporter values into node operands.
 * Constants are materialized once as initializers; symbolic operands of the
 * wrong dtype get a (memoized) Cast.
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
};

/// ONNX element type of a Camel tensor storage dtype.
ElemType elemTypeOf(type::TypeCode dtype);

/// Tensor facts of a value: symbolic facts, a constant tensor's exact facts, a scalar's rank 0.
tensor::ops::TensorFacts factsOf(const Value &value);

/// Type used for operator inference: exact for constant tensors, facts for symbolic ones.
type::Type *inferenceTypeOf(const Value &value);

/// The value as a constant operator argument (scalars, strings, int arrays), when it is one.
std::optional<tensor::ops::ConstArg> constArgOf(const Value &value);

class Emitter {
  public:
    explicit Emitter(int64_t opset);

    int64_t opset() const { return opset_; }

    /// The graph under construction.
    Graph &graph() { return graph_; }

    /// A value name unique within the model, derived from `hint`.
    std::string fresh(std::string_view hint);

    /// Appends a single-output node and returns its output name.
    std::string node(
        std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes = {},
        std::string_view hint = {});

    /// Operand name for `value`, converted to `dtype` when given.
    std::string operand(const Value &value, std::optional<type::TypeCode> dtype = std::nullopt);

    /// Name of a constant 1-D int64 tensor (shapes, axes, slice bounds).
    std::string int64s(std::span<const int64_t> values);

    /// Keeps a GC object produced during export alive until the export finishes.
    void retain(rtdata::Object *object, const type::Type *type);

  private:
    std::string constantOperand(const Value &value, std::optional<type::TypeCode> dtype);
    std::string addInitializer(TensorValue tensor, const std::string &key);

    int64_t opset_;
    Graph graph_;
    std::map<std::pair<std::string, type::TypeCode>, std::string> casts_;
    std::unordered_set<std::string> usedNames_;
    std::unordered_map<std::string, size_t> nameCounts_;
    std::unordered_map<std::string, std::string> initializerByKey_;
    std::deque<core::mm::RootHandle> roots_;
};

} // namespace camel::onnx
