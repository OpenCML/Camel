// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Classify static core-indexed operations, then partition the complete dependency DAG.

#include "camel/core/operator_semantics.h"
#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/loop.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include "tiles/internal/dtype_map.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include "tiles/program.h"
#include <algorithm>
#include <span>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace camel::tiles {
namespace {
namespace rt  = runtime;
namespace sem = core::sem;
namespace ty  = core::type;
/// The element types a device tensor can be read as: the two the device computes with today -- an
/// f32 value and the i64 position an index answers in -- and the two narrow floats `DType` names.
/// This is the type gate, and it asks one question: does the device path know the type at all. A
/// tensor of another element type (an f64 tensor) has no width here and stays unresolved.
bool tensorElement(ty::TypeCode code) {
    return code == ty::TypeCode::Float32 || code == ty::TypeCode::Int64 ||
           code == ty::TypeCode::BFloat16 || code == ty::TypeCode::Float16;
}

/// Whether the element type is one of the two narrow floats: 16-bit types the device path reads and
/// names (`tiles/dtype.h`), and this back end carries no kernel for one, so a narrow node stays on
/// the host and the plan says so by name.
bool narrowElement(DType dtype) { return dtype == DType::F16 || dtype == DType::BF16; }

/// Whether the value's element type is a narrow float, read from the same facts the shape is.
bool narrowElementType(ty::Type *type) {
    if (!type)
        return false;
    const tensor::TensorFacts facts = tensor::tensorFactsOf(type);
    if (!facts.dtype)
        return false;
    const auto element = dtypeOfElementType(*facts.dtype);
    return element && narrowElement(*element);
}

std::optional<Shape> shape(ty::Type *type) {
    if (!type)
        return std::nullopt;
    if (type->code() == ty::TypeCode::Float64)
        return Shape{};
    const tensor::TensorFacts facts = tensor::tensorFactsOf(type);
    // A narrowly typed tensor reads as a shape here, because the device names the type and its
    // width: what is not there for a narrow element is the kernel, and a refusal that says the type
    // cannot be read would blame the wrong half (`eligibility` says which one below).
    if (!facts.dtype || !tensorElement(*facts.dtype) || !facts.isStaticShape())
        return std::nullopt;
    return tensor::extentsOf(*facts.shape);
}
const std::unordered_map<core::OperId, std::string> &operations() {
    static const auto result = [] {
        std::unordered_map<core::OperId, std::string> result;
        auto &table = core::OperatorTable::instance();
        for (const char *name :
             {"neg",
              "abs",
              "exp",
              "log",
              "sqrt",
              "rsqrt",
              "relu",
              "add",
              "subtract",
              "multiply",
              "divide",
              "maximum",
              "minimum",
              "matmul",
              "sum_axis",
              "mean_axis",
              "max_axis",
              "sum_to",
              // The layout operators are not computed: a device program for one is the address the
              // core's own index instance gives (`kernel::coreViewAddress`), so the region reads the
              // operand through it and writes nothing of its own.
              "transpose",
              "permute",
              "reshape",
              "broadcast_like"})
            result.emplace(table.intern(std::string("tensor:") + name), name);
        result.emplace(table.intern("tensor:sum"), "sum_all");
        result.emplace(table.intern("tensor:mean"), "mean_all");
        return result;
    }();
    return result;
}

/// The nodes the chain of a `sync` block holds, either side of it: every node a sync edge names, as
/// the statement written before it or as the one written after. A block writes the order of its
/// statements as a chain (docs/design/effects/edges.cn.md 1), so both ends of an edge are statements
/// of that block -- the first statement of a block stands only before the next one and has no place
/// in the chain itself, and it is a statement all the same.
std::vector<char> chainMembers(const rt::GCGraph &graph) {
    std::vector<char> member(graph.nodeCount(), 0);
    for (const rt::GCGraphSyncEdge &edge : graph.syncEdges()) {
        if (edge.user < member.size()) {
            member[edge.user] = 1;
        }
        if (edge.pred < member.size()) {
            member[edge.pred] = 1;
        }
    }
    return member;
}

/// Whether the node is one the chain holds, read from the set `chainMembers` computed once per graph.
inline bool inChain(const std::vector<char> &chain, rt::gc_node_ref_t id) {
    return id < chain.size() && chain[id] != 0;
}

/// Whether the operation is a layout one: its result is the operand read in another order, so there
/// is no kernel to write for it, only an address (`kernel::coreViewAddress`) to read it through.
bool layoutOperator(const std::string &op) {
    return op == "transpose" || op == "permute" || op == "broadcast_like";
}

std::string eligibility(
    const rt::Program &graphs, const rt::GCGraph &graph, rt::gc_node_ref_t id,
    rt::fact::FactStore &facts, const std::vector<char> &chain, bool device) {
    const auto *node = graph.node(id);
    // Whether the node is a statement of a block, ordered by the edges that order one. The order of
    // a `sync` block is its chain, so a node the chain reaches is a statement as much as one the
    // control kind names (docs/design/effects/edges.cn.md 1). Inside a device scope that ordering is
    // where the node runs, not a reason to leave it on the host: the arm or the block the node is
    // written in is the segment that holds it (docs/design/gpu/plan.cn.md 2.5).
    const bool ordered = !graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty() ||
                         !graph.syncPredsOf(id).empty() || !graph.uses().sync(id).empty() ||
                         inChain(chain, id);
    if (node->kind == rt::GCNodeKind::Loop) {
        if (ordered && !device)
            return "control_edge";
        return loopEligibility(graphs, graph, id, facts);
    }
    if (node->kind != rt::GCNodeKind::Oper)
        return "node_kind";
    if (ordered && !device)
        return "control_edge";
    // The with-arguments of an operator whose kernel takes them after the ordinary ones are
    // operands like the others (`sum<1>(x)` is the kernel's `(x, 1)`).
    if (!rt::kernelArgumentsOf(graph, id).with.empty())
        return "context_arguments";
    auto &table      = core::OperatorTable::instance();
    const auto oper  = graph.nodeBodyAs<rt::GCOperBody>(id)->oper;
    const auto found = operations().find(oper);
    if (found == operations().end())
        return "unsupported_operator";
    if (!table.isPure(oper) || table.hasExternalEffect(oper))
        return "effect";
    const auto *decl = table.semantics(oper);
    if (!decl || decl->key.major != 1)
        return "semantic_declaration";
    const auto output = shape(node->dataType());
    if (!output)
        return "unresolved_type";
    // A narrow element reached this line, so the type gate read it: the device names the type and
    // knows its two bytes. The node is still not planned: this back end has no narrow-float kernel
    // (`tiles/dtype.h`), so nothing here may store a four-byte value into a two-byte element.
    if (narrowElementType(node->dataType()))
        return "no narrow-float kernel on this back end";
    const rt::fact::Anchor anchor{&graphs, &graph, id};
    const auto fact =
        facts.obtain<rt::fact::NodeInstance>(rt::fact::coreKinds().instance, {&anchor, 1});
    if (!fact || !fact->instance || !fact->instance->extents())
        return "unresolved_index_instance";
    const auto &inst = *fact->instance;
    if (!std::all_of(inst.domain.params.begin(), inst.domain.params.end(), [](const auto &p) {
            return p.value.has_value();
        }))
        return "unresolved_index_instance";
    Program program;
    program.contracts = {
        {.uri = std::string(table.uri(oper)), .key = decl->key, .declaration = decl}};
    const auto permissions =
        camel::core::tag::effectivePermissions(graph, id, core::sem::NumericPolicy::explicitOnly());
    // The candidate carries the element type the node was inferred with, read from the same facts
    // the shape came from. Reading it as f32 would plan a four-byte store for an eight-byte
    // position -- the same lie the byte width told before an i64 leaf was readable -- and would
    // read a two-byte narrow element through a four-byte one.
    const tensor::TensorFacts outputFacts = tensor::tensorFactsOf(node->dataType());
    const auto outputElement =
        outputFacts.dtype ? dtypeOfElementType(*outputFacts.dtype) : std::optional<DType>{};
    if (!outputElement)
        return "unresolved_type";
    Node candidate{
        .op          = found->second,
        .shape       = *output,
        .inputs      = {},
        .dtype       = *outputElement,
        .permissions = permissions,
        .semantic    = 0,
        .indexing    = inst};
    const auto inputs = rt::kernelArgumentsOf(graph, id).norm;
    if (inputs.size() != inst.norm.size())
        return "operand_arity";
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inst.norm[i].kind == sem::AccessKind::None)
            continue;
        const auto input = shape(graph.node(inputs[i])->dataType());
        if (!input)
            return "unresolved_input_type";
        // A narrow operand is the same missing kernel read from the other side: the program that
        // would read it is written for a four-byte element, and this one is two.
        if (narrowElementType(graph.node(inputs[i])->dataType()))
            return "no narrow-float kernel on this back end";
        // Current physical tensor kernels take f32 tensor operands; f64 scalar
        // arithmetic has its own lowering and is not selected by this table yet.
        if (graph.node(inputs[i])->dataType()->code() == ty::TypeCode::Float64)
            return "scalar_tensor_operand";
        candidate.inputs.push_back(program.nodes.size());
        program.nodes.push_back(Node{.op = "input", .shape = *input, .inputs = {}});
    }
    try {
        if (layoutOperator(candidate.op)) {
            // One data operand: `broadcast_like`'s second operand only names the shape the result
            // takes, and the source map the core declared for the call reads the first one
            // (modules/tensor/ops/indexing.cpp, broadcastLikeIndexing).
            if (candidate.inputs.empty())
                return "operand_arity";
            Node view   = candidate;
            view.op     = "reindex";
            view.inputs = {candidate.inputs.front()};
            (void)kernel::coreViewAddress(program, view, kernel::IndexExpr(0));
        } else if (candidate.op == "reshape") {
            // A reshape writes no element: it names the same buffer in another shape, and the
            // compiler keeps the buffer only while the element count is the one the result has.
            if (candidate.inputs.size() != 1)
                return "operand_arity";
            if (elements(program.nodes[candidate.inputs.front()].shape) != elements(candidate.shape))
                return "unsupported_index_contract";
        } else if (candidate.op == "sum_to") {
            // `sum_to` names the shape its sum goes to in a second operand that the compiler reads
            // as the result's shape and never as data (compiler.cpp). With that shape already the
            // operand's, it reduces nothing and the instance writes each element once: the same
            // address a view has, and the node the compiler keeps is a copy of it. Otherwise the
            // sum is the reduction the first operand alone describes.
            if (candidate.inputs.empty())
                return "operand_arity";
            Node single = candidate;
            single.inputs = {candidate.inputs.front()};
            if (inst.output.mode == sem::WriteMode::Unique) {
                single.op = "reindex";
                (void)kernel::coreViewAddress(program, single, kernel::IndexExpr(0));
            } else
                (void)kernel::reductionAccess(
                    program,
                    single,
                    kernel::IndexExpr(0),
                    kernel::IndexExpr(0));
        } else if (elementwise(candidate.op)) {
            for (size_t side = 0; side < candidate.inputs.size(); ++side)
                (void)kernel::pointwiseAddress(program, candidate, side, kernel::IndexExpr(0));
        } else if (candidate.op == "matmul") {
            if (candidate.inputs.size() != 2 || program.nodes[0].shape.size() != 2 ||
                program.nodes[1].shape.size() != 2)
                return "matrix_rank";
            kernel::validateMatmulContract(program, candidate);
        } else {
            if (candidate.inputs.size() != 1)
                return "reduction_arity";
            if (candidate.op.ends_with("_axis")) {
                for (size_t d = 0; d < inst.domain.dims.size(); ++d)
                    if (inst.domain.dims[d].role == sem::DimRole::Reduction)
                        candidate.attributes.push_back(d);
                if (candidate.attributes.size() != 1)
                    return "axis_count";
            }
            (void)kernel::reductionAccess(
                program,
                candidate,
                kernel::IndexExpr(0),
                kernel::IndexExpr(0));
        }
    } catch (const std::exception &) {
        return "unsupported_index_contract";
    }
    return {};
}
void ids(std::ostream &out, const std::vector<size_t> &values) {
    out << '[';
    bool first = true;
    for (auto value : values) {
        if (!first)
            out << ',';
        first = false;
        out << value;
    }
    out << ']';
}
} // namespace
bool deviceCapable(core::OperId oper) {
    const auto &table = core::OperatorTable::instance();
    return operations().find(oper) != operations().end() && table.isPure(oper) &&
           !table.hasExternalEffect(oper);
}
const char *
gpuRefusal(const rt::GCGraph &graph, const SegmentAnalysis &analysis, rt::gc_node_ref_t id) {
    if (id >= analysis.exclusions.size())
        return "";
    const std::string &reason = analysis.exclusions[id];
    if (reason.empty() || !graph.node(id) || graph.node(id)->kind != rt::GCNodeKind::Oper)
        return "";
    const auto oper   = graph.nodeBodyAs<rt::GCOperBody>(id)->oper;
    const auto &table = core::OperatorTable::instance();
    // A host effect is the boundary of a device scope, not an operator the device must run
    // The pass leaves it to the host and this refusal ignores it.
    if (!table.isPure(oper) || table.hasExternalEffect(oper))
        return "";
    if (reason == "unsupported_operator") {
        // Not in the device capability list: no module offers a device program for it.
        return "the module provides none";
    }
    if (reason == "semantic_declaration" || reason == "context_arguments") {
        // The operator or the call convention has no lowering, at any shape.
        return "no device program for this operator";
    }
    if (reason == "matrix_rank" || reason == "scalar_tensor_operand" || reason == "axis_count" ||
        reason == "operand_arity" || reason == "reduction_arity" ||
        reason == "unsupported_index_contract") {
        // A lowering exists, but not for this shape or element type; a run with actual
        // arguments cannot change a static one.
        return "no device program for this shape";
    }
    // Unresolved types and index instances are not refusals: the run retries with the actual
    // shapes and decides then; structural host code (`node_kind`, `control_edge`, `inside_loop`)
    // is not an operator's business.
    //
    // A narrow element is not a refusal either, and for a different reason: the type is read and
    // its width is known, so what is missing is this back end's kernel for it, not a program the
    // device turned down. The node stays on the host and the plan prints the reason; refusing the
    // whole `#gpu` scope over a kernel the back end does not carry would say the scope is wrong,
    // which it is not.
    return "";
}
// The reading the residency conclusion is fed with: the graph's placements, and the blocks its
// signature declares. A parameter is the caller's value, and the signature says which value that
// is: `Device<T>` asserts one that lives on the device already, an ordinary declaration asserts the
// host's -- the runtime enforces the declaration when it checks the call, and a resident kernel
// reads the value it is given by that type. The graph's own value is what its exit type declares,
// in the same way: an ordinary exit type hands the caller an ordinary value, a `Device<T>` one
// leaves the value on the device. A `#gpu` scope is not a declaration of its signature: the
// operators of its body are declared over ordinary values, so its parameters arrive as the caller's
// ordinary values and its value leaves as one, whatever block the body works in.
//
// A call that hands a value to an ordinary parameter is therefore a host boundary: the call writes
// the value back, and the callee writes it to the device at its own entry. A call whose callee
// declares device parameters is no boundary: the call travels with the block of its argument.
//
// The reading sits here, with the graph reader, because it asks the type table for the
// declarations (`asDeviceType`), which only the module links.
namespace {

namespace rt = runtime;

/// Whether a parameter of `graph` is declared as a device value.
bool parameterIsDevice(const rt::GCGraph &graph, rt::gc_node_ref_t port) {
    const rt::GCNode *node = graph.node(port);
    return node != nullptr && asDeviceType(node->dataType()) != nullptr;
}

/// Whether a call node hands a value to a parameter the callee declares as an ordinary value. A
/// callee only the run knows reaches no declaration, and is a host boundary too.
bool passesOrdinaryArgument(
    const rt::Program &program, const rt::GCGraph &caller, rt::gc_node_ref_t call) {
    const rt::GCGraph *callee = calleeGraphOf(program, caller, call);
    if (callee == nullptr)
        return true;
    for (auto port : callee->normPorts())
        if (!parameterIsDevice(*callee, port))
            return true;
    return false;
}

/// Whether the host observes what the graph returns: its exit type declares an ordinary value, and
/// the graph hands the caller one. A `Device<T>` exit type hands it a value that stays on the
/// device.
bool returnsToHost(const rt::GCGraph &graph) {
    const auto *type = graph.funcType();
    return type == nullptr || !type->hasExitType() || asDeviceType(type->exitType()) == nullptr;
}

/// Whether the node takes what it is given where it lies: a call whose callee declares device
/// parameters travels with the argument's block, so the call writes nothing back.
bool readsInPlace(
    const rt::Program &program, const rt::GCGraph &graph, const rt::GCNode &node,
    rt::gc_node_ref_t ref) {
    if (node.kind != rt::GCNodeKind::Func && node.kind != rt::GCNodeKind::Call)
        return false;
    return !passesOrdinaryArgument(program, graph, ref);
}

} // namespace

