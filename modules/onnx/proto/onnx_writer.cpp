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
 *   Implementation of the dependency-free ONNX writer: a minimal protobuf
 *   wire-format encoder plus the mapping from the camel::onnx object model to
 *   the onnx.proto field numbers.
 *
 *   Encoding strategy: every message is encoded bottom-up into its own byte
 *   buffer, then embedded into its parent as a length-delimited field. This
 *   avoids a separate size-computation pass at the cost of one copy per
 *   nesting level, which is negligible next to tensor payloads (raw_data is
 *   still copied once per level; models here are small enough that this is
 *   acceptable).
 *
 *   Field numbers were verified against onnx/onnx.proto from onnx 1.23.
 *   onnx.proto is proto2, where repeated scalars default to unpacked; we emit
 *   them packed, which every conforming protobuf parser must accept.
 */

#include "onnx_writer.h"

#include <bit>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace camel::onnx {

namespace {

// ---------------------------------------------------------------------------
// Protobuf wire-format primitives
// ---------------------------------------------------------------------------

enum class WireType : uint32_t {
    Varint          = 0,
    Fixed64         = 1,
    LengthDelimited = 2,
    Fixed32         = 5,
};

class WireWriter {
  public:
    void varint(uint64_t v) {
        while (v >= 0x80) {
            buf_.push_back(static_cast<std::byte>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        buf_.push_back(static_cast<std::byte>(v));
    }

    void fixed32(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            buf_.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
        }
    }

    void fixed64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf_.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
        }
    }

    void tag(uint32_t field, WireType wt) {
        varint((static_cast<uint64_t>(field) << 3) | static_cast<uint32_t>(wt));
    }

    // int32/int64 fields: negative values are sign-extended to 64 bits and
    // encoded as 10-byte varints (protobuf semantics for non-zigzag ints).
    void intField(uint32_t field, int64_t v) {
        tag(field, WireType::Varint);
        varint(static_cast<uint64_t>(v));
    }

    void floatField(uint32_t field, float v) {
        tag(field, WireType::Fixed32);
        fixed32(std::bit_cast<uint32_t>(v));
    }

    void bytesField(uint32_t field, std::span<const std::byte> data) {
        tag(field, WireType::LengthDelimited);
        varint(data.size());
        buf_.insert(buf_.end(), data.begin(), data.end());
    }

    void stringField(uint32_t field, std::string_view s) {
        bytesField(field, std::as_bytes(std::span(s.data(), s.size())));
    }

    // proto2 "optional string": omit when empty.
    void optStringField(uint32_t field, std::string_view s) {
        if (!s.empty()) {
            stringField(field, s);
        }
    }

    void messageField(uint32_t field, const WireWriter &msg) { bytesField(field, msg.buf_); }

    void packedInts(uint32_t field, std::span<const int64_t> values) {
        if (values.empty()) {
            return;
        }
        WireWriter body;
        for (int64_t v : values) {
            body.varint(static_cast<uint64_t>(v));
        }
        messageField(field, body);
    }

    void packedFloats(uint32_t field, std::span<const float> values) {
        if (values.empty()) {
            return;
        }
        WireWriter body;
        for (float v : values) {
            body.fixed32(std::bit_cast<uint32_t>(v));
        }
        messageField(field, body);
    }

    std::vector<std::byte> take() && { return std::move(buf_); }

  private:
    std::vector<std::byte> buf_;
};

// ---------------------------------------------------------------------------
// onnx.proto field numbers (verified against onnx 1.23 onnx.proto)
// ---------------------------------------------------------------------------

namespace model_f {
constexpr uint32_t IrVersion = 1, ProducerName = 2, ProducerVersion = 3, Graph = 7, OpsetImport = 8;
}
namespace opset_f {
constexpr uint32_t Domain = 1, Version = 2;
}
namespace graph_f {
constexpr uint32_t Node = 1, Name = 2, Initializer = 5, Input = 11, Output = 12, ValueInfo = 13;
}
namespace node_f {
constexpr uint32_t Input = 1, Output = 2, Name = 3, OpType = 4, Attribute = 5, Domain = 7;
}
namespace attr_f {
constexpr uint32_t Name = 1, F = 2, I = 3, S = 4, T = 5, G = 6, Floats = 7, Ints = 8, Strings = 9,
                   Type = 20;
}
namespace tensor_f {
constexpr uint32_t Dims = 1, DataType = 2, Name = 8, RawData = 9;
}
namespace value_info_f {
constexpr uint32_t Name = 1, Type = 2;
}
namespace type_f {
constexpr uint32_t TensorType = 1;          // TypeProto
constexpr uint32_t ElemType = 1, Shape = 2; // TypeProto.Tensor
} // namespace type_f
namespace shape_f {
constexpr uint32_t Dim      = 1;               // TensorShapeProto
constexpr uint32_t DimValue = 1, DimParam = 2; // TensorShapeProto.Dimension
} // namespace shape_f

