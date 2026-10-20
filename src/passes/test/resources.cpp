/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 15, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY checks of the resource declarations and their resolution (see resources.h).
 */

#include "resources.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/resources.h"

#include <algorithm>
#include <format>
#include <ostream>
#include <stdexcept>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
namespace rt = camel::runtime;

/// Declares `resources` for `uri` and prints whether the table accepted them.
void attempt(
    std::ostream &os, std::string_view check, std::string_view uri, ResourceSpec resources) {
    try {
        OperatorTable::instance().declareResources(uri, std::move(resources));
        os << "RESOURCES " << check << " accepted\n";
    } catch (const std::invalid_argument &e) {
        os << "RESOURCES " << check << " rejected: " << e.what() << "\n";
    }
}

/// A one-dimensional instance: d0 < n, the write at offset + d0 (a window of a resource).
IndexedInstance window(int64_t offset, int64_t n) {
    IndexedInstance inst;
    ExprPool &x        = inst.exprs;
    inst.domain.params = {
        Param{.name = "n", .source = ParamSource::Static, .operand = 0, .value = n}};
    inst.domain.dims = {{"d0", x.param(0), DimRole::Parallel}};
    inst.output      = Write{.coords = {x.add(x.dim(0), x.constant(offset))}};
    inst.norm        = {Access{.kind = AccessKind::Affine, .coords = {x.scale(2, x.dim(0))}}};
    return inst;
}

std::string boundsText(const std::optional<rt::RegionBounds> &bounds) {
    if (!bounds) {
        return "unknown";
    }
    if (bounds->empty) {
        return "empty";
    }
    std::string out;
    for (const auto &[lo, hi] : bounds->axes) {
        out += std::format("{}[{}, {}]", out.empty() ? "" : " ", lo, hi);
    }
    return out;
}

/// A short label of node `ref`: its operator's URI, the graph a FUNC calls, or its kind.
std::string label(const rt::GCGraph &graph, rt::gc_node_ref_t ref) {
    switch (graph.node(ref)->kind) {
    case rt::GCNodeKind::Oper:
        return std::string(graph.nodeBodyAs<rt::GCOperBody>(ref)->uri());
    case rt::GCNodeKind::Func:
        return "FUNC " + graph.directCalleeGraphOf(ref)->name();
    case rt::GCNodeKind::Call:
        return "CALL";
    case rt::GCNodeKind::Loop:
        return "LOOP";
    default:
        return "node";
    }
}

} // namespace

