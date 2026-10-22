/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Classify static core-indexed operations, then partition the complete dependency DAG.
#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/loop.h"
#include "modules/tensor/type.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/internal/regions.h"
#include "tiles/program.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace camel::tiles {
namespace {
namespace rt  = runtime;
namespace sem = core::sem;
namespace ty  = core::type;
std::optional<Shape> shape(ty::Type *type) {
    if (!type)
        return std::nullopt;
    if (type->code() == ty::TypeCode::Float64)
        return Shape{};
    const auto *tensor = tensor::asTensorType(type);
    if (!tensor || tensor->dtype() != ty::TypeCode::Float32 || !tensor->isStaticShape())
        return std::nullopt;
    return *tensor->shape();
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
              "sum_to"})
            result.emplace(table.intern(std::string("tensor:") + name), name);
        result.emplace(table.intern("tensor:sum"), "sum_all");
        result.emplace(table.intern("tensor:mean"), "mean_all");
        return result;
    }();
    return result;
}
std::string eligibility(
    const rt::Program &graphs, const rt::GCGraph &graph, rt::gc_node_ref_t id,
    rt::fact::FactStore &facts) {
    const auto *node = graph.node(id);
    if (node->kind == rt::GCNodeKind::Loop) {
        if (!graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty())
            return "control_edge";
        return loopEligibility(graphs, graph, id, facts);
    }
    if (node->kind != rt::GCNodeKind::Oper)
        return "node_kind";
    if (!graph.ctrlInputsOf(id).empty() || !graph.ctrlOutputsOf(id).empty())
        return "control_edge";
    if (!graph.withInputsOf(id).empty())
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
    Node candidate{
        .op          = found->second,
        .shape       = *output,
        .inputs      = {},
        .dtype       = node->dataType()->code() == ty::TypeCode::Float64 ? DType::F64 : DType::F32,
        .permissions = camel::core::tag::factsOf(graph, id).permissions,
        .semantic    = 0,
        .indexing    = inst};
    const auto inputs = graph.normInputsOf(id);
    if (inputs.size() != inst.norm.size())
        return "operand_arity";
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inst.norm[i].kind == sem::AccessKind::None)
            continue;
        const auto input = shape(graph.node(inputs[i])->dataType());
        if (!input)
            return "unresolved_input_type";
        // Current physical tensor kernels take f32 tensor operands; f64 scalar
        // arithmetic has its own lowering and is not selected by this table yet.
        if (graph.node(inputs[i])->dataType()->code() == ty::TypeCode::Float64)
            return "scalar_tensor_operand";
        candidate.inputs.push_back(program.nodes.size());
        program.nodes.push_back(Node{.op = "input", .shape = *input, .inputs = {}});
    }
    try {
        if (elementwise(candidate.op)) {
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
RegionAnalysis
analyzeRegions(const rt::Program &graphs, const rt::GCGraph &graph, rt::fact::FactStore &facts) {
    std::vector<RegionNode> nodes(graph.nodeCount());
    std::vector<std::string> reasons(nodes.size());
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
        reasons[id]        = eligibility(graphs, graph, id, facts);
        nodes[id].eligible = reasons[id].empty();
        nodes[id].embedded =
            graph.node(id)->kind == rt::GCNodeKind::Data && graph.node(id)->dataIndex < 0;
        if (graph.node(id)->kind == rt::GCNodeKind::Loop) {
            for (auto edges : {graph.normInputsOf(id), graph.ctrlInputsOf(id)})
                nodes[id].inputs.insert(nodes[id].inputs.end(), edges.begin(), edges.end());
            nodes[id].inputs.insert(
                nodes[id].inputs.end(),
                loopReads[id].begin(),
                loopReads[id].end());
        } else {
            for (auto edges :
                 {graph.normInputsOf(id), graph.withInputsOf(id), graph.ctrlInputsOf(id)})
                nodes[id].inputs.insert(nodes[id].inputs.end(), edges.begin(), edges.end());
        }
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
                !graph.withInputsOf(id).empty())
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
    auto regions   = partitionRegions(nodes);
    auto transfers = planRegionTransfers(nodes, regions);
    return {std::move(regions), std::move(reasons), std::move(transfers)};
}
std::string
inspectRegions(const rt::Program &graphs, const rt::GCGraph &graph, rt::fact::FactStore &facts) {
    const auto analysis = analyzeRegions(graphs, graph, facts);
    const auto &regions = analysis.regions;
    const auto &reasons = analysis.exclusions;
    std::ostringstream out;
    out << "{\"schema\":1,\"regions\":[";
    bool first = true;
    for (const auto &region : regions) {
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
        out << ",\"extracted_nodes\":" << extracted.graph->nodeCount();
        for (auto id : region.members) {
            const auto *copy = extracted.graph->header(extracted.sourceToNode[id]);
            if (copy->origin != graph.nodeOrigin(id) ||
                copy->permissions != camel::core::tag::factsOf(graph, id).permissions ||
                copy->dataType != graph.node(id)->dataType())
                throw std::runtime_error("region extraction lost source metadata");
        }
        out << '}';
    }
    out << "],\"transfers\":{\"uploads\":";
    ids(out, analysis.transfers.uploads);
    out << ",\"downloads\":";
    ids(out, analysis.transfers.downloads);
    out << ",\"resident\":[";
    first = true;
    for (const auto &edge : analysis.transfers.resident) {
        if (!first)
            out << ',';
        first = false;
        out << "{\"value\":" << edge.value << ",\"producer\":" << edge.producer
            << ",\"consumer\":" << edge.consumer << '}';
    }
    out << "]},\"excluded\":[";
    first = true;
    for (size_t id = 0; id < reasons.size(); ++id)
        if (!reasons[id].empty()) {
            if (!first)
                out << ',';
            first = false;
            out << "{\"node\":" << id << ",\"reason\":\"" << reasons[id] << "\"}";
        }
    out << "]}";
    return out.str();
}
} // namespace camel::tiles
