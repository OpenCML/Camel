/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Translate the supported pure tensor subgraphs without running user operators on the host.
#include "camel/core/rtdata/array.h"
#include "camel/core/tags.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/loop.h"
#include "modules/tensor/ops/registry.h"
#include "modules/tensor/ops/support.h"
#include "modules/tensor/tensor.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/access.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/compiled_region.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace camel::tiles {
namespace rt = runtime;
namespace ty = core::type;
namespace td = core::rtdata;
using tensor::ops::ConstArg;

namespace {
thread_local size_t extractions = 0;
struct Value {
    ty::Type *type = nullptr;
    std::optional<ConstArg> constant;
    // Borrowed only during extraction under NoCollectionScope. The core's
    // instantiator consumes the original static value; Program never retains it.
    std::optional<slot_t> staticSlot;
    size_t tensor = SIZE_MAX;
    std::vector<Value> fields;
    // Borrowed function graph and captured values, used only while reading the graph.
    rt::GCGraph *function = nullptr;
    const rt::Program *functionProgram = nullptr; // the program `function` is a graph of
    bool loopElement = false; // Exact i64 body port, materialized only by an explicit conversion.
};
struct Frame {
    rt::GCGraph *graph;
    std::unordered_map<rt::gc_node_ref_t, Value> values;
    std::unordered_set<rt::gc_node_ref_t> visiting;
};

class Compiler {
  public:
    Program program;
    rt::fact::FactStore &facts;
    /// The program the graphs being compiled are of; null for a compiler that reads no graph.
    const rt::Program *runtimeProgram;
    Compiler(rt::fact::FactStore &store, const rt::Program *graphs)
        : facts(store), runtimeProgram(graphs) {}
    std::unordered_set<rt::GCGraph *> active;
    std::unordered_set<::Function *> loadingFunctions;
    std::unordered_map<core::OperId, size_t> contracts;

    size_t contract(core::OperId id) {
        if (const auto found = contracts.find(id); found != contracts.end())
            return found->second;
        auto &table = core::OperatorTable::instance();
        Program::OperatorContract snapshot{.uri = std::string(table.uri(id))};
        if (const auto *semantics = table.semantics(id)) {
            if (semantics->key.major != 1)
                throw std::runtime_error(
                    "unsupported operator semantic version " + semantics->key.toString());
            snapshot.key         = semantics->key;
            snapshot.declaration = semantics;
        }
        const auto index = program.contracts.size();
        program.contracts.push_back(std::move(snapshot));
        contracts.emplace(id, index);
        return index;
    }

    size_t append(Node node) {
        if (program.nodes.size() >= 4096)
            throw std::runtime_error("tiles graph budget exceeded (4096 values)");
        elements(node.shape);
        program.nodes.push_back(std::move(node));
        return program.nodes.size() - 1;
    }

