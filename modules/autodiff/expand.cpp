// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `autodiff::expand`: build a gradient at compile time (docs/design/gpu/plan.cn.md 2.6).
 *
 * `grad` and `with_grad` are runtime operators: `gradFunction` (operators.cpp) calls
 * `buildGradientGraph` while the operator runs. Every pass has finished by then, so no pass ever
 * sees the derived graph, it is never compiled, and the backward pass always runs on the host —
 * the finding that decides this step (plan 2.6).
 *
 * This pass moves that construction to compile time for the call sites **inside a `#gpu`
 * scope**: the function operand is statically known (a named function, or a static closure
 * constant), so the derived graph is built now and the grad operator is replaced by a function
 * constant holding it. The value is the one the operator produces at run time: a `Function`
 * whose `with` parameters the call site binds, not values fixed inside the graph.
 *
 * docs/design/gpu/plan.cn.md 2.6, the requirements this pass keeps:
 *   1. the derived graph is built at compile time and the call site becomes a function constant
 *      holding it (the expansion below);
 *   2. the derived graph inherits the primal's placement (buildGradientGraph copies the primal
 *      body region's attrs; plan 2.3);
 *   3. a grad whose function is not statically known inside a scope is a compile-time error,
 *      `GpuScopeUnsupported`, naming the operator and the call site;
 *   4. outside `#gpu` nothing changes: with no expansion the operator still builds its gradient
 *      at run time and the interpreter runs it;
 *   and the two hard requirements: the pass is **idempotent** (after the replacement there is no
 *   grad operator left to expand) and a graph without `#gpu` is **byte-identical** with and
 *   without it, which `std::jgir` compares.
 *
 * Which call sites are inside a scope is asked the way `tiles` asks it
 * (docs/design/gpu/plan-modules.cn.md 5, decision 1): this module does not link `tiles`; it reads the one placement key with
 * `attr::key("tiles.placement")` and walks the same order as `placementOf`
 * (modules/tiles/src/placement.cpp) — the node's own fact, the nearest tagged region, the
 * function body, the host — plus the call propagation of `GpuScopes` (a helper a `#gpu` scope
 * calls is in the scope too, plan 2.3). A change to that order must change both files.
 */

#include "expand.h"

