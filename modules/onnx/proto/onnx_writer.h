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
 * Responsibility:
 *   Dependency-free ONNX model writer. Declares a small C++ object model that
 *   mirrors the subset of onnx.proto used by the Camel exporter (ModelProto,
 *   GraphProto, NodeProto, AttributeProto, TensorProto, ValueInfoProto) and the
 *   entry points that serialize it to the protobuf wire format.
 *
 *   This layer is purely structural: it performs no ONNX semantic checking
 *   (operator schemas, type inference, topological order). The only
 *   validation done at serialization time is that each tensor's raw payload
 *   size matches its dims and element type.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace camel::onnx {

// ONNX IR version written into ModelProto.ir_version (IR_VERSION_2021_7_30).
inline constexpr int64_t kDefaultIrVersion = 8;
// Default opset version for the default ("" / ai.onnx) domain.
inline constexpr int64_t kDefaultOpset = 17;

// Values of TensorProto.DataType. Only the types the exporter emits are listed.
enum class ElemType : int32_t {
    Float  = 1,
    Int32  = 6,
    Int64  = 7,
    Bool   = 9,
    Double = 11,
};

// Size in bytes of a single element of the given type.
size_t elemSize(ElemType type);

// A tensor constant (initializer or TENSOR attribute payload).
// `raw` holds the elements in little-endian, row-major order, exactly as
// TensorProto.raw_data expects. An empty `dims` denotes a scalar.
struct TensorValue {
    std::string name;
    ElemType type = ElemType::Float;
    std::vector<int64_t> dims;
    std::vector<std::byte> raw;
};

// Typed constructors that copy host elements into `raw`.
// Throws std::invalid_argument when data.size() does not match dims.
TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const float> data);
TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const double> data);
TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const int32_t> data);
TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const int64_t> data);
// Bool tensors are stored as one byte per element (0 or 1).
TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const bool> data);

// One tensor dimension: a concrete extent or a symbolic name (e.g. "batch").
using Dim = std::variant<int64_t, std::string>;

// Graph input / output / intermediate type annotation.
// `shape == std::nullopt` means the rank is unknown (no shape field emitted);
// an engaged empty vector means a scalar (rank 0).
struct ValueInfo {
    std::string name;
    ElemType type = ElemType::Float;
    std::optional<std::vector<Dim>> shape;
};

struct Graph;

// A node attribute. The active alternative of `value` selects the
// AttributeProto.type discriminator. Subgraphs (If/Loop bodies) are held by
// shared_ptr to break the Graph -> Node -> Attribute -> Graph recursion.
struct Attribute {
    using GraphRef = std::shared_ptr<const Graph>;
    using Value    = std::variant<
           float, int64_t, std::string, TensorValue, GraphRef, std::vector<float>,
           std::vector<int64_t>, std::vector<std::string>>;

    std::string name;
    Value value;

    static Attribute makeFloat(std::string name, float v);
    static Attribute makeInt(std::string name, int64_t v);
    static Attribute makeString(std::string name, std::string v);
    static Attribute makeTensor(std::string name, TensorValue v);
    static Attribute makeGraph(std::string name, Graph g);
    static Attribute makeFloats(std::string name, std::vector<float> v);
    static Attribute makeInts(std::string name, std::vector<int64_t> v);
    static Attribute makeStrings(std::string name, std::vector<std::string> v);
};

struct Node {
    std::string opType;
    std::string name;
    std::vector<std::string> inputs; // "" marks an omitted optional input
    std::vector<std::string> outputs;
    std::vector<Attribute> attributes;
    std::string domain; // "" = default ai.onnx domain (omitted on the wire)
};

struct Graph {
    std::string name;
    std::vector<Node> nodes; // must be topologically sorted
    std::vector<TensorValue> initializers;
    std::vector<ValueInfo> inputs;
    std::vector<ValueInfo> outputs;
    std::vector<ValueInfo> valueInfos;
};

// An additional operator-set import, needed only when nodes use a non-default
// domain (e.g. "com.microsoft").
struct OpsetImport {
    std::string domain;
    int64_t version = 1;
};

struct Model {
    Graph graph;
    int64_t irVersion = kDefaultIrVersion;
    int64_t opset     = kDefaultOpset; // version for the default domain ""
    std::vector<OpsetImport> extraOpsets;
    std::string producer = "camel";
    std::string producerVersion;
};

// Serializes `model` into ONNX ModelProto wire bytes.
// Throws std::invalid_argument if a tensor's raw size is inconsistent.
std::vector<std::byte> serialize(const Model &model);

// Serializes `model` and writes it to `path` (truncating any existing file and
// creating missing parent directories).
// Throws std::runtime_error on IO failure.
void writeModelFile(const Model &model, const std::filesystem::path &path);

} // namespace camel::onnx