    Value host(slot_t slot, ty::Type *type, bool runtimeInput = false) {
        if (type->code() == ty::TypeCode::Function) {
            auto *fn = td::fromSlot<::Function *>(slot);
            if (!fn || !fn->graph())
                throw std::runtime_error("missing function value");
            if (loadingFunctions.size() >= 64 || !loadingFunctions.insert(fn).second)
                throw std::runtime_error("cyclic or excessively deep captured function values");
            struct CaptureScope {
                std::unordered_set<::Function *> &active;
                ::Function *function;
                ~CaptureScope() { active.erase(function); }
            } captureScope{loadingFunctions, fn};
            Value value{.type = type, .function = fn->graph(), .functionProgram = fn->program()};
            if (auto *captures = fn->tuple()) {
                for (size_t i = 0; i < captures->size(); ++i) {
                    auto *fieldType  = fn->tupleType()->typeAt(i);
                    const auto field = captures->get<slot_t>(i);
                    value.fields.push_back(
                        field == NullSlot && ty::isGCTraced(fieldType->code())
                            ? Value{.type = fieldType}
                            : host(field, fieldType, true));
                }
            }
            return value;
        }
        if (asDeviceType(type)) {
            auto *wrapper = td::fromSlot<td::ForeignResourceObject *>(slot);
            auto resident = *wrapper->resourceAs<std::shared_ptr<DeviceValue>>();
            Node node{.op = "input", .shape = resident->shape};
            node.dtype      = resident->dtype;
            node.resident   = std::move(resident);
            ty::Type *exact = node.dtype == DType::F64
                                  ? ty::Type::Float64()
                                  : tensor::TensorType::get(ty::TypeCode::Float32, node.shape);
            return {.type = exact, .tensor = append(std::move(node))};
        }
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
                        : host(field, fieldType, runtimeInput));
            }
            return value;
        }
        if (tensor::asTensorType(type)) {
            const auto *t = td::fromSlot<tensor::Array *>(slot);
            if (!t || t->dtype() != ty::TypeCode::Float32)
                throw UnsupportedComputation("tiles currently requires f32 tensors");
            Node node{.op = "input", .shape = t->shapeVector()};
            const size_t n = elements(node.shape);
            if (n)
                node.data.assign(t->dataAs<float>(), t->dataAs<float>() + n);
            auto *exact = tensor::TensorType::get(ty::TypeCode::Float32, node.shape);
            return {.type = exact, .tensor = append(std::move(node))};
        }
        auto constant = tensor::ops::constArgOf(slot, type);
        if (!constant)
            throw UnsupportedComputation("unsupported captured/static value: " + type->toString());
        if (runtimeInput && type->code() == ty::TypeCode::Float64) {
            Node node{.op = "input", .shape = {}};
            node.dtype      = DType::F64;
            node.scalarBits = std::bit_cast<uint64_t>(std::get<double>(*constant));
            return {.type = type, .tensor = append(std::move(node))};
        }
        return {.type = type, .constant = std::move(constant), .staticSlot = slot};
    }

    int64_t integer(const Value &v) {
        if (!v.constant || !std::holds_alternative<int64_t>(*v.constant))
            throw std::runtime_error(
                "an integer attribute must be known when specializing the graph");
        return std::get<int64_t>(*v.constant);
    }

    // A synthesized shape/permutation is a host-only specialization attribute.
    // The core instantiator reads its temporary slot under extraction's no-GC scope;
    // neither the slot nor the array escapes into the owned Program.
    Value integerArray(const std::vector<int64_t> &values) {
        auto *array = ::Array::create(
            tensor::ops::resultAllocator(),
            ty::ArrayType::create(ty::Type::Int64()),
            values.size());
        for (size_t i = 0; i < values.size(); ++i)
            array->set<td::Int64>(i, values[i]);
        return host(td::toSlot<::Array *>(array), ty::ArrayType::create(ty::Type::Int64()));
    }
    size_t tensorValue(const Value &v) {
        if (v.tensor != SIZE_MAX) {
            if (program.nodes[v.tensor].dtype == DType::F64)
                return append(Node{.op = "cast_f32", .shape = {}, .inputs = {v.tensor}});
            return v.tensor;
        }
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
        const std::vector<Value> &captures = {}, const rt::Program *home = nullptr) {
        // The graphs `g` calls are named in the program it is of.
        struct HomeScope {
            const rt::Program *&current;
            const rt::Program *saved;
            ~HomeScope() { current = saved; }
        } homeScope{runtimeProgram, runtimeProgram};
        if (home)
            runtimeProgram = home;
        if (!g || active.contains(g) || active.size() >= 64)
            throw UnsupportedComputation(
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

    Value placeholder(ty::Type *type) {
        Node node{.op = "input"};
        if (type->code() == ty::TypeCode::Float64)
            node.dtype = DType::F64;
        else {
            const auto *t = tensor::asTensorType(type);
            if (!t || t->dtype() != ty::TypeCode::Float32 || !t->isStaticShape())
                throw UnsupportedComputation(
                    "loop values require f64 scalars or static f32 tensors");
            node.shape = *t->shape();
        }
        return {.type = type, .tensor = append(std::move(node))};
    }
    size_t materialize(const Value &value) {
        if (value.tensor != SIZE_MAX)
            return value.tensor;
        if (value.constant && std::holds_alternative<double>(*value.constant))
            return append(
                Node{
                    .op         = "constant64",
                    .attributes = {std::bit_cast<int64_t>(std::get<double>(*value.constant))},
                    .dtype      = DType::F64});
        throw UnsupportedComputation("loop state/capture must be a tensor or f64 value");
    }
    Value loop(Frame &frame, rt::gc_node_ref_t id) {
        const rt::LoopView view(*runtimeProgram, *frame.graph, id);
        if (view.kind() != core::LoopKind::For || view.stateCount() == 0 ||
            !view.bodyEffects().pure())
            throw UnsupportedComputation("tiles requires a pure static For loop");
        // The body is a region of the graph that has the node.
        auto *body = frame.graph;
        const rt::fact::Anchor anchors[]{
            {runtimeProgram, frame.graph, id},
            {runtimeProgram, body, *view.bodyElementParam()}};
        const auto domain =
            facts.obtain<rt::fact::LoopDomain>(rt::fact::coreKinds().loopDomain, anchors);
        if (!domain || !domain->extent || *domain->extent < 0 ||
            body->node(*view.bodyElementParam())->dataType()->code() != ty::TypeCode::Int64)
            throw UnsupportedComputation("tiles loop requires a static int[] domain");
        const auto iterable = eval(frame, *view.iterable());
        if (!iterable.constant || !std::holds_alternative<std::vector<int64_t>>(*iterable.constant))
            throw UnsupportedComputation("tiles loop requires exact static int[] values");
        auto region    = std::make_shared<LoopRegion>();
        region->domain = std::get<std::vector<int64_t>>(*iterable.constant);
        if (region->domain.size() != static_cast<size_t>(*domain->extent))
            throw std::runtime_error("core loop domain disagrees with static array");
        Compiler nested(facts, runtimeProgram);
        nested.active = active;
        std::vector<Value> initials, states, captures;
        std::vector<size_t> inputs;
        for (size_t i = 0; i < view.stateCount(); ++i) {
            auto initial = eval(frame, view.initialState(i));
            inputs.push_back(materialize(initial));
            auto state = nested.placeholder(initial.type);
            region->stateInputs.push_back(state.tensor);
            initials.push_back(std::move(initial));
            states.push_back(std::move(state));
        }
        const std::vector<rt::gc_node_ref_t> outerRefs = view.externalInputs(true);
        for (auto ref : outerRefs) {
            const auto outer = eval(frame, ref);
            inputs.push_back(materialize(outer));
            auto inner = nested.placeholder(outer.type);
            region->captureInputs.push_back(inner.tensor);
            captures.push_back(std::move(inner));
        }
        // One activation is lowered once. The device loop carries the recurrence;
        // Compiler::call is never repeated according to the domain's trip count.
        Frame activation{.graph = body};
        activation.values.emplace(
            *view.bodyElementParam(),
            Value{.type = body->node(*view.bodyElementParam())->dataType(), .loopElement = true});
        for (size_t i = 0; i < states.size(); ++i)
            activation.values.emplace(view.bodyStateParam(i), states[i]);
        for (size_t c = 0; c < captures.size(); ++c)
            activation.values.emplace(outerRefs[c], captures[c]);
        // Visit the statements of the body too: an unused println in a sync body must not
        // disappear.
        nested.eval(activation, view.bodyResult());
        auto result = nested.eval(activation, view.nextState());
        if (states.size() == 1)
            region->nextStates.push_back(nested.materialize(result));
        else {
            if (result.fields.size() != states.size())
                throw std::runtime_error("loop next-state tuple arity mismatch");
            for (const auto &field : result.fields)
                region->nextStates.push_back(nested.materialize(field));
        }
        region->body         = std::move(nested.program);
        region->body.outputs = region->nextStates;
        const auto &source   = program.nodes[inputs.front()];
        Node node{
            .op     = "loop",
            .shape  = source.shape,
            .inputs = std::move(inputs),
            .dtype  = source.dtype};
        for (auto input : node.inputs) {
            if (node.results.size() == states.size())
                break;
            const auto &value = program.nodes[input];
            if (value.shape != node.shape || value.dtype != node.dtype)
                throw UnsupportedComputation(
                    "tiles loop states require identical lane domains and dtypes");
            node.results.push_back({value.shape, value.dtype});
        }
        node.loop = std::move(region);
        // A legal graph outside the initial lane-local subset remains a CPU region.
        // Structural/contract validation below still reports actual malformed IR.
        for (const auto &value : node.loop->body.nodes) {
            const auto &op       = value.op;
            const bool supported = op == "input" || op == "constant64" || op == "loop_index_f64" ||
                                   op == "cast_f32" || op == "scalar_add" || op == "scalar_sub" ||
                                   op == "scalar_mul" || op == "scalar_div" || op == "scalar_neg" ||
                                   elementwise(op);
            if (!supported || value.loop || (!value.shape.empty() && value.shape != node.shape) ||
                (elementwise(op) && !kernel::canonicalPointwise(node.loop->body, value)) ||
                (op == "input" && !value.data.empty() && !value.shape.empty()))
                throw UnsupportedComputation(
                    "tiles loop body is not a supported lane-local recurrence");
            // Scalar expressions still execute when a tensor has no elements. Without a
            // lane to report their fault, keep this recurrence on the CPU instead.
            if (op == "scalar_div" && !node.loop->domain.empty() && elements(node.shape) == 0)
                throw UnsupportedComputation("tiles loop needs a lane to preserve scalar faults");
        }
        kernel::validateLoopRegion(program, node);
        const auto producer = append(std::move(node));
        Value output{.type = view.valueType()};
        for (size_t i = 0; i < states.size(); ++i) {
            const auto &type = program.nodes[producer].results[i];
            auto result      = Value{
                .type   = initials[i].type,
                .tensor = append(
                    Node{
                        .op         = "result",
                        .shape      = type.shape,
                        .inputs     = {producer},
                        .attributes = {static_cast<int64_t>(i)},
                        .dtype      = type.dtype})};
            if (states.size() == 1)
                return result;
            output.fields.push_back(std::move(result));
        }
        return output;
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
            case rt::GCNodeKind::Loop:
                out = loop(f, id);
                break;
            case rt::GCNodeKind::Data:
                if (node->dataIndex >= 0)
                    throw std::runtime_error("nonconstant DATA node");
                out =
                    host(g->staticArea()->get<slot_t>(size_t(-node->dataIndex)), node->dataType());
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
            case rt::GCNodeKind::Brch:
                if (!g->withInputsOf(id).empty())
                    throw UnsupportedComputation("tiles match lowering is not implemented");
                out = eval(f, ins.front());
                if (!out.constant || !std::holds_alternative<bool>(*out.constant))
                    throw UnsupportedComputation(
                        "tiles branch condition must be a boolean known when specializing");
                break;
            case rt::GCNodeKind::Join: {
                const auto condition = eval(f, ins.front());
                if (!condition.constant || !std::holds_alternative<bool>(*condition.constant))
                    throw UnsupportedComputation(
                        "tiles join requires a specialized boolean branch");
                const auto arms  = g->withInputsOf(id);
                const size_t arm = std::get<bool>(*condition.constant) ? 0 : 1;
                if (arms.size() != 2)
                    throw std::runtime_error("tiles boolean branch needs two arms");
                // Do not visit or lower the other arm: it may contain an effect,
                // fault, or unsupported operation that is unreachable in this instance.
                out = eval(f, arms[arm]);
                break;
            }
            case rt::GCNodeKind::Cast: {
                out                = eval(f, ins.front());
                const auto *target = tensor::asTensorType(node->dataType());
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
                out = call(runtimeProgram->calleeOf(*g, id), with, norm);
                break;
            }
            case rt::GCNodeKind::Call: {
                const auto context = g->withInputsOf(id);
                if (context.empty())
                    throw std::runtime_error("function call has no callable value");
                auto fn = eval(f, context.front());
                if (!fn.function)
                    throw UnsupportedComputation(
                        "call target must be known during GPU specialization");
                std::vector<Value> with, norm;
                for (size_t i = 1; i < context.size(); ++i)
                    with.push_back(eval(f, context[i]));
                for (auto input : ins)
                    norm.push_back(eval(f, input));
                out = call(fn.function, with, norm, fn.fields, fn.functionProgram);
                break;
            }
            case rt::GCNodeKind::Fill: {
                if (node->dataType()->code() != ty::TypeCode::Tuple &&
                    node->dataType()->code() != ty::TypeCode::Struct &&
                    node->dataType()->code() != ty::TypeCode::Function)
                    throw std::runtime_error(
                        "tiles supports only tuple/struct aggregate construction");
                out               = eval(f, ins.front());
                const auto slots  = g->nodeBodyAs<rt::GCFillBody>(id)->slots();
                const auto values = g->withInputsOf(id);
                if (slots.size() != values.size())
                    throw std::runtime_error("aggregate fill arity mismatch");
                for (size_t i = 0; i < slots.size(); ++i)
                    out.fields.at(slots[i]) = eval(f, values[i]);
                out.type = node->dataType();
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
                const auto oper       = g->nodeBodyAs<rt::GCOperBody>(id)->oper;
                static const auto gpu = core::OperatorTable::instance().intern("tiles:gpu");
                static const auto run = core::OperatorTable::instance().intern("tiles:run");
                static const auto prefer =
                    core::OperatorTable::instance().intern("tiles:prefer_gpu");
                // Dispatch inside an already GPU-compiled graph is a call to the original
                // computation. Read it structurally; never execute a decorator or launch CUDA.
                if (oper == gpu) {
                    if (ins.size() != 1 || !g->withInputsOf(id).empty())
                        throw std::runtime_error("invalid GPU decorator arguments");
                    out = eval(f, ins.front());
                    if (!out.function)
                        throw std::runtime_error("GPU decorator requires a known function");
                    break;
                }
                if (oper == run || oper == prefer) {
                    const auto context = g->withInputsOf(id);
                    if (context.size() != 1 || ins.size() != 1)
                        throw std::runtime_error("invalid nested GPU dispatch arguments");
                    const auto fn = eval(f, context.front());
                    if (!fn.function)
                        throw std::runtime_error("GPU dispatch requires a known function");
                    auto args = eval(f, ins.front());
                    out       = call(
                        fn.function,
                        {},
                        fn.function->normPorts().size() == 1 ? std::vector<Value>{args}
                                                                   : args.fields,
                        fn.fields,
                        fn.functionProgram);
                    break;
                }
                std::vector<Value> args;
                if (!g->withInputsOf(id).empty())
                    throw UnsupportedComputation(
                        "operator context arguments are not supported yet");
                for (auto in : ins)
                    args.push_back(eval(f, in));
                const auto first      = program.nodes.size();
                const auto semantic   = contract(oper);
                auto indexed          = graphInstance(*g, id, args);
                ty::Type *knownResult = nullptr;
                const auto inputRefs  = g->normInputsOf(id);
                bool sameTypes        = inputRefs.size() == args.size();
                for (size_t i = 0; sameTypes && i < args.size(); ++i)
                    sameTypes = g->node(inputRefs[i])->dataType() == args[i].type;
                if (sameTypes) {
                    const rt::fact::Anchor anchor{runtimeProgram, g, id};
                    const auto shape = facts.obtain<rt::fact::TensorShape>(
                        rt::fact::coreKinds().shape,
                        {&anchor, 1});
                    if (shape && shape->dtype == ty::TypeCode::Float32 && shape->dims &&
                        std::all_of(shape->dims->begin(), shape->dims->end(), [](auto extent) {
                            return extent >= 0;
                        }))
                        knownResult = node->dataType();
                }
                out = operation(oper, args, indexed, knownResult);
                // Only this operator's lowering inherits its lexical grant.
                // Arguments and separately called functions have already been
                // lowered with their own permissions and must remain unchanged.
                const camel::core::sem::PermissionMask permissions =
                    camel::core::tag::factsOf(*g, id).permissions;
                for (size_t i = first; i < program.nodes.size(); ++i) {
                    program.nodes[i].permissions = permissions;
                    if (program.nodes[i].semantic == SIZE_MAX)
                        program.nodes[i].semantic = semantic;
                }
                // An operator's instance describes its result, never synthetic
                // casts or constants introduced while lowering its arguments.
                if (out.tensor != SIZE_MAX && out.tensor >= first &&
                    !program.nodes[out.tensor].indexing) {
                    program.nodes[out.tensor].indexing = std::move(indexed);
                }
                break;
            }
            default:
                throw UnsupportedComputation(
                    "unsupported graph node kind " + std::to_string(int(node->kind)));
            }
            f.visiting.erase(id);
            f.values.emplace(id, out);
            return out;
        } catch (const UnsupportedComputation &e) {
            throw UnsupportedComputation(
                g->name() + " node " + std::to_string(id) + ": " + e.what());
        } catch (const std::exception &e) {
            throw std::runtime_error(g->name() + " node " + std::to_string(id) + ": " + e.what());
        }
    }

    std::optional<core::sem::IndexedInstance> graphInstance(
        const rt::GCGraph &graph, rt::gc_node_ref_t node, const std::vector<Value> &args) {
        const auto inputs = graph.normInputsOf(node);
        bool sameTypes    = inputs.size() == args.size();
        for (size_t i = 0; sameTypes && i < args.size(); ++i)
            sameTypes = graph.node(inputs[i])->dataType() == args[i].type;
        if (sameTypes) {
            const rt::fact::Anchor anchor{runtimeProgram, &graph, node};
            const auto fact =
                facts.obtain<rt::fact::NodeInstance>(rt::fact::coreKinds().instance, {&anchor, 1});
            if (fact && fact->instance &&
                std::all_of(
                    fact->instance->domain.params.begin(),
                    fact->instance->domain.params.end(),
                    [](const auto &param) { return param.value.has_value(); })) {
                ++program.graphFactInstances;
                return fact->instance; // Owned copy; no graph/store pointer survives extraction.
            }
        }
        // A shared graph's unknowns must not be filled in with one invocation's
        // shapes. Specialize separately and keep the result in the owned plan.
        return instance(graph.nodeBodyAs<rt::GCOperBody>(node)->oper, args);
    }

    std::optional<core::sem::IndexedInstance>
    instance(core::OperId id, const std::vector<Value> &args) {
        std::vector<ty::Type *> types;
        std::vector<ty::StaticArg> statics;
        for (const auto &arg : args) {
            types.push_back(arg.type);
            statics.emplace_back(arg.staticSlot);
        }
        // Use the declaration in the core operator table, including its current
        // law validation, rather than bypassing it through Tensor's private IndexFn.
        auto result = core::sem::instantiate(id, {.norm = types, .statics = statics});
        if (result)
            ++program.specializedInstances;
        return result;
    }

    Value operation(
        core::OperId id, const std::vector<Value> &args,
        const std::optional<core::sem::IndexedInstance> &indexed, ty::Type *knownResult = nullptr) {
        static const auto shapeQuery = core::OperatorTable::instance().intern("tensor:shape");
        if (id == shapeQuery && args.size() == 1) {
            const auto input = tensorValue(args[0]);
            return integerArray(program.nodes[input].shape);
        }
        static const auto numel = core::OperatorTable::instance().intern("tensor:numel");
        if (id == numel && args.size() == 1) {
            // Invocation specialization already knows the concrete tensor domain.
            // Keep shape queries as integer attributes, not device tensor results.
            const auto input = tensorValue(args[0]);
            return {
                .type     = ty::Type::Int64(),
                .constant = static_cast<int64_t>(elements(program.nodes[input].shape))};
        }
        static const auto intToFloat = core::OperatorTable::instance().intern(":op/ltod");
        if (id == intToFloat && args.size() == 1) {
            if (args[0].loopElement)
                return {
                    .type   = ty::Type::Float64(),
                    .tensor = append(Node{.op = "loop_index_f64", .dtype = DType::F64})};
            if (args[0].constant && std::holds_alternative<int64_t>(*args[0].constant))
                return {
                    .type   = ty::Type::Float64(),
                    .tensor = append(
                        Node{
                            .op         = "constant64",
                            .attributes = {std::bit_cast<int64_t>(
                                static_cast<double>(std::get<int64_t>(*args[0].constant)))},
                            .dtype      = DType::F64})};
        }
        static const auto scalarOps = [] {
            auto &table = core::OperatorTable::instance();
            return std::unordered_map<core::OperId, std::string>{
                {table.intern(":op/add_d"), "scalar_add"},
                {table.intern(":op/sub_d"), "scalar_sub"},
                {table.intern(":op/mul_d"), "scalar_mul"},
                {table.intern(":op/div_d"), "scalar_div"},
                {table.intern(":op/neg_d"), "scalar_neg"}};
        }();
        auto uri = [&] { return std::string(core::OperatorTable::instance().uri(id)); };
        if (auto op = scalarOps.find(id); op != scalarOps.end()) {
            const size_t arity = op->second == "scalar_neg" ? 1 : 2;
            if (args.size() != arity)
                throw std::runtime_error("scalar operation arity");
            Node node{.op = op->second, .shape = {}, .dtype = DType::F64};
            for (const auto &arg : args) {
                if (!arg.type || arg.type->code() != ty::TypeCode::Float64)
                    throw std::runtime_error("scalar operation requires f64 operands");
                if (arg.tensor != SIZE_MAX) {
                    if (program.nodes[arg.tensor].dtype != DType::F64)
                        throw std::runtime_error("scalar operand storage mismatch");
                    node.inputs.push_back(arg.tensor);
                } else if (arg.constant && std::holds_alternative<double>(*arg.constant)) {
                    node.inputs.push_back(append(
                        Node{
                            .op         = "constant64",
                            .shape      = {},
                            .attributes = {std::bit_cast<int64_t>(std::get<double>(*arg.constant))},
                            .dtype      = DType::F64}));
                } else
                    throw std::runtime_error("scalar operand is unavailable");
            }
            return {.type = ty::Type::Float64(), .tensor = append(std::move(node))};
        }
        const auto *def = tensor::ops::OpRegistry::instance().find(id);
        if (!def || !def->traits.pure)
            throw UnsupportedComputation("unsupported or effectful operator '" + uri() + "'");
        if (def->name == "permute_inverse" && args.size() == 2) {
            const auto input = tensorValue(args[0]);
            const auto rank  = program.nodes[input].shape.size();
            if (!args[1].constant ||
                !std::holds_alternative<std::vector<int64_t>>(*args[1].constant))
                throw UnsupportedComputation("permute_inverse requires a static permutation");
            const auto &permutation = std::get<std::vector<int64_t>>(*args[1].constant);
            if (permutation.size() != rank)
                throw std::runtime_error("permute_inverse: permutation rank mismatch");
            std::vector<int64_t> inverse(rank, -1);
            for (size_t i = 0; i < rank; ++i) {
                const auto axis = permutation[i] < 0 ? permutation[i] + static_cast<int64_t>(rank)
                                                     : permutation[i];
                if (axis < 0 || static_cast<size_t>(axis) >= rank || inverse[axis] != -1)
                    throw std::runtime_error("permute_inverse: invalid permutation");
                inverse[axis] = static_cast<int64_t>(i);
            }
            static const auto permute = core::OperatorTable::instance().intern("tensor:permute");
            std::vector<Value> normalized{args[0], integerArray(inverse)};
            auto result = operation(permute, normalized, instance(permute, normalized));
            // Keep the original derivative contract and the physical view's contract.
            program.nodes[result.tensor].semantic = contract(permute);
            return result;
        }
        std::vector<ty::Type *> types;
        std::vector<std::optional<ConstArg>> constants;
        for (const auto &v : args) {
            types.push_back(v.type);
            constants.push_back(v.constant);
        }
        // Static graph types have already run the module's resolver. Read their
        // core shape fact; only invocation specialization needs to infer again.
        auto inferred = knownResult ? std::optional<ty::Type *>(knownResult)
                                    : def->infer(tensor::ops::InferContext(types, constants));
        if (inferred && (*inferred)->code() == ty::TypeCode::Float64 &&
            (def->name == "sum" || def->name == "mean")) {
            size_t input = tensorValue(args.at(0));
            return {
                .type   = *inferred,
                .tensor = append(
                    Node{
                        .op     = def->name == "sum" ? "sum_all" : "mean_all",
                        .shape  = {},
                        .inputs = {input},
                        .dtype  = DType::F64})};
        }
        const auto *result = inferred ? tensor::asTensorType(*inferred) : nullptr;
        if (!result || result->dtype() != ty::TypeCode::Float32 || !result->isStaticShape())
            throw UnsupportedComputation(
                "operator '" + uri() + "' needs a statically known f32 tensor result");
        Node n{.op = std::string(def->name), .shape = *result->shape()};
        if (n.op == "sum_to") {
            n.inputs = {tensorValue(args.at(0))};
            if (program.nodes[n.inputs[0]].shape == n.shape)
                return {.type = *inferred, .tensor = n.inputs[0]};
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        if (n.op == "cast" || n.op == "cast_like") {
            // `cast_like` keeps the source's shape but takes `like`'s element type; `cast`
            // names its target type in the second argument. The check above leaves only f32
            // results, and `tensorValue` has already converted an f64 scalar source, so the
            // source is f32 whenever the target is: returning it is then the documented
            // "converting a value to its own type does nothing". A different element type
            // would have to convert, which this backend cannot represent yet, so the region
            // stays on the CPU, where the tensor kernel converts
            // (notes/core-semantics-map.md, "cast_like"). A scalar source is not the same
            // value as the 0-d tensor this result has: keep that on the CPU as well.
            if (!tensor::asTensorType(args.at(0).type))
                throw UnsupportedComputation("'" + n.op + "' needs a tensor operand here");
            n.inputs           = {tensorValue(args.at(0))};
            const Node &source = program.nodes[n.inputs[0]];
            if (source.shape != n.shape)
                throw std::runtime_error(n.op + " shape mismatch");
            if (source.dtype == n.dtype)
                return {.type = *inferred, .tensor = n.inputs[0]};
            throw UnsupportedComputation(
                "element type conversion in '" + n.op + "' is not supported");
        }
        if (n.op == "broadcast_like" || n.op == "expand_axis") {
            n.inputs   = {tensorValue(args.at(0))};
            n.op       = "reindex";
            n.indexing = indexed;
            if (!n.indexing)
                throw UnsupportedComputation("view operator lacks a concrete core index instance");
            (void)kernel::coreViewAddress(program, n, kernel::IndexExpr(0));
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        if (n.op == "ones_like" || n.op == "zeros_like") {
            // These source operators do not yet declare index instances. Their
            // scalar-fill physical primitive reads the one embedded constant.
            const auto input =
                append(Node{.op = "input", .data = {n.op == "ones_like" ? 1.0f : 0.0f}});
            n.op     = "reindex";
            n.inputs = {input};
            n.strides.assign(n.shape.size(), 0);
            return {.type = *inferred, .tensor = append(std::move(n))};
        }
        if (n.op == "matmul_grad_lhs" || n.op == "matmul_grad_rhs") {
            const auto a = program.nodes[tensorValue(args.at(1))].shape;
            const auto b = program.nodes[tensorValue(args.at(2))].shape;
            if (a.size() < 2 || b.size() < 2)
                throw UnsupportedComputation("tiles matmul gradient requires rank >= 2");
            // Compose existing tensor primitives, retaining their core instances
            // and contracts rather than inventing a separate batched index law.
            auto apply = [&](const char *uri, std::vector<Value> values) {
                const auto op       = core::OperatorTable::instance().intern(uri);
                const auto first    = program.nodes.size();
                const auto indexed  = instance(op, values);
                auto value          = operation(op, values, indexed);
                const auto semantic = contract(op);
                for (size_t i = first; i < program.nodes.size(); ++i)
                    if (program.nodes[i].semantic == SIZE_MAX)
                        program.nodes[i].semantic = semantic;
                if (value.tensor >= first && !program.nodes[value.tensor].indexing)
                    program.nodes[value.tensor].indexing = indexed;
                return value;
            };
            const bool left = n.op == "matmul_grad_lhs";
            Value lhs = args[1], rhs = args[2], dy = args[0];
            if (!left && b.size() == 2) {
                // Match the Tensor CPU gradient's canonical GEMM: a shared RHS
                // accumulates over batch and row in one ascending FMA chain.
                // Per-batch GEMMs followed by sum_to would round differently.
                const auto rows =
                    static_cast<int64_t>(elements(a) / std::max<int64_t>(a.back(), 1));
                lhs = apply("tensor:reshape", {lhs, integerArray({rows, a.back()})});
                dy  = apply("tensor:reshape", {dy, integerArray({rows, b.back()})});
            }
            auto transposed = apply("tensor:transpose", {left ? rhs : lhs});
            auto product    = apply(
                "tensor:matmul",
                left ? std::vector<Value>{dy, transposed} : std::vector<Value>{transposed, dy});
            return apply("tensor:sum_to", {product, args[left ? 1 : 2]});
        }
        const std::string unary  = "|neg|abs|exp|log|sqrt|rsqrt|relu|";
        const std::string binary = "|add|subtract|multiply|divide|maximum|minimum|";
        if (unary.find("|" + n.op + "|") != std::string::npos) {
            n.inputs = {tensorValue(args.at(0))};
        } else if (binary.find("|" + n.op + "|") != std::string::npos || n.op == "matmul") {
            n.inputs = {tensorValue(args.at(0)), tensorValue(args.at(1))};
            if (n.op == "matmul" && (program.nodes[n.inputs[0]].shape.size() < 2 ||
                                     program.nodes[n.inputs[1]].shape.size() < 2))
                throw UnsupportedComputation("tiles matmul requires tensors with rank >= 2");
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
            n.inputs   = {tensorValue(args.at(0))};
            n.indexing = indexed;
            n.op       = "reindex";
            if (!n.indexing)
                throw UnsupportedComputation(
                    "layout operator lacks a concrete core index instance");
            (void)kernel::coreViewAddress(program, n, kernel::IndexExpr(0));
        } else if (n.op == "reshape") {
            n.inputs = {tensorValue(args.at(0))};
            if (elements(program.nodes[n.inputs[0]].shape) != elements(n.shape))
                throw std::runtime_error("reshape changes the number of elements");
        } else {
            throw UnsupportedComputation("no CUDA lowering for '" + uri() + "'");
        }
        return {.type = *inferred, .tensor = append(std::move(n))};
    }
};
} // namespace