#include "pullback.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/node_attrs.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/pass/opt.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::autodiff {
namespace {

namespace attr = camel::core::attr;
namespace rt   = camel::runtime;
namespace td   = camel::core::rtdata;
namespace ty   = camel::core::type;

/// The two operators this pass rewrites, by the URIs their groups register (operators.cpp).
constexpr std::string_view kGradUri     = "autodiff:grad";
constexpr std::string_view kWithGradUri = "autodiff:with_grad";

attr::AttrSetId bodyAttrs(const rt::GCGraph &graph) {
    return graph.regionCount() > 0 && graph.region(0) ? graph.region(0)->attrs : attr::kNoAttrs;
}

/// What the first placement fact found says (plan 2.2). The three answers the walk needs:
/// on the device, explicitly on the host (`#cpu`), or no fact at all.
enum class Fact { Device, Host, None };

Fact classify(const rt::GCGraph &graph) {
    const attr::AttrKey key = attr::key("tiles.placement");
    if (key == attr::kNoKey) {
        return Fact::None;
    }
    const attr::AttrValueId value = attr::valueOf(bodyAttrs(graph), key);
    if (value == attr::kNoValue) {
        return Fact::None;
    }
    // A body fact is the graph's own scope: `gpu` there is the device, anything else the host.
    return value == attr::symbol("gpu") ? Fact::Device : Fact::Host;
}

/// The placement the first fact around `node` gives, in the order of tiles' `placementOf`: the
/// node's own fact, the nearest tagged region, the function body, then nothing (the host).
Fact factOf(const rt::GCGraph &graph, rt::gc_node_ref_t node) {
    const attr::AttrKey key = attr::key("tiles.placement");
    if (key == attr::kNoKey) {
        return Fact::None;
    }
    const auto read = [&](attr::AttrSetId attrs) {
        const attr::AttrValueId value = attr::valueOf(attrs, key);
        if (value == attr::kNoValue) {
            return Fact::None;
        }
        // `cpu` is the explicit host fact and the written way out of a scope; a fact that is
        // neither label cannot name a device Tiles runs on, so it is the host's as well.
        return value == attr::symbol("gpu") ? Fact::Device : Fact::Host;
    };
    if (const Fact own = read(graph.nodeAttrs(node)); own != Fact::None) {
        return own;
    }
    for (uint32_t region = graph.nodeRegion(node); region != 0;
         region          = graph.region(region)->parent) {
        if (const Fact found = read(graph.region(region)->attrs); found != Fact::None) {
            return found;
        }
    }
    return classify(graph);
}

bool onDevice(const rt::GCGraph &graph, rt::gc_node_ref_t node) {
    return factOf(graph, node) == Fact::Device;
}

/// The graph a `#gpu` scope is: a body whose own fact asks for the device.
bool isScope(const rt::GCGraph &graph) { return classify(graph) == Fact::Device; }

/// Which graphs a `#gpu` scope covers (plan 2.3): the tagged bodies, plus — transitively — what
/// a device call calls. Mirrors `GpuScopes` (modules/tiles/src/placement.cpp).
using ScopeMap = std::unordered_map<const rt::GCGraph *, const rt::GCGraph *>;

ScopeMap scopeMap(const rt::Program &program) {
    ScopeMap scope;
    std::vector<const rt::GCGraph *> scopes;
    for (const rt::GCGraph *graph : program.graphs()) {
        if (isScope(*graph)) {
            scope.emplace(graph, graph);
            scopes.push_back(graph);
        }
    }
    const auto addCall = [&](const rt::GCGraph &graph, rt::gc_node_ref_t id) {
        if (graph.node(id) == nullptr || graph.node(id)->kind != rt::GCNodeKind::Func) {
            return;
        }
        if (!onDevice(graph, id)) {
            return;
        }
        // A call written under `#cpu` is the host's, whatever calls it: it is not a device call,
        // and the callee it names is not in the scope because of it (plan 2.3).
        const rt::GCGraph *callee = program.calleeOf(graph, id);
        if (callee == nullptr || scope.contains(callee)) {
            return;
        }
        const auto found = scope.find(&graph);
        scope.emplace(callee, found == scope.end() ? &graph : found->second);
        scopes.push_back(callee);
    };
    for (const rt::GCGraph *graph : program.graphs()) {
        for (rt::gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
            if (onDevice(*graph, id)) {
                addCall(*graph, id);
            }
        }
    }
    // A helper of a helper is called by the scope as well.
    for (size_t next = 0; next < scopes.size(); ++next) {
        const rt::GCGraph &graph = *scopes[next];
        for (rt::gc_node_ref_t id = 0; id < graph.nodeCount(); ++id) {
            addCall(graph, id);
        }
    }
    return scope;
}

/// The name the refusal reports the scope under: the graph the scope is, not the helper that
/// happens to hold the call site.
std::string scopeNameOf(const ScopeMap &scope, const rt::GCGraph &graph) {
    const auto found = scope.find(&graph);
    return found == scope.end() || found->second == nullptr ? graph.name() : found->second->name();
}

bool inScope(const ScopeMap &scope, const rt::GCGraph &graph, rt::gc_node_ref_t node) {
    if (onDevice(graph, node)) {
        return true;
    }
    // No fact anywhere around the node: it follows the graph it is written in, and a helper a
    // `#gpu` scope calls is in the scope (plan 2.3).
    return factOf(graph, node) == Fact::None && scope.contains(&graph);
}

/// The `Function` value a static DATA node holds, or nullptr when the node is not one, or holds
/// something else (a closure the run builds, a value from a parameter).
::Function *staticFunctionOf(const rt::GCGraph &graph, rt::gc_node_ref_t ref) {
    const rt::GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != rt::GCNodeKind::Data || node->dataIndex >= 0) {
        return nullptr;
    }
    const size_t slot = static_cast<size_t>(-node->dataIndex);
    if (slot >= graph.staticSlots().size()) {
        return nullptr;
    }
    ty::Type *type = node->dataType();
    if (type == nullptr || type->code() != ty::TypeCode::Function) {
        return nullptr;
    }
    const slot_t value = graph.staticSlots()[slot];
    if (value == NullSlot) {
        return nullptr;
    }
    ::Function *function = td::fromSlot<::Function *>(value);
    return function != nullptr && function->graph() != nullptr ? function : nullptr;
}

/// The same of a draft, the side the pass works on.
::Function *staticFunctionOf(const rt::GraphDraft &draft, rt::gc_node_ref_t ref) {
    const rt::DraftNode *node = draft.node(ref);
    if (node == nullptr || node->header.kind != rt::GCNodeKind::Data ||
        node->header.dataIndex >= 0) {
        return nullptr;
    }
    const size_t slot = static_cast<size_t>(-node->header.dataIndex);
    if (slot >= draft.staticSlots().size()) {
        return nullptr;
    }
    ty::Type *type = node->header.dataType;
    if (type == nullptr || type->code() != ty::TypeCode::Function) {
        return nullptr;
    }
    const slot_t value = draft.staticSlots()[slot];
    if (value == NullSlot) {
        return nullptr;
    }
    ::Function *function = td::fromSlot<::Function *>(value);
    return function != nullptr && function->graph() != nullptr ? function : nullptr;
}

} // namespace

namespace {

/// The expansion of the grad call sites of one pass run.
class ExpandPass final : public RuntimeGraphRewritePass {
  public:
    explicit ExpandPass(const camel::core::context::context_ptr_t &context)
        : RuntimeGraphRewritePass(context) {}