// AttributeProto.AttributeType
enum class AttrType : int64_t {
    Float   = 1,
    Int     = 2,
    String  = 3,
    Tensor  = 4,
    Graph   = 5,
    Floats  = 6,
    Ints    = 7,
    Strings = 8,
};

// ---------------------------------------------------------------------------
// Message encoders
// ---------------------------------------------------------------------------

WireWriter encodeGraph(const Graph &graph);

WireWriter encodeTensor(const TensorValue &t) {
    int64_t count = 1;
    for (int64_t d : t.dims) {
        if (d < 0) {
            throw std::invalid_argument("onnx: tensor '" + t.name + "' has a negative dim");
        }
        count *= d;
    }
    if (static_cast<size_t>(count) * elemSize(t.type) != t.raw.size()) {
        throw std::invalid_argument(
            "onnx: tensor '" + t.name + "' raw size " + std::to_string(t.raw.size()) +
            " does not match dims (expected " +
            std::to_string(static_cast<size_t>(count) * elemSize(t.type)) + " bytes)");
    }

    WireWriter w;
    w.packedInts(tensor_f::Dims, t.dims);
    w.intField(tensor_f::DataType, static_cast<int32_t>(t.type));
    w.optStringField(tensor_f::Name, t.name);
    // Always emit raw_data (even when empty) so the payload location is explicit.
    w.bytesField(tensor_f::RawData, t.raw);
    return w;
}

WireWriter encodeValueInfo(const ValueInfo &vi) {
    WireWriter tensorType;
    tensorType.intField(type_f::ElemType, static_cast<int32_t>(vi.type));
    if (vi.shape) {
        // An engaged but empty shape is emitted as an empty TensorShapeProto,
        // which ONNX reads as rank 0 (scalar).
        WireWriter shape;
        for (const Dim &d : *vi.shape) {
            WireWriter dim;
            if (const auto *v = std::get_if<int64_t>(&d)) {
                dim.intField(shape_f::DimValue, *v);
            } else {
                dim.stringField(shape_f::DimParam, std::get<std::string>(d));
            }
            shape.messageField(shape_f::Dim, dim);
        }
        tensorType.messageField(type_f::Shape, shape);
    }

    WireWriter type;
    type.messageField(type_f::TensorType, tensorType);

    WireWriter w;
    w.stringField(value_info_f::Name, vi.name);
    w.messageField(value_info_f::Type, type);
    return w;
}

WireWriter encodeAttribute(const Attribute &a) {
    WireWriter w;
    w.stringField(attr_f::Name, a.name);

    // Scalar payloads are emitted even when zero: the `type` discriminator
    // tells readers which field is meaningful, and presence avoids ambiguity.
    auto kind = std::visit(
        [&w](const auto &v) -> AttrType {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>) {
                w.floatField(attr_f::F, v);
                return AttrType::Float;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                w.intField(attr_f::I, v);
                return AttrType::Int;
            } else if constexpr (std::is_same_v<T, std::string>) {
                w.stringField(attr_f::S, v);
                return AttrType::String;
            } else if constexpr (std::is_same_v<T, TensorValue>) {
                w.messageField(attr_f::T, encodeTensor(v));
                return AttrType::Tensor;
            } else if constexpr (std::is_same_v<T, Attribute::GraphRef>) {
                if (!v) {
                    throw std::invalid_argument("onnx: graph attribute holds a null graph");
                }
                w.messageField(attr_f::G, encodeGraph(*v));
                return AttrType::Graph;
            } else if constexpr (std::is_same_v<T, std::vector<float>>) {
                w.packedFloats(attr_f::Floats, v);
                return AttrType::Floats;
            } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                w.packedInts(attr_f::Ints, v);
                return AttrType::Ints;
            } else {
                static_assert(std::is_same_v<T, std::vector<std::string>>);
                // `repeated bytes` cannot be packed; one field per element.
                for (const auto &s : v) {
                    w.stringField(attr_f::Strings, s);
                }
                return AttrType::Strings;
            }
        },
        a.value);

    w.intField(attr_f::Type, static_cast<int64_t>(kind));
    return w;
}

WireWriter encodeNode(const Node &n) {
    WireWriter w;
    // Repeated strings keep position, so empty names ("omitted optional
    // input") must still be written.
    for (const auto &in : n.inputs) {
        w.stringField(node_f::Input, in);
    }
    for (const auto &out : n.outputs) {
        w.stringField(node_f::Output, out);
    }
    w.optStringField(node_f::Name, n.name);
    w.stringField(node_f::OpType, n.opType);
    for (const auto &a : n.attributes) {
        w.messageField(node_f::Attribute, encodeAttribute(a));
    }
    w.optStringField(node_f::Domain, n.domain);
    return w;
}