ResidencyPlan planResidency(
    const rt::Program &program, const rt::GCGraph &graph, const std::vector<RegionNode> &nodes,
    const std::vector<Segment> &regions) {
    ResidencyFacts facts;
    facts.values.resize(nodes.size());
    facts.returnsToHost = returnsToHost(graph);
    for (size_t ref = 0; ref < nodes.size(); ++ref) {
        const auto id       = static_cast<rt::gc_node_ref_t>(ref);
        const rt::GCNode *node = graph.node(id);
        if (node == nullptr)
            continue;
        auto &value = facts.values[ref];
        if (node->kind == rt::GCNodeKind::Port)
            value.block.placement = parameterIsDevice(graph, id) ? Placement::Gpu : Placement::Host;
        else
            value.block = placementOf(graph, id);
        value.readsInPlace = readsInPlace(program, graph, *node, id);
    }
    return planResidency(facts, nodes, regions);
}

SegmentAnalysis analyzeRegions(
    const rt::Program &graphs, const rt::GCGraph &graph, rt::fact::FactStore &facts,
    const GpuScopes *scopes) {
    std::vector<RegionNode> nodes(graph.nodeCount());
    std::vector<std::string> reasons(nodes.size());
    // Where each node runs (docs/design/gpu/plan.cn.md 2.5): the body, an arm of a BRCH, or a part of a LOOP.
    const rt::ControlRegions control(graph);
    // The statements of the `sync` blocks, read once for the whole graph.
    const std::vector<char> chain = chainMembers(graph);
    // The nodes of the condition and of the body of a loop are the loop's: a region that holds
    // the LOOP node holds them, and what they read from outside is read by the LOOP node.
    std::vector<bool> insideLoop(nodes.size(), false);
    std::vector<std::vector<rt::gc_node_ref_t>> loopReads(nodes.size());
    for (rt::gc_node_ref_t id = 0; id < graph.nodeCount(); ++id) {
        if (graph.node(id)->kind != rt::GCNodeKind::Loop) {
            continue;
        }
        const rt::LoopView view(graphs, graph, id);
        for (auto inner : view.regionNodes()) {
            insideLoop[inner] = true;
        }
        // Every value the regions read, constants included: this is dependency analysis (was the
        // read computed outside the loop?), not the loop's capture list. A constant is not a
        // capture and is evaluated in place (LoopView::externalInputs, default without it).
        loopReads[id] = view.externalInputs(true);
    }
    for (rt::gc_node_ref_t id = 0; id < graph.nodeCount(); ++id) {
        if (insideLoop[id]) {
            reasons[id] = "inside_loop";
            continue;
        }
        const bool device  = scopes != nullptr && scopes->covers(graph, id);
        reasons[id]        = eligibility(graphs, graph, id, facts, chain, device);
        nodes[id].eligible = reasons[id].empty();
        nodes[id].embedded =
            graph.node(id)->kind == rt::GCNodeKind::Data && graph.node(id)->dataIndex < 0;
        nodes[id].control = control.controlOf(id);
        if (graph.node(id)->kind == rt::GCNodeKind::Loop) {
            for (auto edges : {graph.normInputsOf(id)})
                nodes[id].inputs.insert(nodes[id].inputs.end(), edges.begin(), edges.end());
            nodes[id].inputs.insert(
                nodes[id].inputs.end(),
                loopReads[id].begin(),
                loopReads[id].end());
        } else {
            for (auto edges : {graph.normInputsOf(id), graph.withInputsOf(id)})
                nodes[id].inputs.insert(nodes[id].inputs.end(), edges.begin(), edges.end());
        }
        // The control edges order the node in the block it is written in; they are not values,
        // so a region that holds the node reads nothing through them (docs/design/gpu/plan.cn.md 2.5).
        for (auto edges : {graph.ctrlInputsOf(id)})
            nodes[id].order.insert(nodes[id].order.end(), edges.begin(), edges.end());
        // The statement before it in its block's chain orders it the same way, whichever kind
        // that edge is written in (docs/design/effects/edges.cn.md 1, 5.3).
        for (const rt::GCGraphSyncEdge &edge : graph.syncPredsOf(id))
            nodes[id].order.push_back(edge.pred);
        nodes[id].output = id == graph.outputNodeRef() || id == graph.exitNodeRef();
    }
    // Fixed tuple projections are structural edges of an eligible aggregate
    // producer, not host computation boundaries. Never admit dynamic indexing.
    bool changed;
    do {
        changed = false;
        for (rt::gc_node_ref_t id = 0; id < graph.nodeCount(); ++id) {
            if (nodes[id].eligible || graph.node(id)->kind != rt::GCNodeKind::Accs ||
                !graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty() ||
                !graph.syncPredsOf(id).empty() || !graph.uses().sync(id).empty() ||
                inChain(chain, id) || !graph.withInputsOf(id).empty())
                continue;
            const auto inputs = graph.normInputsOf(id);
            if (inputs.size() != 1 || !nodes[inputs[0]].eligible)
                continue;
            auto *sourceType   = graph.node(inputs[0])->dataType();
            const auto *access = graph.nodeBodyAs<rt::GCAccsBody>(id);
            if (sourceType->code() != ty::TypeCode::Tuple ||
                access->accsKind != rt::GCAccsKind::TupleIndex)
                continue;
            auto *tuple = static_cast<ty::TupleType *>(sourceType);
            if (access->value >= tuple->size() ||
                tuple->typeAt(access->value) != graph.node(id)->dataType())
                continue;
            nodes[id].eligible = true;
            reasons[id].clear();
            changed = true;
        }
    } while (changed);
    auto regions = partitionRegions(nodes);
    // The boundary answer comes from the residency conclusion, not from the partition: the
    // partition says which nodes form a segment, and the conclusion says what crosses its boundary
    // (`tiles/internal/residency.h`).
    auto residency = planResidency(graphs, graph, nodes, regions);
    return {std::move(regions), std::move(reasons), std::move(residency)};
}
namespace {
// One graph's regions, the boundaries they extract and why the nodes in none are in none, as the
// report writes them, as the members of the object the caller opened. `skip` names nodes the
// reader is told about elsewhere: the wrapper of a cut scope reports its exec calls as segments,
// and an analysis exclusion of them says nothing.
void writeAnalysis(
    std::ostringstream &out, const rt::Program &graphs, const rt::GCGraph &graph,
    rt::fact::FactStore &facts, const GpuScopes *scopes,
    std::span<const rt::gc_node_ref_t> skip = {}) {
    const auto analysis = analyzeRegions(graphs, graph, facts, scopes);
    const auto &regions = analysis.regions;
    const auto &reasons = analysis.exclusions;
    // `residency` is keyed by the partition's region index, so the per-segment answer can sit with
    // the segment it belongs to: what the segment uploads, what the host downloads from it, and
    // what it takes from an earlier segment in place.
    const auto &segments = analysis.residency.segments;
    out << "\"regions\":[";
    bool first = true;
    for (size_t r = 0; r < regions.size(); ++r) {
        const auto &region = regions[r];
        if (!first)
            out << ',';
        first = false;
        out << "{\"members\":";
        ids(out, region.members);
        out << ",\"inputs\":";
        ids(out, region.inputs);
        out << ",\"outputs\":";
        ids(out, region.outputs);
        auto extracted = extractRegion(graph, region);
        out << ",\"parameters\":";
        ids(out, extracted.parameters);
        out << ",\"result_arity\":" << region.outputs.size();
        out << ",\"extracted_nodes\":" << extracted.graph->graph().nodeCount();
        const SegmentResidency empty;
        const auto &segment = r < segments.size() ? segments[r] : empty;
        out << ",\"uploads\":";
        ids(out, segment.uploads);
        out << ",\"downloads\":";
        ids(out, segment.downloads);
        out << ",\"resident\":[";
        bool firstHandoff = true;
        for (const auto &handoff : segment.resident) {
            if (!firstHandoff)
                out << ',';
            firstHandoff = false;
            out << "{\"value\":" << handoff.value << ",\"producer\":" << handoff.producer
                << ",\"consumer\":" << handoff.consumer << '}';
        }
        out << ']';
        for (auto id : region.members) {
            const auto *copy       = extracted.graph->graph().header(extracted.sourceToNode[id]);
            const auto permissions = camel::core::tag::effectivePermissions(
                graph,
                id,
                core::sem::NumericPolicy::explicitOnly());
            if (copy->origin != graph.nodeOrigin(id) || copy->permissions != permissions ||
                copy->dataType != graph.node(id)->dataType())
                throw std::runtime_error("region extraction lost source metadata");
        }
        out << '}';
    }
    // Once for the whole graph: the same three answers, which is what writes transfer nodes.
    out << "],\"transfers\":{\"uploads\":";
    ids(out, analysis.residency.uploads);
    out << ",\"downloads\":";
    ids(out, analysis.residency.downloads);
    out << ",\"resident\":[";
    first = true;
    for (const auto &edge : analysis.residency.resident) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"value\":" << edge.value << ",\"producer\":" << edge.producer
            << ",\"consumer\":" << edge.consumer << '}';
    }
    out << "]},\"excluded\":[";
    first = true;
    for (size_t id = 0; id < reasons.size(); ++id) {
        if (reasons[id].empty() ||
            std::find(skip.begin(), skip.end(), static_cast<rt::gc_node_ref_t>(id)) != skip.end())
            continue;
        if (!first)
            out << ',';
        first = false;
        out << "{\"node\":" << id << ",\"reason\":\"" << reasons[id] << "\"}";
    }
    out << ']';
}
} // namespace
std::string inspectRegions(
    const rt::Program &graphs, const rt::GCGraph &graph, rt::fact::FactStore &facts,
    std::span<const RegionSegment> segments) {
    // The table is the compiler's view of the program, so it says the same thing the pass would:
    // code written in a `#gpu` scope's arm or loop is a segment of that region (docs/design/gpu/plan.cn.md 2.5).
    const GpuScopes scopes(graphs);
    std::ostringstream out;
    out << "{\"schema\":1";
    std::vector<rt::gc_node_ref_t> cut;
    for (const auto &segment : segments)
        cut.push_back(segment.call);
    // A scope a pass has cut is a wrapper of exec calls: each call carries the graph of its
    // segment, and the segment graphs are what the report is about. The wrapper's own exclusion
    // of those calls (`context_arguments`) says nothing a reader asked for, so it is left out.
    out << ",\"segments\":[";
    bool first = true;
    for (const auto &segment : segments) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"call\":" << segment.call << ",\"graph\":\"" << segment.graph->graph()->name()
            << "\",";
        // The segment's own regions: the code the pass moved into it, cut as the graph it now is.
        // No scope covers a segment's graphs; the analysis a graph with no scope takes is the one
        // the report has always shown.
        writeAnalysis(out, graphs, *segment.graph->graph(), facts, nullptr);
        out << '}';
    }
    out << "],";
    writeAnalysis(out, graphs, graph, facts, &scopes, cut);
    out << '}';
    return out.str();
}
} // namespace camel::tiles
