/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Translate the supported pure tensor subgraphs without running user operators on the host.
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/ops/support.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/program.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles {
namespace rt = runtime;
namespace ty = core::type;
namespace td = core::rtdata;
using tensor::ops::ConstArg;

namespace {
struct Value {
    ty::Type *type = nullptr;
    std::optional<ConstArg> constant;
    size_t tensor = SIZE_MAX;
    std::vector<Value> fields;
};
struct Frame {
    rt::GCGraph *graph;
    std::unordered_map<rt::gc_node_ref_t, Value> values;
    std::unordered_set<rt::gc_node_ref_t> visiting;
};

class Compiler {
  public:
    Program program;
    std::unordered_set<rt::GCGraph *> active;

    size_t append(Node node) {
        if (program.nodes.size() >= 4096)
            throw std::runtime_error("tiles graph budget exceeded (4096 values)");
        elements(node.shape);
        program.nodes.push_back(std::move(node));
        return program.nodes.size() - 1;
    }

    Value host(slot_t slot, ty::Type *type) {
        if (type->code() == ty::TypeCode::Tuple || type->code() == ty::TypeCode::Struct) {
            Value value{.type = type};
            const bool tuple   = type->code() == ty::TypeCode::Tuple;
            const size_t count = tuple ? static_cast<ty::TupleType *>(type)->size()
                                       : static_cast<ty::StructType *>(type)->size();
            for (size_t i = 0; i < count; ++i) {
                auto *fieldType = tuple ? static_cast<ty::TupleType *>(type)->typeAt(i)
                                        : static_cast<ty::StructType *>(type)->typeAt(i);
                auto field      = slot == NullSlot ? NullSlot
                                  : tuple          ? td::fromSlot<::Tuple *>(slot)->get<slot_t>(i)
                                                   : td::fromSlot<::Struct *>(slot)->get<slot_t>(i);
                // FILL templates contain null placeholders. Unfilled leaves cannot
                // become tensor operands or outputs; the normal checks reject them.
                value.fields.push_back(
                    field == NullSlot && ty::isGCTraced(fieldType->code())
                        ? Value{.type = fieldType}
                        : host(field, fieldType));
            }
            return value;
        }
        if (tensor::asTensorType(type)) {
            const auto *t = td::fromSlot<tensor::TensorObject *>(slot);
            if (!t || t->dtype() != ty::TypeCode::Float32)
                throw std::runtime_error("tiles currently requires f32 tensors");
            Node node{.op = "input", .shape = t->shapeVector()};
            const size_t n = elements(node.shape);
            if (n)
                node.data.assign(t->dataAs<float>(), t->dataAs<float>() + n);
            auto *exact = tensor::TensorType::get(ty::TypeCode::Float32, node.shape);
            return {.type = exact, .tensor = append(std::move(node))};
        }
        auto constant = tensor::ops::constArgOf(slot, type);
        if (!constant)
            throw std::runtime_error("unsupported captured/static value: " + type->toString());
        return {.type = type, .constant = std::move(constant)};
    }

    int64_t integer(const Value &v) {
        if (!v.constant || !std::holds_alternative<int64_t>(*v.constant))
            throw std::runtime_error(
                "an integer attribute must be known when specializing the graph");
        return std::get<int64_t>(*v.constant);
    }
    size_t tensorValue(const Value &v) {
        if (v.tensor != SIZE_MAX)
            return v.tensor;
        if (!v.constant)
            throw std::runtime_error("expected a tensor or numeric constant");
        double x;
        if (auto p = std::get_if<double>(&*v.constant))
            x = *p;
        else if (auto p = std::get_if<int64_t>(&*v.constant))
            x = static_cast<double>(*p);
        else
            throw std::runtime_error("expected a tensor or numeric constant");
        return append(Node{.op = "input", .shape = {}, .data = {static_cast<float>(x)}});
    }

    Value call(
        rt::GCGraph *g, const std::vector<Value> &with, const std::vector<Value> &norm,
        const std::vector<Value> &captures = {}) {
        if (!g || active.contains(g) || active.size() >= 64)
            throw std::runtime_error(
                "recursive or excessively deep calls are not supported by tiles yet");
        if (with.size() != g->withPorts().size() || norm.size() != g->normPorts().size() ||
            captures.size() != g->closureNodes().size())
            throw std::runtime_error("unbound function context or argument count mismatch");
        auto *ft = g->funcType();
        for (size_t i = 0; i < ft->withTypesCount(); ++i)
            if (ft->withIsVarAt(i))
                throw std::runtime_error("mutable context is not supported by tiles");
        for (size_t i = 0; i < ft->normTypesCount(); ++i)
            if (ft->normIsVarAt(i))
                throw std::runtime_error("mutable arguments are not supported by tiles");
        active.insert(g);
        Frame f{.graph = g};
        for (size_t i = 0; i < with.size(); ++i)
            f.values.emplace(g->withPorts()[i], with[i]);
        for (size_t i = 0; i < norm.size(); ++i)
            f.values.emplace(g->normPorts()[i], norm[i]);
        for (size_t i = 0; i < captures.size(); ++i)
            f.values.emplace(g->closureNodes()[i], captures[i]);
        // Visit control dependencies too: an unused println in a sync body must not disappear.
        if (g->exitNodeRef() != rt::kInvalidNodeRef)
            eval(f, g->exitNodeRef());
        auto value = eval(f, camel::execute::resolveRuntimeTailValueRef(g));
        active.erase(g);
        return value;
    }

    Value eval(Frame &f, rt::gc_node_ref_t id) {
        if (auto it = f.values.find(id); it != f.values.end())
            return it->second;
        if (!f.graph->containsNodeRef(id) || !f.visiting.insert(id).second)
            throw std::runtime_error("invalid or cyclic graph dependency");
        auto *g          = f.graph;
        const auto *node = g->node(id);
        try {
            for (auto dep : g->ctrlInputsOf(id))
                eval(f, dep);
            Value out;
            const auto ins = g->normInputsOf(id);
            switch (node->kind) {
            case rt::GCNodeKind::Data:
                if (node->dataIndex >= 0)
                    throw std::runtime_error("nonconstant DATA node");
                out = host(g->staticArea()->get<slot_t>(size_t(-node->dataIndex)), node->dataType);
                break;
            case rt::GCNodeKind::Copy:
            case rt::GCNodeKind::Gate:
                if (!ins.empty())
                    out = eval(f, ins.front());
                break;
            case rt::GCNodeKind::Sync:
                for (auto in : ins)
                    out = eval(f, in);
                break;
            case rt::GCNodeKind::Cast: {
                out                = eval(f, ins.front());
                const auto *target = tensor::asTensorType(node->dataType);
                if (!target || out.tensor == SIZE_MAX || !target->assignableFrom(out.type))
                    throw std::runtime_error("unsupported cast or tensor shape assertion failed");
                break;
            }
            case rt::GCNodeKind::Func: {
                std::vector<Value> with, norm;
                for (auto in : g->withInputsOf(id))
                    with.push_back(eval(f, in));
                for (auto in : ins)
                    norm.push_back(eval(f, in));
                out = call(g->directCalleeGraphOf(id), with, norm);
                break;
            }
            case rt::GCNodeKind::Fill: {
                if (node->dataType->code() != ty::TypeCode::Tuple &&
                    node->dataType->code() != ty::TypeCode::Struct)
                    throw std::runtime_error(
                        "tiles supports only tuple/struct aggregate construction");
                out               = eval(f, ins.front());
                const auto slots  = g->nodeBodyAs<rt::GCFillBody>(id)->slots();
                const auto values = g->withInputsOf(id);
                if (slots.size() != values.size())
                    throw std::runtime_error("aggregate fill arity mismatch");
                for (size_t i = 0; i < slots.size(); ++i)
                    out.fields.at(slots[i]) = eval(f, values[i]);
                out.type = node->dataType;
                break;
            }
            case rt::GCNodeKind::Accs: {
                auto value         = eval(f, ins.front());
                const auto *access = g->nodeBodyAs<rt::GCAccsBody>(id);
                size_t index       = access->value;
                if (access->accsKind == rt::GCAccsKind::StructKey) {
                    if (value.type->code() != ty::TypeCode::Struct)
                        throw std::runtime_error("expected struct aggregate");
                    auto field =
                        static_cast<ty::StructType *>(value.type)->findField(access->key());
                    if (!field)
                        throw std::runtime_error("missing struct field");
                    index = *field;
                }
                out = value.fields.at(index);
                break;
            }
            case rt::GCNodeKind::Oper: {
                std::vector<Value> args;
                if (!g->withInputsOf(id).empty())
                    throw std::runtime_error("operator context arguments are not supported yet");
                for (auto in : ins)
                    args.push_back(eval(f, in));
                out = operation(std::string(g->nodeBodyAs<rt::GCOperBody>(id)->uri()), args);
                break;
            }
            default:
                throw std::runtime_error(
                    "unsupported graph node kind " + std::to_string(int(node->kind)));
            }
            f.visiting.erase(id);
            f.values.emplace(id, out);
            return out;
        } catch (const std::exception &e) {
            throw std::runtime_error(g->name() + " node " + std::to_string(id) + ": " + e.what());
        }
    }

    Value operation(const std::string &uri, const std::vector<Value> &args) {
        const auto *def = tensor::ops::OpRegistry::instance().find(uri);
        if (!def || !def->traits.pure)
            throw std::runtime_error("unsupported or effectful operator '" + uri + "'");
        std::vector<ty::Type *> types;
        std::vector<std::optional<ConstArg>> constants;
        for (const auto &v : args) {
            types.push_back(v.type);
            constants.push_back(v.constant);
        }
        auto inferred      = def->infer(tensor::ops::InferContext(types, constants));
        const auto *result = inferred ? tensor::asTensorType(*inferred) : nullptr;
        if (!result || result->dtype() != ty::TypeCode::Float32 || !result->isStaticShape())
            throw std::runtime_error(
                "operator '" + uri + "' needs a statically known f32 tensor result");
        Node n{.op = std::string(def->name), .shape = *result->shape()};
        if (n.op == "sum_to" || n.op == "cast_like") {
            n.inputs = {tensorValue(args.at(0))};
            if (program.nodes[n.inputs[0]].shape == n.shape)
                return {.type = *inferred, .tensor = n.inputs[0]};
            if (n.op == "cast_like")
                throw std::runtime_error("cast_like shape mismatch");
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        if (n.op == "broadcast_like" || n.op == "expand_axis" || n.op == "ones_like" ||
            n.op == "zeros_like") {
            size_t input;
            if (n.op == "ones_like" || n.op == "zeros_like")
                input = append(
                    Node{.op = "input", .shape = {}, .data = {n.op == "ones_like" ? 1.0f : 0.0f}});
            else
                input = tensorValue(args.at(0));
            Shape shape = program.nodes[input].shape;
            if (n.op == "expand_axis") {
                int64_t axis = integer(args.at(2));
                if (axis < 0)
                    axis += static_cast<int64_t>(n.shape.size());
                if (axis < 0 || size_t(axis) >= n.shape.size())
                    throw std::runtime_error("gradient axis out of bounds");
                Shape kept = n.shape;
                kept[axis] = 1;
                if (elements(kept) != elements(shape))
                    throw std::runtime_error("gradient reshape count mismatch");
                if (kept != shape)
                    input = append(Node{.op = "reshape", .shape = kept, .inputs = {input}});
                shape = std::move(kept);
            }
            if (shape.size() > n.shape.size())
                throw std::runtime_error("gradient broadcast rank");
            n.strides.assign(n.shape.size(), 0);
            size_t stride      = 1;
            const size_t shift = n.shape.size() - shape.size();
            for (size_t d = shape.size(); d-- > 0;) {
                if (shape[d] != 1 && shape[d] != n.shape[d + shift])
                    throw std::runtime_error("gradient broadcast extent");
                n.strides[d + shift] = shape[d] == 1 ? 0 : stride;
                stride *= size_t(shape[d]);
            }
            n.op     = "reindex";
            n.inputs = {input};
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        if (n.op == "matmul_grad_lhs" || n.op == "matmul_grad_rhs") {
            size_t dy  = tensorValue(args.at(0));
            size_t lhs = tensorValue(args.at(1)), rhs = tensorValue(args.at(2));
            const auto a = program.nodes[lhs].shape, b = program.nodes[rhs].shape;
            if (a.size() != 2 || b.size() != 2)
                throw std::runtime_error("tiles matmul gradient requires rank 2");
            const bool left   = n.op == "matmul_grad_lhs";
            const auto &shape = left ? b : a;
            size_t transposed = append(
                Node{
                    .op      = "reindex",
                    .shape   = {shape[1], shape[0]},
                    .inputs  = {left ? rhs : lhs},
                    .strides = {1, size_t(shape[1])}});
            n.op = "matmul";
            n.inputs =
                left ? std::vector<size_t>{dy, transposed} : std::vector<size_t>{transposed, dy};
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        const std::string unary  = "|neg|abs|exp|log|sqrt|rsqrt|relu|";
        const std::string binary = "|add|subtract|multiply|divide|maximum|minimum|";
        if (unary.find("|" + n.op + "|") != std::string::npos) {
            n.inputs = {tensorValue(args.at(0))};
        } else if (binary.find("|" + n.op + "|") != std::string::npos || n.op == "matmul") {
            n.inputs = {tensorValue(args.at(0)), tensorValue(args.at(1))};
            if (n.op == "matmul" && (program.nodes[n.inputs[0]].shape.size() != 2 ||
                                     program.nodes[n.inputs[1]].shape.size() != 2))
                throw std::runtime_error("tiles matmul currently requires two rank-2 tensors");
        } else if (n.op == "sum_axis" || n.op == "max_axis" || n.op == "mean_axis") {
            n.inputs          = {tensorValue(args.at(0))};
            int64_t axis      = integer(args.at(1));
            const auto &shape = program.nodes[n.inputs[0]].shape;
            if (axis < 0)
                axis += static_cast<int64_t>(shape.size());
            if (axis < 0 || size_t(axis) >= shape.size())
                throw std::runtime_error("reduction axis out of bounds");
            if (n.op == "max_axis" && shape[axis] == 0)
                throw std::runtime_error("max over an empty axis");
            n.attributes = {axis};
        } else if (n.op == "transpose" || n.op == "permute" || n.op == "slice") {
            n.inputs          = {tensorValue(args.at(0))};
            const auto &shape = program.nodes[n.inputs[0]].shape;
            std::vector<size_t> strides(shape.size());
            for (size_t d = 0; d < shape.size(); ++d)
                strides[d] = elements(Shape(shape.begin() + d + 1, shape.end()));
            if (n.op == "slice") {
                int64_t axis = integer(args.at(1));
                if (axis < 0)
                    axis += static_cast<int64_t>(shape.size());
                if (axis < 0 || size_t(axis) >= shape.size())
                    throw std::runtime_error("slice axis out of bounds");
                int64_t start = integer(args.at(2));
                if (start < 0)
                    start += shape[axis];
                const int64_t step = args.size() > 4 ? integer(args.at(4)) : 1;
                if (start < 0 || start > shape[axis] || step <= 0)
                    throw std::runtime_error("invalid static slice interval");
                // Empty domains never dereference an address. Canonicalize their map
                // to avoid overflowing unused endpoint/stride products.
                if (elements(n.shape)) {
                    n.offset = size_t(start) * strides[axis];
                    if (size_t(step) > SIZE_MAX / std::max(strides[axis], size_t(1)))
                        throw std::runtime_error("slice stride overflows");
                    strides[axis] *= size_t(step);
                } else {
                    std::fill(strides.begin(), strides.end(), 0);
                }
                n.strides = std::move(strides);
            } else {
                std::vector<int64_t> permutation(shape.size());
                std::iota(permutation.begin(), permutation.end(), 0);
                if (n.op == "transpose") {
                    if (shape.size() < 2)
                        throw std::runtime_error("transpose needs rank >= 2");
                    std::swap(permutation[shape.size() - 1], permutation[shape.size() - 2]);
                } else {
                    if (!args.at(1).constant ||
                        !std::holds_alternative<std::vector<int64_t>>(*args.at(1).constant))
                        throw std::runtime_error("permutation must be known when specializing");
                    permutation = std::get<std::vector<int64_t>>(*args.at(1).constant);
                }
                if (permutation.size() != shape.size())
                    throw std::runtime_error("permutation rank mismatch");
                std::vector<bool> seen(shape.size());
                for (auto axis : permutation) {
                    if (axis < 0)
                        axis += static_cast<int64_t>(shape.size());
                    if (axis < 0 || size_t(axis) >= shape.size() || seen[axis])
                        throw std::runtime_error("invalid permutation");
                    seen[axis] = true;
                    n.strides.push_back(strides[axis]);
                }
            }
            n.op = "reindex";
        } else if (n.op == "reshape") {
            n.inputs = {tensorValue(args.at(0))};
            if (elements(program.nodes[n.inputs[0]].shape) != elements(n.shape))
                throw std::runtime_error("reshape changes the number of elements");
        } else {
            throw std::runtime_error("no CUDA lowering for '" + uri + "'");
        }
        return {.type = *inferred, .tensor = append(std::move(n))};
    }
};
} // namespace

Program compile(Function *function, const std::vector<Argument> &arguments) {
    if (!function || !function->graph())
        throw std::runtime_error("expected a function graph");
    Compiler c;
    std::vector<Value> with, norm;
    if (auto *closure = function->tuple()) {
        const auto *types = function->tupleType();
        if (!types)
            throw std::runtime_error("closure type is missing");
        for (size_t i = 0; i < closure->size(); ++i)
            with.push_back(c.host(closure->get<slot_t>(i), types->typeAt(i)));
    }
    for (const auto &a : arguments)
        norm.push_back(c.host(a.value, a.type));
    auto out = c.call(function->graph(), {}, norm, with);
    c.program.outputs.clear();
    auto collect = [&](auto &&self, const Value &value) -> void {
        if (value.tensor != SIZE_MAX) {
            c.program.outputs.push_back(value.tensor);
        } else if (
            value.type &&
            (value.type->code() == ty::TypeCode::Tuple ||
             value.type->code() == ty::TypeCode::Struct) &&
            !value.fields.empty()) {
            for (const auto &field : value.fields)
                self(self, field);
        } else {
            throw std::runtime_error("tiles result must contain nonempty aggregates of tensors");
        }
    };
    collect(collect, out);
    generate(c.program);
    return std::move(c.program);
}
} // namespace camel::tiles