// Read-only capability probing uses the very same recurrence compiler with deferred
// boundary inputs. It creates no graph, function, CUDA context, or retained GC reference.
std::string loopEligibility(
    const rt::Program &program, const rt::GCGraph &graph, rt::gc_node_ref_t id,
    rt::fact::FactStore &facts) {
    try {
        Compiler compiler(facts, &program);
        Frame frame{.graph = const_cast<rt::GCGraph *>(&graph)};
        const rt::LoopView view(program, graph, id);
        if (view.kind() != core::LoopKind::For || view.stateCount() == 0)
            return "unsupported_loop_form";
        for (size_t i = 0; i < view.stateCount(); ++i) {
            const auto ref = view.initialState(i);
            frame.values.emplace(ref, compiler.placeholder(graph.node(ref)->dataType()));
        }
        const std::vector<rt::gc_node_ref_t> outerRefs = view.externalInputs(true);
        for (auto ref : outerRefs)
            frame.values.emplace(ref, compiler.placeholder(graph.node(ref)->dataType()));
        (void)compiler.loop(frame, id);
        return {};
    } catch (const UnsupportedComputation &) {
        return "unsupported_loop_body_or_domain";
    }
}

static Program compileInputs(
    Function *function, const std::vector<Argument> &arguments, rt::fact::FactStore &facts,
    bool fromTypes) {
    if (!function || !function->graph())
        throw std::runtime_error("expected a function graph");
    ++extractions;
    Compiler c(facts, function->program());
    std::vector<Value> with, norm;
    if (auto *closure = function->tuple()) {
        const auto *types = function->tupleType();
        if (!types)
            throw std::runtime_error("closure type is missing");
        for (size_t i = 0; i < closure->size(); ++i)
            with.push_back(c.host(closure->get<slot_t>(i), types->typeAt(i), true));
    }
    if (fromTypes) {
        if (!with.empty())
            throw UnsupportedComputation(
                "type-only compilation requires an extracted closure-free region");
        auto *signature = function->graph()->funcType();
        for (size_t i = 0; i < signature->normTypesCount(); ++i) {
            auto *type = signature->normTypeAt(i);
            Node input{.op = "input"};
            if (type->code() == ty::TypeCode::Float64) {
                input.dtype = DType::F64;
            } else {
                const auto *tensor = tensor::asTensorType(type);
                if (!tensor || tensor->dtype() != ty::TypeCode::Float32 || !tensor->isStaticShape())
                    throw UnsupportedComputation(
                        "type-only compilation requires static f32 tensor shapes");
                input.shape = *tensor->shape();
            }
            const auto node = c.append(std::move(input));
            c.program.parameterInputs.push_back(node);
            norm.push_back(Value{.type = type, .tensor = node});
        }
    } else {
        for (const auto &a : arguments) {
            auto input = c.host(a.value, a.type, true);
            if (input.tensor != SIZE_MAX)
                c.program.parameterInputs.push_back(input.tensor);
            norm.push_back(std::move(input));
        }
    }
    auto out = c.call(function->graph(), {}, norm, with);
    c.program.outputs.clear();
    auto collect = [&](auto &&self, const Value &value) -> void {
        if (value.tensor != SIZE_MAX) {
            c.program.outputs.push_back(value.tensor);
        } else if (value.constant && std::holds_alternative<double>(*value.constant)) {
            c.program.outputs.push_back(c.append(
                Node{
                    .op         = "constant64",
                    .shape      = {},
                    .attributes = {std::bit_cast<int64_t>(std::get<double>(*value.constant))},
                    .dtype      = DType::F64}));
        } else if (
            value.type &&
            (value.type->code() == ty::TypeCode::Tuple ||
             value.type->code() == ty::TypeCode::Struct) &&
            !value.fields.empty()) {
            for (const auto &field : value.fields)
                self(self, field);
        } else {
            throw std::runtime_error(
                "tiles result must contain tensors or f64 scalars in nonempty aggregates");
        }
    };
    collect(collect, out);
    generate(c.program, {}, fromTypes ? InputBindings::Deferred : InputBindings::Required);
    return std::move(c.program);
}
Program
compile(Function *function, const std::vector<Argument> &arguments, rt::fact::FactStore &facts) {
    return compileInputs(function, arguments, facts, false);
}
size_t compilationExtractions() { return extractions; }
Program compileRegionTemplate(Function *function, rt::fact::FactStore &facts) {
    return compileInputs(function, {}, facts, true);
}
void bindRegionArguments(
    Program &program, const std::vector<Argument> &arguments, rt::fact::FactStore &facts) {
    if (arguments.size() != program.parameterInputs.size())
        throw std::runtime_error("compiled region argument count changed");
    // Reuse the ordinary input snapshot implementation; no graph node, operator
    // instantiation, inference, scheduling or source generation is visited here.
    Compiler bindings(facts, nullptr);
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto &argument = arguments[i];
        const auto value     = bindings.host(argument.value, argument.type, true);
        if (value.tensor == SIZE_MAX)
            throw std::runtime_error("compiled region requires tensor or f64 input");
        auto input   = std::move(bindings.program.nodes.at(value.tensor));
        auto &target = program.nodes.at(program.parameterInputs[i]);
        if (input.shape != target.shape || input.dtype != target.dtype)
            throw std::runtime_error("compiled region input shape or dtype changed");
        target.data       = std::move(input.data);
        target.scalarBits = input.scalarBits;
        target.resident   = std::move(input.resident);
    }
    // Allocation needs depend on host versus resident ownership. The compiled
    // launches and arithmetic are immutable; only this invocation owns inputs.
    planStorage(program);
}
} // namespace camel::tiles

