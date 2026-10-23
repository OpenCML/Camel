// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY checks of the resource declarations and their resolution (see resources.h).
 */

#include "resources.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/resources.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <vector>

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
std::string label(const rt::Program &program, const rt::GCGraph &graph, rt::gc_node_ref_t ref) {
    switch (graph.node(ref)->kind) {
    case rt::GCNodeKind::Oper:
        return std::string(graph.nodeBodyAs<rt::GCOperBody>(ref)->uri());
    case rt::GCNodeKind::Func:
        return "FUNC " + program.calleeOf(graph, ref)->name();
    case rt::GCNodeKind::Call:
        return "CALL";
    case rt::GCNodeKind::Loop:
        return "LOOP";
    default:
        return "node";
    }
}

using camel::core::type::FunctionType;
using camel::core::type::FunctionTypeFactory;
using camel::core::type::Type;

FunctionType *nullaryInt() {
    FunctionTypeFactory factory;
    factory.setExitType(Type::Int64());
    return factory.build();
}

FunctionType *unaryInt() {
    FunctionTypeFactory factory;
    factory.addNormArg("x", Type::Int64(), false);
    factory.setExitType(Type::Int64());
    return factory.build();
}

/// A function type that publishes `uses h` (or `pure`) while its body says nothing: the call's
/// footprint must come from the type (design effects plan 2.3).
FunctionType *typedUnaryInt(bool usesParameter, bool isVar = false) {
    FunctionTypeFactory factory;
    factory.addNormArg("h", Type::Int64(), isVar);
    factory.setExitType(Type::Int64());
    ResourceSignature signature;
    signature.mode = ResourceSignature::Mode::Concrete;
    if (usesParameter) {
        signature.params.push_back(
            ResourceSignature::ParamUse{
                .index = 0,
                .cls   = ResourceClass::Object,
                .name  = {},
                .mode  = AccessMode::ReadModifyWrite,
            });
    }
    factory.setResources(signature);
    return factory.build();
}