    const rt::Program *apply(const rt::Program *program, std::ostream &out) override {
        (void)out;
        if (program == nullptr) {
            return program;
        }
        const ScopeMap scope = scopeMap(*program);
        rt::RuntimeGraphDraftSession session(context_, program);
        const std::vector<rt::GCGraph *> graphs = session.collectReachableRuntimeGraphs();
        size_t count                            = 0;
        std::string missing;
        for (rt::GCGraph *graph : graphs) {
            std::vector<std::pair<rt::gc_node_ref_t, bool>> sites;
            for (rt::gc_node_ref_t ref = 0; ref < graph->nodeCount() && missing.empty(); ++ref) {
                const rt::GCNode *node = graph->node(ref);
                if (node == nullptr || node->kind != rt::GCNodeKind::Oper) {
                    continue;
                }
                const std::string_view uri = graph->nodeBodyAs<rt::GCOperBody>(ref)->uri();
                if (uri != kGradUri && uri != kWithGradUri) {
                    continue;
                }
                if (!inScope(scope, *graph, ref)) {
                    continue;
                }
                const auto inputs = graph->normInputsOf(ref);
                ::Function *primal =
                    inputs.size() == 1 ? staticFunctionOf(*graph, inputs.front()) : nullptr;
                if (primal == nullptr) {
                    // plan 2.6.3: inside a scope the gradient needs a function known at compile
                    // time; a function value from a parameter, a result or the run is refused
                    // here, at the call site.
                    missing = uri == kGradUri ? "autodiff:grad" : "autodiff:with_grad";
                    report(*graph, ref, scope, missing);
                    break;
                }
                sites.emplace_back(ref, uri == kWithGradUri);
            }
            if (sites.empty()) {
                continue;
            }
            auto &draft = session.edit(graph);
            for (const auto &[ref, withValue] : sites) {
                expandSite(session, draft, ref, withValue);
                ++count;
            }
        }
        if (!missing.empty()) {
            // The refusal is committed; the program is left as it was, so nothing half-expanded
            // is published (tiles::compile refuses its scopes the same way).
            return program;
        }
        return count > 0 ? session.commit() : program;
    }

  private:
    /// plan 2.6.3: the scope cannot run a gradient whose function the run chooses.
    void report(
        const rt::GCGraph &graph, rt::gc_node_ref_t ref, const ScopeMap &scope,
        const std::string &uri) {
        auto diag = context_->rtmDiags()->of(camel::core::error::RuntimeDiag::GpuScopeUnsupported);
        const auto origin = graph.nodeOrigin(ref);
        if (origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(
            scopeNameOf(scope, graph),
            "operator '" + uri + "'",
            "a gradient inside a `#gpu` scope needs a function known at compile time");
    }

    /// Replaces the grad operator at `ref` by the function constant of its derived graph: the
    /// same value the operator returns at run time (operators.cpp::gradFunction), built now.
    void expandSite(
        rt::RuntimeGraphDraftSession &session, rt::GraphDraft &draft, rt::gc_node_ref_t ref,
        bool withValue) {
        const auto inputs  = draft.normInputsOf(ref);
        ::Function *primal = staticFunctionOf(draft, inputs.front());
        const GradientGraph gradient =
            buildGradientGraph(context_, primal->program(), primal->graph(), withValue);
        // The derived graph has to be a graph of the program this pass commits, or the pass that
        // runs next (tiles::compile) never sees it: adopt its program so the commit keeps it.
        session.adoptProgram(*gradient.program);
        auto *result = ::Function::create(
            gradient.program,
            gradient.graph->id(),
            primal->tupleType(),
            camel::core::mm::autoSpace());
        // The gradient graph keeps the primal's closure: it reads the same captured values
        // (operators.cpp::gradFunction).
        if (::Tuple *closure = primal->tuple()) {
            for (size_t i = 0; i < closure->size(); ++i) {
                result->tuple()->set<slot_t>(i, closure->get<slot_t>(i), primal->tupleType());
            }
        }
        ty::Type *type = draft.node(ref)->header.dataType;
        const auto node = draft.materializeStaticValue(
            td::toSlot<::Function *>(result), type, draft.node(ref)->header.runtimeFlags);
        draft.setNodeOrigin(node, draft.nodeOrigin(ref));
        draft.replaceAllValueUses(ref, node);
        // The call site is a call through a value, and a graph's reference table (what the closure
        // walk reads to know which graphs a program holds) records the callees of written calls,
        // not the functions a value carries: the derived graph has to be declared here, or no pass
        // after this one reaches it — `tiles::compile` would not compile its regions and it would
        // run on the host with the scope's device values (docs/design/gpu/plan.cn.md 2.6, and
        // docs/design/gpu/plan-modules.cn.md 5, decision 2 for the call through a known value on
        // the tiles side).
        draft.addStaticGraphRef(gradient.graph->id());
        draft.takeStatementPlace(ref, node);
        draft.eraseNode(ref);
    }
};

} // namespace

void registerExpandPass() {
    registerModulePass(
        "autodiff::expand",
        passWithoutParameters([](const camel::core::context::context_ptr_t &context) {
            return std::make_unique<ExpandPass>(context);
        }));
}

} // namespace camel::autodiff