WireWriter encodeGraph(const Graph &g) {
    WireWriter w;
    for (const auto &n : g.nodes) {
        w.messageField(graph_f::Node, encodeNode(n));
    }
    w.optStringField(graph_f::Name, g.name);
    for (const auto &t : g.initializers) {
        w.messageField(graph_f::Initializer, encodeTensor(t));
    }
    for (const auto &vi : g.inputs) {
        w.messageField(graph_f::Input, encodeValueInfo(vi));
    }
    for (const auto &vi : g.outputs) {
        w.messageField(graph_f::Output, encodeValueInfo(vi));
    }
    for (const auto &vi : g.valueInfos) {
        w.messageField(graph_f::ValueInfo, encodeValueInfo(vi));
    }
    return w;
}

WireWriter encodeOpset(std::string_view domain, int64_t version) {
    WireWriter w;
    // The default domain is the empty string; proto2 absence reads as "".
    w.optStringField(opset_f::Domain, domain);
    w.intField(opset_f::Version, version);
    return w;
}

// Copies host elements into little-endian raw bytes independent of host order.
template <typename T, typename Bits>
TensorValue
packTensor(std::string name, std::vector<int64_t> dims, std::span<const T> data, ElemType type) {
    int64_t count = 1;
    for (int64_t d : dims) {
        count *= d;
    }
    if (count < 0 || static_cast<size_t>(count) != data.size()) {
        throw std::invalid_argument(
            "onnx: tensor '" + name + "' element count " + std::to_string(data.size()) +
            " does not match dims");
    }

    TensorValue t{std::move(name), type, std::move(dims), {}};
    t.raw.reserve(data.size() * sizeof(Bits));
    for (const T &v : data) {
        Bits bits;
        if constexpr (std::is_same_v<T, bool>) {
            bits = v ? 1 : 0;
        } else {
            bits = std::bit_cast<Bits>(v);
        }
        for (size_t i = 0; i < sizeof(Bits); ++i) {
            t.raw.push_back(static_cast<std::byte>((bits >> (8 * i)) & 0xFF));
        }
    }
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

size_t elemSize(ElemType type) {
    switch (type) {
    case ElemType::Float:
        return 4;
    case ElemType::Int32:
        return 4;
    case ElemType::Int64:
        return 8;
    case ElemType::Bool:
        return 1;
    case ElemType::Double:
        return 8;
    }
    throw std::invalid_argument("onnx: unknown element type");
}

TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const float> data) {
    return packTensor<float, uint32_t>(std::move(name), std::move(dims), data, ElemType::Float);
}

TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const double> data) {
    return packTensor<double, uint64_t>(std::move(name), std::move(dims), data, ElemType::Double);
}

TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const int32_t> data) {
    return packTensor<int32_t, uint32_t>(std::move(name), std::move(dims), data, ElemType::Int32);
}

TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const int64_t> data) {
    return packTensor<int64_t, uint64_t>(std::move(name), std::move(dims), data, ElemType::Int64);
}

TensorValue makeTensor(std::string name, std::vector<int64_t> dims, std::span<const bool> data) {
    return packTensor<bool, uint8_t>(std::move(name), std::move(dims), data, ElemType::Bool);
}

Attribute Attribute::makeFloat(std::string name, float v) { return {std::move(name), v}; }

Attribute Attribute::makeInt(std::string name, int64_t v) { return {std::move(name), v}; }

Attribute Attribute::makeString(std::string name, std::string v) {
    return {std::move(name), std::move(v)};
}

Attribute Attribute::makeTensor(std::string name, TensorValue v) {
    return {std::move(name), std::move(v)};
}

Attribute Attribute::makeGraph(std::string name, Graph g) {
    return {std::move(name), std::make_shared<const Graph>(std::move(g))};
}

Attribute Attribute::makeFloats(std::string name, std::vector<float> v) {
    return {std::move(name), std::move(v)};
}

Attribute Attribute::makeInts(std::string name, std::vector<int64_t> v) {
    return {std::move(name), std::move(v)};
}

Attribute Attribute::makeStrings(std::string name, std::vector<std::string> v) {
    return {std::move(name), std::move(v)};
}

std::vector<std::byte> serialize(const Model &model) {
    WireWriter w;
    w.intField(model_f::IrVersion, model.irVersion);
    w.optStringField(model_f::ProducerName, model.producer);
    w.optStringField(model_f::ProducerVersion, model.producerVersion);
    w.messageField(model_f::Graph, encodeGraph(model.graph));
    w.messageField(model_f::OpsetImport, encodeOpset("", model.opset));
    for (const auto &op : model.extraOpsets) {
        w.messageField(model_f::OpsetImport, encodeOpset(op.domain, op.version));
    }
    return std::move(w).take();
}

void writeModelFile(const Model &model, const std::filesystem::path &path) {
    // Serialize first so a validation error never leaves a truncated file.
    const std::vector<std::byte> bytes = serialize(model);

    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec); // open() reports failures
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("onnx: cannot open '" + path.string() + "' for writing");
    }
    out.write(
        reinterpret_cast<const char *>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("onnx: failed to write '" + path.string() + "'");
    }
}

} // namespace camel::onnx