camel::runtime::GCGraph *
ResourcesTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    OperatorTable &table = OperatorTable::instance();

    // ---- Registration (design 4.4, check 5): each declaration below breaks one rule. ----
    table.set("semtest:cell_write", {.stateOnly = true});
    table.set("semtest:io", {.pure = false});
    table.set("semtest:pure", {.pure = true});
    const ResourceAccess cellWrite = operandAccess(ResourceClass::Variable, 0, AccessMode::Write);
    attempt(os, "valid", "semtest:cell_write", ResourceSpec{.accesses = {cellWrite}});
    attempt(
        os,
        "unknown_class",
        "semtest:cell_write",
        ResourceSpec{.accesses = {operandAccess(ResourceClass::Unknown, 0, AccessMode::Write)}});
    attempt(
        os,
        "ambient_without_name",
        "semtest:io",
        ResourceSpec{.accesses = {ambientAccess("", AccessMode::Read)}, .externalEffect = true});
    attempt(
        os,
        "variable_with_name",
        "semtest:cell_write",
        ResourceSpec{
            .accesses = {operandAccess(ResourceClass::Variable, 0, AccessMode::Write, "x")}});
    {
        ResourceAccess bound = ambientAccess("fs", AccessMode::Read);
        bound.binding        = ResourceBinding{.kind = ResourceBinding::Kind::Norm, .index = 0};
        attempt(
            os,
            "ambient_bound_to_operand",
            "semtest:io",
            ResourceSpec{.accesses = {bound}, .externalEffect = true});
        ResourceAccess unbound = cellWrite;
        unbound.binding        = ResourceBinding{.kind = ResourceBinding::Kind::Ambient};
        attempt(os, "variable_unbound", "semtest:cell_write", ResourceSpec{.accesses = {unbound}});
        ResourceAccess capture = cellWrite;
        capture.binding = ResourceBinding{.kind = ResourceBinding::Kind::Capture, .index = 0};
        attempt(
            os,
            "operator_bound_to_capture",
            "semtest:cell_write",
            ResourceSpec{.accesses = {capture}});
    }
    attempt(
        os,
        "same_without_operand",
        "semtest:cell_write",
        ResourceSpec{
            .accesses = {cellWrite},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Same}});
    attempt(os, "pure_touches", "semtest:pure", ResourceSpec{.accesses = {cellWrite}});
    attempt(os, "effectful_touches_nothing", "semtest:io", ResourceSpec{});
    attempt(
        os,
        "external_against_state_only",
        "semtest:cell_write",
        ResourceSpec{.accesses = {cellWrite}, .externalEffect = true});
    // A refinement of the traits: an operator they count as external that declares no effect
    // outside the program (as the assignments do).
    table.set("semtest:assign", {.pure = false});
    attempt(os, "refines_external", "semtest:assign", ResourceSpec{.accesses = {cellWrite}});
    attempt(
        os,
        "allocates_ambient",
        "semtest:cell_write",
        ResourceSpec{
            .accesses  = {cellWrite},
            .allocates = Resource{.cls = ResourceClass::Ambient, .name = "x"}});
    // A record that exists gets the resources one minor version up; the same resources again
    // keep the version.
    table.declareSemantics(
        "semtest:pure",
        OperatorSemantics{.key = OperatorKey::of("semtest:pure", 1, 4)});
    attempt(os, "pure_touches_none", "semtest:pure", ResourceSpec{});
    attempt(os, "same_again", "semtest:pure", ResourceSpec{});
    // The declaration is the truth about effects; the `var` parameters of the signature are
    // checked against it (design principles 3.3), whichever is registered first.
    {
        using camel::core::type::StaticFuncTypeResolver;
        using camel::core::type::Type;
        table.set("semtest:var_op", {.stateOnly = true});
        table.setResolver(
            "semtest:var_op",
            StaticFuncTypeResolver::create({}, {{Type::Int64(), true}}, Type::Int64()));
        attempt(os, "var_param_written", "semtest:var_op", ResourceSpec{.accesses = {cellWrite}});
        attempt(
            os,
            "var_param_not_written",
            "semtest:var_op",
            ResourceSpec{
                .accesses = {operandAccess(ResourceClass::Variable, 0, AccessMode::Read)}});
        table.set("semtest:plain_op", {.stateOnly = true});
        table.setResolver(
            "semtest:plain_op",
            StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()));
        attempt(
            os,
            "written_without_var",
            "semtest:plain_op",
            ResourceSpec{.accesses = {cellWrite}});
        table.set("semtest:late_op", {.stateOnly = true});
        attempt(
            os,
            "late_declared",
            "semtest:late_op",
            ResourceSpec{
                .accesses = {operandAccess(ResourceClass::Variable, 0, AccessMode::Read)}});
        try {
            table.setResolver(
                "semtest:late_op",
                StaticFuncTypeResolver::create({}, {{Type::Int64(), true}}, Type::Int64()));
            os << "RESOURCES late_resolver accepted\n";
        } catch (const std::invalid_argument &e) {
            os << "RESOURCES late_resolver rejected: " << e.what() << "\n";
        }
        os << "RESOURCES writes var_op "
           << table.writesOperand(*table.lookup("semtest:var_op"), false, 0) << " plain_op "
           << table.writesOperand(*table.lookup("semtest:plain_op"), false, 0) << " late_op "
           << table.writesOperand(*table.lookup("semtest:late_op"), false, 0) << "\n";
    }
    os << "RESOURCES version " << table.key(*table.lookup("semtest:pure"))->toString() << " "
       << table.key(*table.lookup("semtest:cell_write"))->toString() << "\n";
    os << "RESOURCES text "
       << toString(*table.semantics(*table.lookup("semtest:cell_write"))->resources) << "\n";
    os << "RESOURCES conservative " << toString(conservativeResources(2, 1, false, true)) << "\n";
    os << "RESOURCES conservative_pure " << toString(conservativeResources(2, 1, true, false))
       << "\n";

    // ---- Mapped regions: windows of one resource. ----
    const ResourceRegion write{
        .kind = ResourceRegion::Kind::Mapped,
        .map  = ResourceRegion::Map::Write};
    const ResourceRegion read{
        .kind    = ResourceRegion::Kind::Mapped,
        .map     = ResourceRegion::Map::Norm,
        .operand = 0};
    const auto a = rt::regionBounds(window(2, 4), write); // [2, 5]
    const auto b = rt::regionBounds(window(6, 4), write); // [6, 9]
    const auto c = rt::regionBounds(window(5, 3), write); // [5, 7]
    const auto e = rt::regionBounds(window(0, 0), write); // empty
    const auto r = rt::regionBounds(window(0, 4), read);  // [0, 6] (2 * d0)
    os << "RESOURCES bounds " << boundsText(a) << " " << boundsText(b) << " " << boundsText(c)
       << " " << boundsText(e) << " " << boundsText(r) << " "
       << boundsText(rt::regionBounds(window(0, 4), ResourceRegion{})) << "\n";
    os << "RESOURCES overlap a-b " << rt::regionsMayOverlap(a, b) << " a-c "
       << rt::regionsMayOverlap(a, c) << " a-empty " << rt::regionsMayOverlap(a, e) << " a-unknown "
       << rt::regionsMayOverlap(a, std::nullopt) << "\n";

    // ---- The program. ----
    std::vector<rt::GCGraph *> graphs = rt::collectReachableGraphs(graph);
    std::sort(graphs.begin(), graphs.end(), [](const rt::GCGraph *x, const rt::GCGraph *y) {
        return x->name() < y->name();
    });
    for (const rt::GCGraph *g : graphs) {
        for (rt::gc_node_ref_t ref = 0; ref < g->nodeCount(); ++ref) {
            const rt::NodeResources resources = rt::resourcesOf(*g, ref);
            if (!resources.none()) {
                os << "RESOURCES node " << g->name() << " " << ref << " " << label(*g, ref) << ": "
                   << rt::toString(resources) << "\n";
            }
        }
        os << "RESOURCES footprint " << g->name() << " " << rt::toString(rt::footprintOf(g))
           << "\n";
        for (const rt::RequiredOrdering &o : rt::requiredOrderings(*g)) {
            os << "RESOURCES order " << g->name() << " " << o.first << " " << o.second << " "
               << rt::conflictKindName(o.why) << " " << (o.ordered ? "ordered" : "unordered");
            if (o.ordered) {
                os << " by " << (o.byData ? "data" : "control");
            }
            os << "\n";
        }
    }
    return graph;
}
