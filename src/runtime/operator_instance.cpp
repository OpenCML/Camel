// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator semantics of an OPER node of a frozen graph (see operator_instance.h).
 */

#include "camel/runtime/operator_instance.h"

#include "camel/runtime/reachable.h"

#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/tags.h"
#include "camel/core/type/base.h"

#include <ostream>
#include <string>
#include <vector>

namespace camel::runtime {

namespace core = camel::core;
namespace type = camel::core::type;

namespace {

/// True for a value that is not an object: nothing can change it in place.
bool isPlainScalar(type::Type *t) {
    switch (t->code()) {
    case type::TypeCode::Int32:
    case type::TypeCode::Int64:
    case type::TypeCode::Float32:
    case type::TypeCode::Float64:
    case type::TypeCode::Bool:
    // A bf16 or f16 value is a scalar of the language like an f32 (design principles 7.3), and
    // its slot holds its two-byte pattern: a constant of one of them inlines the same way.
    case type::TypeCode::BFloat16:
    case type::TypeCode::Float16:
        return true;
    default:
        return false;
    }
}

/// The value of `ref` when it is a constant: a static DATA node, also behind GATEs. An object
/// (an int array) counts only when every user of the DATA node is a pure operator.
std::optional<slot_t> staticValueOf(const GCGraph &graph, gc_node_ref_t ref) {
    for (gc_cnt_t hops = 0; hops < graph.nodeCount(); ++hops) {
        const GCNode *n = graph.node(ref);
        if (n->kind != GCNodeKind::Gate || graph.normInputsOf(ref).empty()) {
            break;
        }
        ref = graph.normInputsOf(ref).back();
    }
    const GCNode *n = graph.node(ref);
    if (n->kind != GCNodeKind::Data || n->dataIndex >= 0 || n->dataType() == nullptr) {
        return std::nullopt;
    }
    if (!isPlainScalar(n->dataType())) {
        const auto &table = core::OperatorTable::instance();
        for (gc_node_ref_t user : graph.normOutputsOf(ref)) {
            const GCNode *u = graph.node(user);
            if (u->kind != GCNodeKind::Oper ||
                !table.mayBeRecomputed(graph.nodeBodyAs<GCOperBody>(user)->oper)) {
                return std::nullopt;
            }
        }
        if (!graph.withOutputsOf(ref).empty()) {
            return std::nullopt;
        }
    }
    return graph.staticArea()->get<slot_t>(static_cast<size_t>(-n->dataIndex));
}

} // namespace

std::optional<core::sem::IndexedInstance> instanceOf(const GCGraph &graph, gc_node_ref_t node) {
    if (!graph.containsNodeRef(node) || graph.node(node)->kind != GCNodeKind::Oper) {
        return std::nullopt;
    }
    std::vector<type::Type *> norm, with;
    std::vector<type::StaticArg> statics;
    for (gc_node_ref_t in : graph.normInputsOf(node)) {
        type::Type *t = graph.node(in)->dataType();
        if (t == nullptr) {
            return std::nullopt;
        }
        norm.push_back(t);
        statics.emplace_back(staticValueOf(graph, in));
    }
    for (gc_node_ref_t in : graph.withInputsOf(node)) {
        type::Type *t = graph.node(in)->dataType();
        if (t == nullptr) {
            return std::nullopt;
        }
        with.push_back(t);
    }
    return core::sem::instantiate(
        graph.nodeBodyAs<GCOperBody>(node)->oper,
        core::sem::InstanceContext{.norm = norm, .with = with, .statics = statics});
}

std::optional<core::sem::ReorderFreedom>
reorderFreedomOf(const GCGraph &graph, gc_node_ref_t node, const core::sem::NumericPolicy &policy) {
    const auto instance = instanceOf(graph, node);
    if (!instance || instance->law == core::sem::kNoLaw) {
        return std::nullopt;
    }
    const core::sem::OperatorSemantics *s =
        core::OperatorTable::instance().semantics(graph.nodeBodyAs<GCOperBody>(node)->oper);
    // instantiate validated the law's index against these semantics.
    return core::sem::reorderFreedom(
        s->laws[instance->law],
        core::tag::effectivePermissions(graph, node, policy),
        policy);
}

namespace {

std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out += c;
        }
    }
    return out + "\"";
}

/// The lines of `text` as a JSON list of strings.
std::string jsonLines(std::string_view text) {
    std::string out = "[";
    size_t start    = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        end        = end == std::string_view::npos ? text.size() : end;
        out += (out.size() > 1 ? "," : "") + jsonString(text.substr(start, end - start));
        start = end + 1;
    }
    return out + "]";
}

std::string freedomJson(const core::sem::ReorderFreedom &f) {
    std::string out = "[";
    for (const auto &[on, name] :
         {std::pair{f.regroup, "regroup"},
          std::pair{f.permute, "permute"},
          std::pair{f.blockIdentity, "block_identity"},
          std::pair{f.contract, "contract"},
          std::pair{f.changePrecision, "change_precision"}}) {
        if (on) {
            out += (out.size() > 1 ? "," : "") + jsonString(name);
        }
    }
    return out + "]";
}

} // namespace

void dumpOperatorInstances(const Program &program, std::ostream &os) {
    const auto &table = core::OperatorTable::instance();
    for (GCGraph *graph : collectReachableGraphs(program)) {
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (graph->node(ref)->kind != GCNodeKind::Oper) {
                continue;
            }
            const core::OperId id                 = graph->nodeBodyAs<GCOperBody>(ref)->oper;
            const core::sem::OperatorSemantics *s = table.semantics(id);
            if (s == nullptr || !s->indexing) {
                continue;
            }
            std::string line    = "{\"graph\":" + jsonString(graph->name()) +
                                  ",\"node\":" + std::to_string(ref) +
                                  ",\"uri\":" + jsonString(table.uri(id)) +
                                  ",\"key\":" + jsonString(s->key.toString());
            const auto instance = instanceOf(*graph, ref);
            line += ",\"instance\":" +
                    (instance ? jsonLines(core::sem::toString(*instance, s)) : std::string("null"));
            const camel::core::tag::Facts facts = camel::core::tag::factsOf(*graph, ref);
            if (facts.permissions != 0) {
                line +=
                    ",\"permissions\":" + jsonString(core::sem::permissionsText(facts.permissions));
            }
            if (facts.tags != 0) {
                line += ",\"attrs\":" + jsonString(core::attr::text(facts.tags));
            }
            if (const auto freedom = reorderFreedomOf(*graph, ref)) {
                line += ",\"freedom\":" + freedomJson(*freedom);
            }
            os << "INSTANCE " << line << "}\n";
        }
    }
}

} // namespace camel::runtime