/// Graphs built here rather than by the front end: the model's FUNC/CALL result inference has no
/// syntax yet (design effects plan steps 1-2 are the internal model), so the shapes the plan
/// names are built directly and asked the public queries.
void syntheticGraphs(std::ostream &os, const rt::Program &base) {
    uint32_t nextId = static_cast<uint32_t>(base.tableSize()) + 1000;
    const auto alloc = [&] { return static_cast<rt::GraphId>(nextId++); };
    const rt::GraphId idOpen  = alloc();
    const rt::GraphId idSame  = alloc();
    const rt::GraphId idView  = alloc();
    const rt::GraphId idNext  = alloc();
    const rt::GraphId idPass  = alloc();
    const rt::GraphId idTyped = alloc();
    const rt::GraphId idTypedUnused = alloc();
    const rt::GraphId idTypedCaller = alloc();
    const rt::GraphId idTypedSame   = alloc();
    const rt::GraphId idUnnamed     = alloc();
    enum Caller {
        kOpenTwice,
        kOpenTwiceCtrl,
        kSameTwice,
        kSameOneCtrl,
        kCallerCount,
    };
    std::array<rt::GraphId, kCallerCount> callerIds{};
    for (auto &id : callerIds) {
        id = alloc();
    }

    std::vector<rt::GCGraph *> graphs;
    const auto addPort = [](rt::GraphDraft &draft, Type *type) {
        const rt::gc_slot_idx_t slot = draft.allocateRuntimeSlot(type);
        const rt::gc_node_ref_t port = draft.addPortNode(type, slot);
        draft.appendNormPort(port);
        return port;
    };
    const auto finish = [](rt::GraphDraft &draft, rt::gc_node_ref_t value) {
        draft.setReturnNode(value, rt::GCReturnKind::Self);
        draft.setOutputNode(value);
        draft.setExitNode(value);
    };
    const auto encode = [&](rt::GraphDraft &draft, const std::string &name) {
        rt::GCGraph *graph = draft.encode("synth_" + name, "synth:" + name);
        graphs.push_back(graph);
        return graph;
    };
    const auto makeDraft = [](rt::GraphId id, FunctionType *type) {
        auto draft = std::make_unique<rt::GraphDraft>();
        draft->setId(id);
        draft->setFuncType(type);
        return draft;
    };

    // `open()` makes a fresh object, `same(x)` returns its parameter, `view(x)` a view of it,
    // `next(h)` touches the object the handle names.
    {
        auto draft = makeDraft(idOpen, nullaryInt());
        const rt::gc_node_ref_t value =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_open");
        finish(*draft, value);
        encode(*draft, "open");
    }
    {
        auto draft = makeDraft(idSame, unaryInt());
        const rt::gc_node_ref_t port = addPort(*draft, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> inputs{port};
        const rt::gc_node_ref_t value =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_same");
        draft->setNormInputs(value, inputs);
        finish(*draft, value);
        encode(*draft, "same");
    }
    {
        auto draft = makeDraft(idView, unaryInt());
        const rt::gc_node_ref_t port = addPort(*draft, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> inputs{port};
        const rt::gc_node_ref_t value =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_view");
        draft->setNormInputs(value, inputs);
        finish(*draft, value);
        encode(*draft, "view");
    }
    {
        auto draft = makeDraft(idNext, unaryInt());
        const rt::gc_node_ref_t port = addPort(*draft, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> inputs{port};
        const rt::gc_node_ref_t value =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
        draft->setNormInputs(value, inputs);
        finish(*draft, value);
        encode(*draft, "next");
    }
    {
        // A function whose return is its parameter: the result is the port, not fresh.
        auto draft = makeDraft(idPass, unaryInt());
        finish(*draft, addPort(*draft, Type::Int64()));
        encode(*draft, "passthrough");
    }
    {
        // The same body, but the type publishes `uses h`: the call touches the argument even
        // though the body does not.
        auto draft = makeDraft(idTyped, typedUnaryInt(true));
        finish(*draft, addPort(*draft, Type::Int64()));
        encode(*draft, "typed");
    }
    {
        // `pure` with a `var` parameter: a call that passes a cell the signature says is unused
        // touches nothing (the plan's 2.2/2.3 property).
        auto draft = makeDraft(idTypedUnused, typedUnaryInt(false, true));
        finish(*draft, addPort(*draft, Type::Int64()));
        encode(*draft, "typed-unused");
    }
    {
        auto draft = makeDraft(idTypedCaller, nullaryInt());
        draft->addDependency(idOpen);
        draft->addDependency(idPass);
        draft->addDependency(idTyped);
        draft->addDependency(idTypedUnused);
        const rt::gc_node_ref_t a = draft->addFuncNode(idOpen, Type::Int64());
        const rt::gc_node_ref_t b = draft->addFuncNode(idOpen, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> ax{a};
        const std::array<rt::gc_node_ref_t, 1> by{b};
        const rt::gc_node_ref_t generic = draft->addFuncNode(idPass, Type::Int64());
        draft->setNormInputs(generic, ax);
        const rt::gc_node_ref_t ca = draft->addFuncNode(idTyped, Type::Int64());
        const rt::gc_node_ref_t cb = draft->addFuncNode(idTyped, Type::Int64());
        const rt::gc_node_ref_t ua = draft->addFuncNode(idTypedUnused, Type::Int64());
        draft->setNormInputs(ca, ax);
        draft->setNormInputs(cb, by);
        draft->setNormInputs(ua, ax);
        finish(*draft, cb);
        encode(*draft, "typed-caller");
    }
    {
        // Two calls of the same typed function on one value: one resource, serialised. The body
        // says nothing, so before step 2 there is no pair at all.
        auto draft = makeDraft(idTypedSame, nullaryInt());
        draft->addDependency(idOpen);
        draft->addDependency(idTyped);
        const rt::gc_node_ref_t a = draft->addFuncNode(idOpen, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> ax{a};
        const rt::gc_node_ref_t ca = draft->addFuncNode(idTyped, Type::Int64());
        const rt::gc_node_ref_t cb = draft->addFuncNode(idTyped, Type::Int64());
        draft->setNormInputs(ca, ax);
        draft->setNormInputs(cb, ax);
        const std::array<rt::gc_node_ref_t, 1> after{ca};
        draft->setCtrlInputs(cb, after);
        finish(*draft, cb);
        encode(*draft, "typed-same");
    }
    {
        // An effect that names no resource at all, next to one that names a cell: the global
        // resource meets every effect (design principles 3.3), where two named resources do not
        // (F9). Before the pair is decided by resource alone there is no pair here at all.
        auto draft = makeDraft(idUnnamed, unaryInt());
        const rt::gc_node_ref_t port = addPort(*draft, Type::Int64());
        const std::array<rt::gc_node_ref_t, 1> inputs{port};
        const rt::gc_node_ref_t cell =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:cell_write");
        draft->setNormInputs(cell, inputs);
        const rt::gc_node_ref_t global =
            draft->addOperNode(Type::Int64(), nullptr, "semtest:unnamed");
        const std::array<rt::gc_node_ref_t, 1> after{cell};
        draft->setCtrlInputs(global, after);
        finish(*draft, global);
        encode(*draft, "unnamed-global");
    }

    // (a) open() then two operations on the same returned value: one object, serialised.
    for (const Caller caller : {kOpenTwice, kOpenTwiceCtrl, kSameTwice, kSameOneCtrl}) {
        const bool withCtrl = caller == kOpenTwiceCtrl || caller == kSameOneCtrl;
        auto draft = makeDraft(callerIds[caller], nullaryInt());
        draft->addDependency(idOpen);
        if (caller != kOpenTwice && caller != kOpenTwiceCtrl) {
            draft->addDependency(idSame);
        }
        const rt::gc_node_ref_t a = draft->addFuncNode(idOpen, Type::Int64());
        const rt::gc_node_ref_t b = caller != kSameOneCtrl
                                        ? draft->addFuncNode(idOpen, Type::Int64())
                                        : rt::kInvalidNodeRef;
        rt::gc_node_ref_t first = rt::kInvalidNodeRef;
        rt::gc_node_ref_t second = rt::kInvalidNodeRef;
        if (caller == kOpenTwice || caller == kOpenTwiceCtrl) {
            // Two open() results: before result inference both are Unknown, so the model cannot
            // tell them apart and has to serialise them.
            first  = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            second = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            const std::array<rt::gc_node_ref_t, 1> x{a};
            const std::array<rt::gc_node_ref_t, 1> y{b};
            draft->setNormInputs(first, x);
            draft->setNormInputs(second, y);
        } else if (caller == kSameTwice) {
            // same(open()) twice: the result is Same of each fresh object.
            const std::array<rt::gc_node_ref_t, 1> ax{a};
            const std::array<rt::gc_node_ref_t, 1> by{b};
            const rt::gc_node_ref_t ia = draft->addFuncNode(idSame, Type::Int64());
            const rt::gc_node_ref_t ib = draft->addFuncNode(idSame, Type::Int64());
            draft->setNormInputs(ia, ax);
            draft->setNormInputs(ib, by);
            first  = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            second = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            const std::array<rt::gc_node_ref_t, 1> x{ia};
            const std::array<rt::gc_node_ref_t, 1> y{ib};
            draft->setNormInputs(first, x);
            draft->setNormInputs(second, y);
        } else {
            // same(open()) and open() are one object (Same), so the two next calls conflict.
            const std::array<rt::gc_node_ref_t, 1> ax{a};
            const rt::gc_node_ref_t ia = draft->addFuncNode(idSame, Type::Int64());
            draft->setNormInputs(ia, ax);
            first  = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            second = draft->addOperNode(Type::Int64(), nullptr, "semtest:handle_next");
            const std::array<rt::gc_node_ref_t, 1> x{ia};
            const std::array<rt::gc_node_ref_t, 1> y{a};
            draft->setNormInputs(first, x);
            draft->setNormInputs(second, y);
        }
        if (withCtrl) {
            const std::array<rt::gc_node_ref_t, 1> after{first};
            draft->setCtrlInputs(second, after);
        }
        finish(*draft, second);
        encode(*draft, caller == kOpenTwice       ? "open-twice"
                           : caller == kOpenTwiceCtrl ? "open-twice-ctrl"
                           : caller == kSameTwice     ? "same-twice"
                                                      : "same-one-ctrl");
    }

    // The signature is part of the type: equal signatures intern to one object, and a Concrete
    // signature is not the Generic type of the same parameters (design effects plan 2.2).
    {
        FunctionType *a = typedUnaryInt(true);
        FunctionType *b = typedUnaryInt(true);
        FunctionType *pure = typedUnaryInt(false);
        FunctionType *generic = unaryInt();
        os << "RESOURCES synth type intern " << (a == b ? "same" : "different") << " pure_generic "
           << (pure->equals(generic) ? "same" : "different") << "\n";
    }

    rt::Program synthetic(base);
    for (rt::GCGraph *graph : graphs) {
        synthetic.bind(graph->id(), graph);
    }

    const auto byName = [&](const std::string &name) -> rt::GCGraph * {
        for (rt::GCGraph *graph : graphs) {
            if (graph->name() == name) {
                return graph;
            }
        }
        return nullptr;
    };
    for (const char *name :
         {"open", "same", "view", "next", "passthrough", "typed", "typed-unused"}) {
        const rt::GCGraph *graph = byName(std::string("synth:") + name);
        os << "RESOURCES synth result " << name << " "
           << rt::toString(rt::footprintOf(synthetic, graph)) << "\n";
    }
    for (const char *name : {
             "open-twice",
             "open-twice-ctrl",
             "same-twice",
             "same-one-ctrl",
             "typed-caller",
             "typed-same",
         }) {
        const rt::GCGraph *graph = byName(std::string("synth:") + name);
        for (rt::gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (graph->node(ref)->kind != rt::GCNodeKind::Func) {
                continue;
            }
            const rt::GCGraph *callee = synthetic.calleeOf(*graph, ref);
            os << "RESOURCES synth node " << name << " " << ref << " FUNC "
               << (callee != nullptr ? callee->name() : std::string("<unresolved>")) << ": "
               << rt::toString(rt::resourcesOf(synthetic, *graph, ref)) << "\n";
        }
        for (const rt::RequiredOrdering &o : rt::requiredOrderings(synthetic, *graph)) {
            os << "RESOURCES synth order " << name << " " << o.first << " " << o.second << " "
               << rt::conflictKindName(o.why) << " " << (o.ordered ? "ordered" : "unordered");
            if (o.ordered) {
                os << " by " << (o.byData ? "data" : "control");
            }
            os << "\n";
        }
    }
}

} // namespace

const camel::runtime::Program *
ResourcesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    OperatorTable &table = OperatorTable::instance();

    // ---- Registration (design 4.4, check 5): each declaration below breaks one rule. ----
    table.set("semtest:cell_write", {.stateOnly = true});
    table.set("semtest:io", {.pure = false});
    table.set("semtest:pure", {.pure = true});
    // Synthetic handle operators: the model's shapes before the `#resource`/`uses` syntax exists.
    table.set("semtest:handle_open", {.stateOnly = true});
    table.set("semtest:handle_next", {.stateOnly = true});
    table.set("semtest:handle_same", {.stateOnly = true});
    table.set("semtest:handle_view", {.stateOnly = true});
    // A handle: one resource per value, named by the declaration that introduces it.
    constexpr const char *kStream = "semtest:Stream";
    table.declareResources(
        "semtest:handle_open",
        ResourceSpec{.result    = ResultAlias{.kind = ResultAlias::Kind::Fresh},
                     .allocates = Resource{.cls = ResourceClass::Handle, .name = kStream}});
    table.declareResources(
        "semtest:handle_next",
        ResourceSpec{
            .accesses = {operandAccess(
                ResourceClass::Handle, 0, AccessMode::ReadModifyWrite, kStream)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Fresh}});
    table.declareResources(
        "semtest:handle_same",
        ResourceSpec{
            .accesses = {operandAccess(ResourceClass::Handle, 0, AccessMode::Read, kStream)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0}});
    table.declareResources(
        "semtest:handle_view",
        ResourceSpec{
            .accesses = {operandAccess(ResourceClass::Handle, 0, AccessMode::Read, kStream)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::View, .operand = 0}});
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
    // An effect outside the program that names nothing at all: the conservative global resource
    // (design principles 3.3). The traits allow an external effect, so the declaration is accepted
    // with no access: the node itself gets the access of unknown class.
    table.set("semtest:unnamed", {.pure = false});
    attempt(
        os,
        "unnamed_effect_declared",
        "semtest:unnamed",
        ResourceSpec{.externalEffect = true});
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
    std::vector<rt::GCGraph *> graphs = rt::collectReachableGraphs(*program, graph);
    std::sort(graphs.begin(), graphs.end(), [](const rt::GCGraph *x, const rt::GCGraph *y) {
        return x->name() < y->name();
    });
    for (const rt::GCGraph *g : graphs) {
        for (rt::gc_node_ref_t ref = 0; ref < g->nodeCount(); ++ref) {
            const rt::NodeResources resources = rt::resourcesOf(*program, *g, ref);
            if (!resources.none()) {
                os << "RESOURCES node " << g->name() << " " << ref << " "
                   << label(*program, *g, ref) << ": " << rt::toString(resources) << "\n";
            }
        }
        os << "RESOURCES footprint " << g->name() << " "
           << rt::toString(rt::footprintOf(*program, g)) << "\n";
        for (const rt::RequiredOrdering &o : rt::requiredOrderings(*program, *g)) {
            os << "RESOURCES order " << g->name() << " " << o.first << " " << o.second << " "
               << rt::conflictKindName(o.why) << " " << (o.ordered ? "ordered" : "unordered");
            if (o.ordered) {
                os << " by " << (o.byData ? "data" : "control");
            }
            os << "\n";
        }
    }
    syntheticGraphs(os, *program);
    return program;
}
