// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resources of the nodes of a frozen graph (see resources.h).
 *
 * Footprints. The footprint of a graph is collected from its nodes, and a FUNC, LOOP or CALL
 * node's resources come from the footprints of the graphs it runs, so footprints of mutually
 * recursive graphs depend on each other. They are computed as a least fixpoint: a graph met
 * again while its own footprint is being collected contributes what is known of it so far,
 * and the walk is repeated until no footprint grows (each one is a set over a finite domain,
 * so it ends). A footprint that came out of a finished walk is cached by the closure revision of
 * the graph (closureRevision: a new one whenever the graph or a graph it refers to changes) and the
 * operator declaration epoch, which together fix everything it was computed from.
 */

#include "camel/runtime/resources.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/execute/shaped_operators.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/program.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace camel::runtime {

namespace core = camel::core;
using sem::AccessMode;
using sem::ResourceClass;
using sem::ResultAlias;

// ---- Which port of a graph an access came through
// -------------------------------------------------

size_t declaredWithPorts(const GCGraph &graph) {
    const auto with     = graph.withPorts();
    const auto captured = graph.captureNames();
    // A graph whose captures are closure ports holds none of them among its `with` ports, and a
    // table longer than the list is not a graph the builder makes -- and must not underflow a
    // count either. Both read as "every `with` port is declared".
    if (!graph.closureNodes().empty() || captured.size() > with.size()) {
        return with.size();
    }
    return with.size() - captured.size();
}

PortPlacement portPlacement(const GCGraph &graph, gc_node_ref_t ref) {
    const auto closure = graph.closureNodes();
    for (size_t i = 0; i < closure.size(); ++i) {
        if (closure[i] == ref) {
            return {PortGroup::Capture, i};
        }
    }
    const auto with       = graph.withPorts();
    const size_t declared = declaredWithPorts(graph);
    for (size_t i = 0; i < with.size(); ++i) {
        if (with[i] != ref) {
            continue;
        }
        // Past the declared ones, a `with` port is a capture, and its number is its position in
        // the capture table -- not its index among the `with` ports.
        return i < declared ? PortPlacement{PortGroup::With, i}
                            : PortPlacement{PortGroup::Capture, i - declared};
    }
    const auto norm = graph.normPorts();
    for (size_t i = 0; i < norm.size(); ++i) {
        if (norm[i] == ref) {
            return {PortGroup::Norm, i};
        }
    }
    return {};
}

namespace {

// ---- Small helpers ----------------------------------------------------------------------------

/// The value a GATE forwards is camel::runtime::valueBehindGates: its first normal input, then its
/// first context input, then nothing (design principles 3.1;
/// camel/runtime/graph.h). What follows the value in the input list is the reads a region's writes
/// are held by, which gateWaitsOf answers.
using camel::runtime::valueBehindGates;

/// The union of two modes of access to one resource.
AccessMode unite(AccessMode a, AccessMode b) { return a == b ? a : AccessMode::ReadModifyWrite; }

/// Two declared resources may be the same thing: a class matches itself or Unknown, and a
/// Foreign or Ambient resource only one of the same name.
bool compatible(const sem::Resource &a, const sem::Resource &b) {
    if (a.cls == ResourceClass::Unknown || b.cls == ResourceClass::Unknown) {
        return true;
    }
    return a.cls == b.cls && a.name == b.name;
}

/// The Function constant `ref` holds, seen through GATEs: a static DATA node.
const ::Function *staticFunctionOf(const GCGraph &graph, gc_node_ref_t ref) {
    ref                = valueBehindGates(graph, ref);
    const GCNode *node = graph.node(ref);
    if (node->kind != GCNodeKind::Data || node->dataIndex >= 0 || graph.staticArea() == nullptr ||
        node->dataType() == nullptr || node->dataType()->code() != core::type::TypeCode::Function) {
        return nullptr;
    }
    return core::rtdata::fromSlot<::Function *>(
        graph.staticArea()->get<slot_t>(static_cast<size_t>(-node->dataIndex)));
}

/// The function type of a value, or nullptr when the value has no type or is no function. A call
/// through a function value is read from the type of the value it calls (`declaredClauseAt`,
/// `Resolver::call`): the type is where a function value's effect clause is written, and it is the
/// one thing a call site can read when the callee's body is not statically known.
const core::type::FunctionType *functionTypeOfValue(const GCGraph &graph, gc_node_ref_t value) {
    const GCNode *node = graph.node(value);
    if (node == nullptr || node->dataType() == nullptr ||
        node->dataType()->code() != core::type::TypeCode::Function) {
        return nullptr;
    }
    return static_cast<const core::type::FunctionType *>(node->dataType());
}

// ---- Footprints --------------------------------------------------------------------------------

struct FootprintCache {
    std::mutex mutex;
    /// (closure revision, declaration epoch) -> footprint of a finished walk.
    std::map<std::pair<uint64_t, uint64_t>, GraphFootprint> entries;
};

FootprintCache &footprintCache() {
    static FootprintCache cache;
    return cache;
}

std::optional<GraphFootprint>
cachedFootprint(const Program &program, const GCGraph *graph, uint64_t epoch) {
    FootprintCache &cache = footprintCache();
    std::lock_guard lock(cache.mutex);
    if (auto it =
            cache.entries.find({closureRevision(program, const_cast<GCGraph *>(graph)), epoch});
        it != cache.entries.end()) {
        return it->second;
    }
    return std::nullopt;
}

void addAccess(GraphFootprint &fp, sem::ResourceAccess access) {
    access.region = sem::ResourceRegion{};
    for (sem::ResourceAccess &known : fp.accesses) {
        if (known.resource == access.resource && known.binding == access.binding) {
            known.mode = unite(known.mode, access.mode);
            return;
        }
    }
    fp.accesses.push_back(std::move(access));
}

void addAny(GraphFootprint &fp, ResourceClass cls) {
    if (std::find(fp.anyOf.begin(), fp.anyOf.end(), cls) == fp.anyOf.end()) {
        fp.anyOf.push_back(cls);
    }
}

/// Everything in `from` added to `into`; true when `into` grew.
bool merge(GraphFootprint &into, const GraphFootprint &from) {
    const GraphFootprint before = into;
    for (const sem::ResourceAccess &access : from.accesses) {
        addAccess(into, access);
    }
    for (ResourceClass cls : from.anyOf) {
        addAny(into, cls);
    }
    into.external |= from.external;
    into.result = from.result;
    into.declared &= from.declared;
    return !(into == before);
}

/// The walk that collects footprints (see the file comment).
class FootprintWalk {
  public:
    FootprintWalk(const Program &program, uint64_t epoch) : program_(program), epoch_(epoch) {}

    /// The footprint of `graph`, as far as the walk knows it.
    GraphFootprint of(const GCGraph *graph);

    /// The fixpoint for `graph`, cached.
    GraphFootprint run(const GCGraph *graph) {
        if (graph == nullptr) {
            return {};
        }
        if (auto cached = cachedFootprint(program_, graph, epoch_)) {
            return *cached;
        }
        for (;;) {
            done_.clear();
            grew_     = false;
            recursed_ = false;
            of(graph);
            if (!recursed_ || !grew_) {
                break;
            }
        }
        FootprintCache &cache = footprintCache();
        std::lock_guard lock(cache.mutex);
        for (const auto &[g, fp] : memo_) {
            cache.entries.insert_or_assign(
                {closureRevision(program_, const_cast<GCGraph *>(g)), epoch_},
                fp);
        }
        return memo_[graph];
    }

  private:
    // program-holder-exempt: borrow-or-foreign-type -- the walk reads the program its caller
    // holds for the footprint.
    const Program &program_;
    uint64_t epoch_;
    std::unordered_map<const GCGraph *, GraphFootprint> memo_;
    std::unordered_set<const GCGraph *> done_;
    std::unordered_set<const GCGraph *> active_;
    bool grew_     = false;
    bool recursed_ = false;
};

/// Resolves the resources of nodes. The footprints of the graphs a node runs come from
/// `footprint`: the walk under way while a footprint is being collected, the cached fixpoint
/// otherwise.
class Resolver {
  public:
    using FootprintFn = std::function<GraphFootprint(const GCGraph *)>;
    Resolver(const Program &program, FootprintFn footprint)
        : program_(program), footprint_(std::move(footprint)) {}

    NodeResources node(const GCGraph &graph, gc_node_ref_t ref) const;

    /// The origin of the value of `ref`, through GATEs and through Same/View results (a call's
    /// result, an in-place operator's result, an assignment's result -- the cell that write
    /// wrote into) to the port or allocation it comes from.
    ValueOrigin originOf(const GCGraph &graph, gc_node_ref_t ref) const;
    bool mayAlias(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) const;

    /// Where the graph's return value comes from, as a `ResultAlias` whose operand is a norm
    /// port (design effects plan 3.3). Fresh when it makes a new resource, Same when it returns
    /// one of its norm ports. A view of a port is Same at the footprint: the whole port.
    sem::ResultAlias resultOf(const GCGraph &graph, gc_node_ref_t returnValue) const;

  private:
    /// The accesses of the node, as the graph and the declarations give them: an external effect
    /// that names no resource is added by `node` itself.
    NodeResources accessesOf(const GCGraph &graph, gc_node_ref_t ref) const;
    NodeResources oper(const GCGraph &graph, gc_node_ref_t ref) const;
    NodeResources call(const GCGraph &graph, gc_node_ref_t ref) const;
    NodeResources loop(const GCGraph &graph, gc_node_ref_t ref) const;

    /// The resources of an operator that runs a function the program hands it
    /// (`sem::ResourceSpec::functionOperand`): the clause of the function value that arrives in the
    /// operand the declaration names. Never "touches nothing" -- an unreadable value keeps the
    /// conservative answer, because the operator runs code this graph does not hold.
    NodeResources fromFunctionOperand(
        const GCGraph &graph, gc_node_ref_t ref, const sem::ResourceSpec &spec,
        const sem::OperatorSemantics *facts) const;

    /// The footprint `fp` of a graph run by node `ref`, with each binding resolved to an input
    /// of the node by `bind` (kInvalidNodeRef: the resource cannot be told).
    template <typename Bind>
    NodeResources fromFootprint(const GraphFootprint &fp, size_t normCount, Bind &&bind) const;

    /// The resources of a call whose callee's type publishes a Concrete signature (design
    /// effects plan 2.3): each use binds to the call's argument by the callee's numbering.
    /// `withArgBase` is where that numbering's `with` parameters start among this node's `with`
    /// inputs: after the callee for a call through a value, after the function operand for an
    /// operator that runs one, and at 0 for an operator's own `with` arguments as written.
    NodeResources fromSignature(
        const sem::ResourceSignature &signature, size_t withCount, const GCGraph &graph,
        gc_node_ref_t ref, size_t withArgBase) const;

    // program-holder-exempt: borrow-or-foreign-type -- the resolver reads the program its
    // caller holds for the query.
    const Program &program_;
    FootprintFn footprint_;
};

GraphFootprint FootprintWalk::of(const GCGraph *graph) {
    if (graph == nullptr) {
        return {};
    }
    if (done_.contains(graph)) {
        return memo_[graph];
    }
    if (active_.contains(graph)) {
        // A recursion: what is known of it so far; the walk is repeated until nothing grows.
        recursed_ = true;
        return memo_[graph];
    }
    if (auto cached = cachedFootprint(program_, graph, epoch_)) {
        memo_[graph] = *cached;
        done_.insert(graph);
        return *cached;
    }
    active_.insert(graph);
    Resolver resolver(program_, [this](const GCGraph *callee) { return of(callee); });
    GraphFootprint fp;
    for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
        const NodeResources r = resolver.node(*graph, ref);
        fp.external |= r.external;
        fp.declared &= r.declared;
        for (const NodeAccess &access : r.accesses) {
            if (access.anyValue) {
                addAny(fp, access.resource.cls);
                continue;
            }
            if (access.resource.cls == ResourceClass::Ambient) {
                addAccess(fp, sem::ambientAccess(access.resource.name, access.mode));
                continue;
            }
            const ValueOrigin origin = resolver.originOf(*graph, access.value);
            if (origin.kind == ValueOrigin::Kind::Fresh) {
                continue; // made by this activation: invisible to its caller
            }
            if (origin.kind == ValueOrigin::Kind::Unknown) {
                addAny(fp, access.resource.cls);
                continue;
            }
            // A port: which one, and how the graph's own numbering names it. `portPlacement` is the
            // one reading of that question: a `with` port past the declared ones is a capture, and
            // its number is the capture's position -- not the port's index among the `with` ports,
            // which is what a declared clause never says (`Builder::numberBodyPorts`).
            const camel::runtime::PortPlacement placement =
                camel::runtime::portPlacement(*graph, origin.node);
            sem::ResourceBinding binding;
            switch (placement.group) {
            case camel::runtime::PortGroup::Norm:
                binding = {sem::ResourceBinding::Kind::Norm, static_cast<uint8_t>(placement.index)};
                break;
            case camel::runtime::PortGroup::With:
                binding = {sem::ResourceBinding::Kind::With, static_cast<uint8_t>(placement.index)};
                break;
            case camel::runtime::PortGroup::Capture:
                binding = {
                    sem::ResourceBinding::Kind::Capture,
                    static_cast<uint8_t>(placement.index)};
                break;
            case camel::runtime::PortGroup::None:
                // A port of no group: the graph does not number it, so no clause can name it. That
                // is an access of no nameable resource, not a capture whose number might match one
                // a clause wrote, so it is read as unknown of its class -- the reading a declared
                // clause can only cover through `any`.
                addAny(fp, access.resource.cls);
                continue;
            }
            addAccess(
                fp,
                sem::ResourceAccess{
                    .resource = access.resource,
                    .binding  = binding,
                    .region   = sem::ResourceRegion{},
                    .mode     = access.mode,
                });
        }
    }
    // What the graph hands back (design effects plan 3.3). No return value, or no return:
    // nothing of the caller's escapes, which is fresh.
    const gc_node_ref_t returned = graph->returnNodeRef();
    if (graph->returnKind() != GCReturnKind::None && graph->containsNodeRef(returned)) {
        fp.result = resolver.resultOf(*graph, returned);
    } else {
        fp.result = sem::ResultAlias{.kind = sem::ResultAlias::Kind::Fresh};
    }
    active_.erase(graph);
    grew_ |= merge(memo_[graph], fp);
    done_.insert(graph);
    return memo_[graph];
}

template <typename Bind>
NodeResources
Resolver::fromFootprint(const GraphFootprint &fp, size_t normCount, Bind &&bind) const {
    NodeResources r;
    r.external = fp.external;
    r.declared = fp.declared;
    // Where the callee's return comes from, in the caller's numbering: its norm ports are the
    // node's ordinary inputs.
    r.result = fp.result;
    if ((r.result.kind == ResultAlias::Kind::Same || r.result.kind == ResultAlias::Kind::View) &&
        r.result.operand >= normCount) {
        r.result = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    }
    for (const sem::ResourceAccess &access : fp.accesses) {
        NodeAccess a{.resource = access.resource, .mode = access.mode};
        if (access.binding.kind != sem::ResourceBinding::Kind::Ambient) {
            a.value    = bind(access.binding);
            a.anyValue = a.value == kInvalidNodeRef;
        }
        r.accesses.push_back(a);
    }
    for (ResourceClass cls : fp.anyOf) {
        r.accesses.push_back(NodeAccess{.resource = {.cls = cls}, .anyValue = true});
    }
    return r;
}

/// The closure a node runs, when it is one this graph holds: the function value stands at With
/// `withArgBase - 1` (`core::functionValueWithBase`: the callee for a call through a value, the
/// operand the declaration names for an operator that runs one), and a closure built here is a
/// FILL of a function constant whose trailing `with` inputs hold the captured values, in capture
/// order (design principles 2.1; `Builder::captureInto`).
struct ClosureAt {
    /// The body behind the value, when the program names one statically.
    const ::Function *function = nullptr;
    /// The captured values, by capture position; kInvalidNodeRef for a position this graph does
    /// not hand over. Empty for a function constant, which captures nothing.
    std::vector<gc_node_ref_t> captures;
};

ClosureAt closureAt(const GCGraph &graph, gc_node_ref_t ref, size_t withArgBase) {
    ClosureAt out;
    const auto with = graph.withInputsOf(ref);
    if (withArgBase == 0 || withArgBase > with.size()) {
        return out;
    }
    const gc_node_ref_t value = valueBehindGates(graph, with[withArgBase - 1]);
    if (const ::Function *fn = staticFunctionOf(graph, value)) {
        return ClosureAt{.function = fn};
    }
    const GCNode *node = graph.node(value);
    if (node == nullptr || node->kind != GCNodeKind::Fill || graph.normInputsOf(value).empty()) {
        return out;
    }
    const ::Function *fn = staticFunctionOf(graph, graph.normInputsOf(value)[0]);
    const auto *body     = graph.nodeBodyAs<GCFillBody>(value);
    if (fn == nullptr || body == nullptr) {
        return out;
    }
    out.function      = fn;
    const auto slots  = body->slots();
    const auto inputs = graph.withInputsOf(value);
    for (size_t i = 0; i < slots.size() && i < inputs.size(); ++i) {
        const size_t position = static_cast<size_t>(slots[i]);
        if (out.captures.size() <= position) {
            out.captures.resize(position + 1, kInvalidNodeRef);
        }
        out.captures[position] = inputs[i];
    }
    return out;
}

NodeResources Resolver::node(const GCGraph &graph, gc_node_ref_t ref) const {
    NodeResources r = accessesOf(graph, ref);
    // An effect outside the program that names no resource at all -- an operator whose declaration
    // gives it an external effect and no access, or the conservative reading of an undeclared one
    // with nothing to bind to -- is the "global" resource: it may be any resource, so it keeps its
    // order with every effect (design principles 3.3). The model carries that as one access of
    // unknown class with `anyValue`: compatible() meets the access of any resource, and the other
    // side of the pair is asked for nothing. Two effects that name *different* resources stay
    // unrelated, which is the whole point of judging a conflict by resource (F9).
    if (r.external && r.accesses.empty()) {
        r.accesses.push_back(
            NodeAccess{
                .resource = {},
                .mode     = AccessMode::ReadModifyWrite,
                .region   = sem::ResourceRegion{.kind = sem::ResourceRegion::Kind::Unknown},
                .anyValue = true,
            });
    }
    return r;
}

NodeResources Resolver::accessesOf(const GCGraph &graph, gc_node_ref_t ref) const {
    switch (graph.node(ref)->kind) {
    case GCNodeKind::Oper:
        return oper(graph, ref);
    case GCNodeKind::Func: {
        const auto norm       = graph.normInputsOf(ref);
        const GCGraph *callee = program_.calleeOf(graph, ref);
        if (callee != nullptr && callee->funcType() != nullptr &&
            callee->funcType()->resources().mode == sem::ResourceSignature::Mode::Concrete &&
            callee->funcType()->resources().captures.empty()) {
            return fromSignature(
                callee->funcType()->resources(),
                callee->funcType()->withTypesCount(),
                graph,
                ref,
                /*withArgBase=*/0);
        }
        const auto with = graph.withInputsOf(ref);
        // A direct call hands its callee's captures in as `with` inputs after the declared `with`
        // arguments (`Builder::linkCaptureSites`), so a capture names a value this node does hold,
        // at the position the callee's own port numbering puts it in. Reading it as unknown instead
        // makes the caller carry an `any` of the capture's class -- a resource the call does not
        // reach, held for a value the caller is holding (design principles 3.3, effect masking).
        const size_t declared = callee != nullptr ? camel::runtime::declaredWithPorts(*callee) : 0;
        return fromFootprint(
            footprint_(callee),
            norm.size(),
            [&](const sem::ResourceBinding &b) -> gc_node_ref_t {
                switch (b.kind) {
                case sem::ResourceBinding::Kind::Norm:
                    return b.index < norm.size() ? norm[b.index] : kInvalidNodeRef;
                case sem::ResourceBinding::Kind::With:
                    return b.index < with.size() ? with[b.index] : kInvalidNodeRef;
                case sem::ResourceBinding::Kind::Capture: {
                    const size_t at = declared + size_t(b.index);
                    return at < with.size() ? with[at] : kInvalidNodeRef;
                }
                default:
                    return kInvalidNodeRef;
                }
            });
    }
    case GCNodeKind::Call:
        return call(graph, ref);
    case GCNodeKind::Loop:
        return loop(graph, ref);
    default:
        // A copy is a new object nobody else refers to, and no other kind reaches a resource
        // through this node: the answer is the same one, so it is asked once (the copy is a
        // `Clone` by its shape, `camel/execute/shaped_operators.h`).
        return NodeResources{};
    }
}

NodeResources Resolver::oper(const GCGraph &graph, gc_node_ref_t ref) const {
    const auto &table               = core::OperatorTable::instance();
    const core::OperId id           = graph.nodeBodyAs<GCOperBody>(ref)->oper;
    const auto traits               = table.find(id);
    const sem::OperatorSemantics *s = table.semantics(id);
    const auto norm                 = graph.normInputsOf(ref);
    const auto with                 = graph.withInputsOf(ref);

    NodeResources r;
    sem::ResourceSpec spec;
    if (s != nullptr && s->resources) {
        spec = *s->resources;
    } else {
        spec = sem::conservativeResources(
            norm.size(),
            with.size(),
            traits && traits->pure,
            table.hasExternalEffect(id));
        r.declared = traits && traits->pure;
    }
    r.result    = spec.result;
    r.allocates = spec.allocates;
    r.external  = spec.externalEffect;
    if (spec.functionOperand) {
        // An operator that runs a function the program hands it has that function's accesses and
        // none of its own (design principles 3.3; `checkResources` refuses the two together), so
        // this node's footprint is read off the value the declaration names. Reading the spec's
        // empty access list here is what made such a node "touches nothing" while it ran code.
        return fromFunctionOperand(graph, ref, spec, s);
    }
    for (const sem::ResourceAccess &access : spec.accesses) {
        NodeAccess a{.resource = access.resource, .mode = access.mode, .region = access.region};
        a.unordered = access.unordered;
        switch (access.binding.kind) {
        case sem::ResourceBinding::Kind::Norm:
            a.value =
                access.binding.index < norm.size() ? norm[access.binding.index] : kInvalidNodeRef;
            break;
        case sem::ResourceBinding::Kind::With:
            a.value =
                access.binding.index < with.size() ? with[access.binding.index] : kInvalidNodeRef;
            break;
        default:
            break;
        }
        // An access of unknown class (the conservative reading) may reach anything its operand
        // holds, not only the operand itself; a missing operand cannot be told either.
        a.anyValue = access.resource.cls != ResourceClass::Ambient &&
                     (access.resource.cls == ResourceClass::Unknown || a.value == kInvalidNodeRef);
        if (a.anyValue) {
            a.value = kInvalidNodeRef;
        }
        r.accesses.push_back(a);
    }
    return r;
}

NodeResources Resolver::fromFunctionOperand(
    const GCGraph &graph, gc_node_ref_t ref, const sem::ResourceSpec &spec,
    const sem::OperatorSemantics *facts) const {
    const sem::ResourceBinding &where = *spec.functionOperand;
    // What the operator runs is the value in the operand the declaration names, or nothing this
    // reading can see: an absent operand, a value that is not a function, and a type that writes
    // no clause (`Generic`) are the same answer as an unreadable callee gets in `call`.
    const core::FunctionValueClause site = clauseOfOperand(graph, ref, where);
    NodeResources r;
    if (!site.clause.has_value()) {
        r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
        r.result   = ResultAlias{.kind = ResultAlias::Kind::Unknown};
        r.external = true;
        r.declared = false;
        return r;
    }
    // The clause is read where a call through a value reads it, and its parameters are placed on
    // the operand the call would pass: the function value stands where a callee stands, so the
    // clause's `with` parameters start one past it -- the base is the shared reading's, not this
    // site's. An operator that takes the function as an ordinary operand, or one whose shape
    // (`ElementCalls`) makes the callback's parameters something other than the node's operands,
    // cannot place them -- and a use that cannot be placed is "some resource of its class", the
    // reading `fromSignature` gives a capture, never a dropped one.
    const bool folds = facts != nullptr && facts->elementCalls &&
                       facts->elementCalls->shape == sem::ElementShape::Fold;
    if (where.kind == sem::ResourceBinding::Kind::With && !folds) {
        r = fromSignature(*site.clause, site.withCount, graph, ref, site.withArgBase);
    } else {
        const sem::ResourceSignature &signature = *site.clause;
        r.result                                = signature.result;
        r.allocates                             = signature.allocates;
        r.external                              = signature.external;
        for (const auto &use : signature.params) {
            r.accesses.push_back(
                NodeAccess{
                    .resource = {.cls = use.cls, .name = use.name},
                    .mode     = use.mode,
                    .anyValue = true,
                });
        }
        for (const auto &use : signature.ambient) {
            r.accesses.push_back(
                NodeAccess{
                    .resource  = {.cls = ResourceClass::Ambient, .name = use.name},
                    .mode      = use.mode,
                    .unordered = use.unordered,
                });
        }
        for (ResourceClass cls : signature.anyOf) {
            r.accesses.push_back(NodeAccess{.resource = {.cls = cls}, .anyValue = true});
        }
        for ([[maybe_unused]] uint8_t capture : signature.captures) {
            r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
        }
        r.declared = false;
    }
    // What the operator gives back is the operator's: `map` answers a new array even through a
    // callback that hands its argument back, so the function's result alias is not this node's.
    r.result    = spec.result;
    r.allocates = spec.allocates;
    return r;
}

NodeResources Resolver::call(const GCGraph &graph, gc_node_ref_t ref) const {
    const auto norm = graph.normInputsOf(ref);
    const auto with = graph.withInputsOf(ref);
    // The callee is With input 0: a known function when it is a constant, or a closure this
    // graph builds from one (a FILL of a constant function and the captured values).
    const ClosureAt closure = closureAt(graph, ref, /*withArgBase=*/1);
    const GCGraph *callee   = closure.function != nullptr ? closure.function->graph() : nullptr;
    const std::vector<gc_node_ref_t> &captures = closure.captures;
    if (callee == nullptr) {
        // A call through a value: the value's function type is where its effect is written, and it
        // is the one thing a call site can read when the callee's body is not statically known --
        // `declaredClauseAt` reads the same place, through the same reading. A type that writes a
        // clause is read as written, `pure` included: `pure` is the clause that names nothing, so
        // the call reaches nothing and is not an effect. A type that promises nothing (a `Generic`
        // clause: no clause written, `docs/design/effects/plan.cn.md` 2.2) is not a reading at all,
        // so the call keeps the conservative one: recording it as `unknown` is the honest answer
        // about a value whose reach the program does not state. Reaching that fallback is a fact
        // about the type, not about the call site; one type that lands there is the one
        // `autodiff`'s `grad` answers with.
        if (!with.empty()) {
            const core::FunctionValueClause site = clauseOfCall(graph, ref);
            // The clause is read in one place (`core::functionValueClause`, through the one lookup
            // below), together with the base its `with` parameters land at; a `captures` list is
            // not a reason to refuse the reading, because `fromSignature` reads each capture's
            // class off the value's own type and never drops the access it stands for. What the
            // type cannot say is what an absent clause (`Generic`) says, and that keeps the
            // conservative answer below.
            if (site.clause.has_value()) {
                return fromSignature(*site.clause, site.withCount, graph, ref, site.withArgBase);
            }
        }
        // No clause to read: anything may happen.
        NodeResources r;
        r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
        r.result   = ResultAlias{.kind = ResultAlias::Kind::Unknown};
        r.external = true;
        r.declared = false;
        return r;
    }
    // A callee whose type publishes its resources: the call takes its footprint from the type
    // (design effects plan 2.3), not by walking the body. A Generic type (a Camel body, until
    // its signature is inferred) keeps the body walk. So does a signature that names a capture: it
    // says which value the callee captured, and the walk reads that use back onto the very value
    // this graph handed over (design principles 3.3, effect masking) -- one resource per value,
    // where the type alone would have had to settle for the class. A closure that writes a `var`
    // it captured carries the capture in its type, so this is the common case for one.
    if (callee->funcType() != nullptr &&
        callee->funcType()->resources().mode == sem::ResourceSignature::Mode::Concrete &&
        callee->funcType()->resources().captures.empty()) {
        return fromSignature(
            callee->funcType()->resources(),
            callee->funcType()->withTypesCount(),
            graph,
            ref,
            /*withArgBase=*/1);
    }
    return fromFootprint(
        footprint_(callee),
        norm.size(),
        [&](const sem::ResourceBinding &b) -> gc_node_ref_t {
            switch (b.kind) {
            case sem::ResourceBinding::Kind::Norm:
                return b.index < norm.size() ? norm[b.index] : kInvalidNodeRef;
            case sem::ResourceBinding::Kind::With:
                // With input 0 is the callee; the explicit with arguments follow it.
                return size_t(b.index) + 1 < with.size() ? with[b.index + 1] : kInvalidNodeRef;
            case sem::ResourceBinding::Kind::Capture:
                return b.index < captures.size() ? captures[b.index] : kInvalidNodeRef;
            default:
                return kInvalidNodeRef;
            }
        });
}

NodeResources Resolver::fromSignature(
    const sem::ResourceSignature &signature, size_t withCount, const GCGraph &graph,
    gc_node_ref_t ref, size_t withArgBase) const {
    const auto norm = graph.normInputsOf(ref);
    const auto with = graph.withInputsOf(ref);
    NodeResources r;
    r.result    = signature.result;
    r.allocates = signature.allocates;
    r.external  = signature.external;
    // The signature's operand numbering is the callee's: with parameters first, ordinary ones
    // after. It is the same mapping as fromFootprint's bind, with the class the declaration gave
    // the parameter.
    const auto argument = [&](uint8_t index) -> gc_node_ref_t {
        if (index < withCount) {
            const size_t at = size_t(index) + withArgBase;
            return at < with.size() ? with[at] : kInvalidNodeRef;
        }
        const size_t at = size_t(index) - withCount;
        return at < norm.size() ? norm[at] : kInvalidNodeRef;
    };
    for (const auto &use : signature.params) {
        NodeAccess a{.resource = {.cls = use.cls, .name = use.name}, .mode = use.mode};
        a.value    = argument(use.index);
        a.anyValue = use.cls == ResourceClass::Unknown || a.value == kInvalidNodeRef;
        if (a.anyValue) {
            a.value = kInvalidNodeRef;
        }
        r.accesses.push_back(std::move(a));
    }
    for (const auto &use : signature.ambient) {
        r.accesses.push_back(
            NodeAccess{
                .resource  = {.cls = ResourceClass::Ambient, .name = use.name},
                .mode      = use.mode,
                .unordered = use.unordered,
            });
    }
    for (ResourceClass cls : signature.anyOf) {
        r.accesses.push_back(NodeAccess{.resource = {.cls = cls}, .anyValue = true});
    }
    // A capture is an extra argument the type names by position and nothing else
    // (`ResourceSignature::captures` is a list of positions; design effects plan 3.4's table of the
    // graph data model: after a direct call's `with` arguments, and inside the FILL that builds the
    // closure when it is a function value). Its class therefore comes from *the captured value's
    // type*, never from a guess:
    //
    //   * a handle (`Ref<T>`, a `#resource` declaration) is one resource per value (design effects
    //     plan 3.2), so the class is the declaration's name for it and the value says which one.
    //     When this graph hands that value over -- a direct call's `with` argument, a FILL's input
    //     for a closure built here -- the access names it; when it does not, the access is "one
    //     handle of that class", which is an unknown *of a class* and not the global unknown the
    //     design keeps for state that names nothing (design principles 3.3);
    //   * a function value's reach is its type's clause, read the way a function *argument* is
    //     read: its parameters have no operand to land on here, because the call of it is inside
    //     the closure's body;
    //   * a value with no identity is no resource at all -- copying it into the closure is not an
    //     effect a caller can observe (design principles 10.5), so it produces no access.
    //
    // Only a capture whose value this graph neither holds nor types keeps the conservative answer,
    // and that is the opaque case: nothing says which resource it is.
    const ClosureAt closure = closureAt(graph, ref, withArgBase);
    const GCGraph *body     = closure.function != nullptr ? closure.function->graph() : nullptr;
    for (uint8_t position : signature.captures) {
        const gc_node_ref_t at = size_t(position) < closure.captures.size()
                                     ? closure.captures[position]
                                     : kInvalidNodeRef;
        const GCNode *node       = at != kInvalidNodeRef ? graph.node(at) : nullptr;
        const core::type::Type *type = node != nullptr ? node->dataType() : nullptr;
        if (type == nullptr && body != nullptr) {
            // The call site does not hand the value over (a closure called where it was not made):
            // the capture port of its own body is the same value's type, read where it was built.
            const auto ports = body->closureNodes();
            if (size_t(position) < ports.size() && body->node(ports[position]) != nullptr) {
                type = body->node(ports[position])->dataType();
            }
        }
        if (type == nullptr) {
            r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
            r.declared = false;
            continue;
        }
        if (sem::isHandleType(type)) {
            NodeAccess access{
                .resource = {.cls = ResourceClass::Handle, .name = sem::handleResourceName(type)},
            };
            access.value    = at;
            access.anyValue = at == kInvalidNodeRef;
            r.accesses.push_back(std::move(access));
            continue;
        }
        if (type->code() == core::type::TypeCode::Function) {
            const auto &captured = static_cast<const core::type::FunctionType *>(type)->resources();
            if (captured.mode == sem::ResourceSignature::Mode::Concrete) {
                for (const auto &use : captured.params) {
                    r.accesses.push_back(
                        NodeAccess{
                            .resource = {.cls = use.cls, .name = use.name},
                            .mode     = use.mode,
                            .anyValue = true,
                        });
                }
                for (const auto &use : captured.ambient) {
                    r.accesses.push_back(
                        NodeAccess{
                            .resource  = {.cls = ResourceClass::Ambient, .name = use.name},
                            .mode      = use.mode,
                            .unordered = use.unordered,
                        });
                }
                for (ResourceClass cls : captured.anyOf) {
                    r.accesses.push_back(NodeAccess{.resource = {.cls = cls}, .anyValue = true});
                }
            }
            // What that function captured is no value this graph holds, and its type does not say
            // what it is: those positions keep the conservative answer, as they always did.
            for ([[maybe_unused]] uint8_t nested : captured.captures) {
                r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
                r.declared = false;
            }
            continue;
        }
        // Any other type: a copy of it inside the closure reaches nothing its caller holds.
    }
    if ((r.result.kind == ResultAlias::Kind::Same || r.result.kind == ResultAlias::Kind::View) &&
        r.result.operand >= norm.size()) {
        r.result = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    }
    return r;
}

NodeResources Resolver::loop(const GCGraph &, gc_node_ref_t) const {
    // The condition and the body are regions of the same graph: the walk visits their nodes like
    // any other, so the loop node itself only says that its result is anything.
    NodeResources r;
    r.result = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    return r;
}

ValueOrigin Resolver::originOf(const GCGraph &graph, gc_node_ref_t ref) const {
    for (gc_cnt_t hops = 0; hops <= graph.nodeCount() && graph.containsNodeRef(ref); ++hops) {
        ref                = valueBehindGates(graph, ref);
        const GCNode *node = graph.node(ref);
        // A copy and a construction make a value of their own: asked of the operator the node
        // names, or of the kind for the form the front end still builds
        // (`camel/execute/shaped_operators.h`, refactor stage 2.3).
        const camel::execute::OperShape shape = camel::execute::shapeOfOperation(graph, ref);
        if (shape == camel::execute::OperShape::Clone ||
            shape == camel::execute::OperShape::Construction) {
            return ValueOrigin{ValueOrigin::Kind::Fresh, ref};
        }
        switch (node->kind) {
        case GCNodeKind::Port:
            return ValueOrigin{ValueOrigin::Kind::Port, ref};
        case GCNodeKind::Oper:
        case GCNodeKind::Func:
        case GCNodeKind::Call:
        case GCNodeKind::Loop: {
            const ResultAlias result = this->node(graph, ref).result;
            if (result.kind == ResultAlias::Kind::Fresh) {
                return ValueOrigin{ValueOrigin::Kind::Fresh, ref};
            }
            const auto norm = graph.normInputsOf(ref);
            if ((result.kind == ResultAlias::Kind::Same ||
                 result.kind == ResultAlias::Kind::View) &&
                result.operand < norm.size()) {
                ref = norm[result.operand];
                continue;
            }
            return ValueOrigin{ValueOrigin::Kind::Unknown, ref};
        }
        default:
            return ValueOrigin{ValueOrigin::Kind::Unknown, ref};
        }
    }
    return ValueOrigin{ValueOrigin::Kind::Unknown, ref};
}

bool Resolver::mayAlias(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) const {
    const ValueOrigin x = originOf(graph, a);
    const ValueOrigin y = originOf(graph, b);
    if (x.kind == ValueOrigin::Kind::Unknown || y.kind == ValueOrigin::Kind::Unknown || x == y) {
        return true;
    }
    // Two different fresh allocations, or a fresh one and a received value: different objects.
    // Two received values may be one (a caller passes one cell twice).
    return x.kind == ValueOrigin::Kind::Port && y.kind == ValueOrigin::Kind::Port;
}

sem::ResultAlias Resolver::resultOf(const GCGraph &graph, gc_node_ref_t value) const {
    for (gc_cnt_t hops = 0; hops <= graph.nodeCount() && graph.containsNodeRef(value); ++hops) {
        value              = valueBehindGates(graph, value);
        const GCNode *node = graph.node(value);
        // A copy and a construction make a value of their own: asked of the operator the node
        // names, or of the kind for the form the front end still builds
        // (`camel/execute/shaped_operators.h`, refactor stage 2.3).
        const camel::execute::OperShape shape = camel::execute::shapeOfOperation(graph, value);
        if (shape == camel::execute::OperShape::Clone ||
            shape == camel::execute::OperShape::Construction) {
            return sem::ResultAlias{.kind = ResultAlias::Kind::Fresh};
        }
        switch (node->kind) {
        case GCNodeKind::Port: {
            const auto norm = graph.normPorts();
            const auto at   = std::find(norm.begin(), norm.end(), value);
            // A with port or a capture is not a norm port of the footprint; the footprint has no
            // binding for it yet.
            return at == norm.end() ? sem::ResultAlias{.kind = ResultAlias::Kind::Unknown}
                                    : sem::ResultAlias{
                                          .kind    = ResultAlias::Kind::Same,
                                          .operand = static_cast<uint8_t>(at - norm.begin())};
        }
        case GCNodeKind::Oper:
        case GCNodeKind::Func:
        case GCNodeKind::Call:
        case GCNodeKind::Loop: {
            const ResultAlias result = this->node(graph, value).result;
            if (result.kind == ResultAlias::Kind::Fresh ||
                result.kind == ResultAlias::Kind::Unknown) {
                return result;
            }
            const auto norm = graph.normInputsOf(value);
            if ((result.kind == ResultAlias::Kind::Same ||
                 result.kind == ResultAlias::Kind::View) &&
                result.operand < norm.size()) {
                value = norm[result.operand];
                continue;
            }
            return sem::ResultAlias{.kind = ResultAlias::Kind::Unknown};
        }
        default:
            return sem::ResultAlias{.kind = ResultAlias::Kind::Unknown};
        }
    }
    return sem::ResultAlias{.kind = ResultAlias::Kind::Unknown};
}

// ---- Regions -----------------------------------------------------------------------------------

/// Whether two accesses' regions may overlap (only meaningful for one resource).
bool regionsOverlap(
    const GCGraph &graph, gc_node_ref_t a, const NodeAccess &x, gc_node_ref_t b,
    const NodeAccess &y) {
    const auto boundsAt = [&](gc_node_ref_t node,
                              const NodeAccess &access) -> std::optional<RegionBounds> {
        if (access.region.kind != sem::ResourceRegion::Kind::Mapped) {
            return std::nullopt;
        }
        const auto instance = instanceOf(graph, node);
        return instance ? regionBounds(*instance, access.region) : std::nullopt;
    };
    return regionsMayOverlap(boundsAt(a, x), boundsAt(b, y));
}

// ---- Paths -------------------------------------------------------------------------------------

/// Whether `to` reaches `from` backwards through inputs (Norm and With; the three kinds that order
/// -- control, order and sync -- too when asked).
bool reaches(const GCGraph &graph, gc_node_ref_t from, gc_node_ref_t to, bool ctrl) {
    std::vector<bool> seen(graph.nodeCount(), false);
    std::vector<gc_node_ref_t> work{from};
    const auto visitOne = [&](gc_node_ref_t input) {
        if (graph.containsNodeRef(input) && !seen[input]) {
            seen[input] = true;
            work.push_back(input);
        }
    };
    const auto visit = [&](std::span<const gc_node_ref_t> inputs) {
        for (gc_node_ref_t input : inputs) {
            visitOne(input);
        }
    };
    while (!work.empty()) {
        const gc_node_ref_t ref = work.back();
        work.pop_back();
        visit(graph.normInputsOf(ref));
        visit(graph.withInputsOf(ref));
        if (ctrl) {
            visit(graph.ctrlInputsOf(ref));
            // A sync edge carries the order of the statement written directly before it, and an
            // order edge only orders: both say "this runs after that", which is the question here.
            for (const GCGraphSyncEdge &edge : graph.syncPredsOf(ref)) {
                visitOne(edge.pred);
            }
            for (const GCGraphOrderEdge &edge : graph.orderPredsOf(ref)) {
                visitOne(edge.pred);
            }
        }
        if (graph.containsNodeRef(to) && seen[to]) {
            return true;
        }
    }
    return false;
}

std::string accessText(const NodeAccess &access) { return toString(access); }

} // namespace

// ---- Public queries
// ------------------------------------------------------------------------------

std::optional<RegionBounds>
regionBounds(const camel::core::sem::IndexedInstance &instance, const sem::ResourceRegion &region) {
    if (region.kind != sem::ResourceRegion::Kind::Mapped) {
        return std::nullopt;
    }
    const std::vector<sem::ExprId> *coords = nullptr;
    switch (region.map) {
    case sem::ResourceRegion::Map::Write:
        if (instance.output.indirect) {
            return std::nullopt;
        }
        coords = &instance.output.coords;
        break;
    case sem::ResourceRegion::Map::Norm:
    case sem::ResourceRegion::Map::With: {
        const auto &accesses =
            region.map == sem::ResourceRegion::Map::Norm ? instance.norm : instance.with;
        if (region.operand >= accesses.size() ||
            accesses[region.operand].kind != sem::AccessKind::Affine) {
            return std::nullopt;
        }
        coords = &accesses[region.operand].coords;
        break;
    }
    }
    const auto extents = instance.extents();
    if (!extents) {
        return std::nullopt;
    }
    RegionBounds bounds;
    if (std::any_of(extents->begin(), extents->end(), [](int64_t e) { return e <= 0; })) {
        bounds.empty = true;
        return bounds;
    }
    const auto params = instance.paramValues();
    for (sem::ExprId coord : *coords) {
        const auto form = instance.exprs.affine(coord, extents->size(), params);
        if (!form) {
            return std::nullopt;
        }
        // An affine form over the box [0, extent): each term is smallest and largest at an end.
        int64_t lo = form->constant, hi = form->constant;
        for (size_t d = 0; d < form->coeffs.size(); ++d) {
            const int64_t last = form->coeffs[d] * ((*extents)[d] - 1);
            lo += std::min<int64_t>(0, last);
            hi += std::max<int64_t>(0, last);
        }
        bounds.axes.emplace_back(lo, hi);
    }
    return bounds;
}

bool regionsMayOverlap(const std::optional<RegionBounds> &a, const std::optional<RegionBounds> &b) {
    if ((a && a->empty) || (b && b->empty)) {
        return false;
    }
    if (!a || !b || a->axes.size() != b->axes.size()) {
        return true;
    }
    for (size_t i = 0; i < a->axes.size(); ++i) {
        if (a->axes[i].second < b->axes[i].first || b->axes[i].second < a->axes[i].first) {
            return false;
        }
    }
    return true;
}

NodeResources resourcesOf(const Program &program, const GCGraph &graph, gc_node_ref_t node) {
    // The graphs the node runs: each footprint a finished fixpoint (cached).
    Resolver resolver(program, [&program](const GCGraph *callee) {
        return footprintOf(program, callee);
    });
    return resolver.node(graph, node);
}

GraphFootprint footprintOf(const Program &program, const GCGraph *graph) {
    FootprintWalk walk(program, core::OperatorTable::instance().declarationEpoch());
    return walk.run(graph);
}

ValueOrigin originOf(const Program &program, const GCGraph &graph, gc_node_ref_t value) {
    Resolver resolver(program, [&program](const GCGraph *callee) {
        return footprintOf(program, callee);
    });
    return resolver.originOf(graph, value);
}

bool mayAlias(const Program &program, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    Resolver resolver(program, [&program](const GCGraph *callee) {
        return footprintOf(program, callee);
    });
    return resolver.mayAlias(graph, a, b);
}

std::string_view conflictKindName(ConflictKind kind) {
    switch (kind) {
    case ConflictKind::None:
        return "none";
    case ConflictKind::Resource:
        return "resource";
    }
    return "none";
}

namespace {

/// The conflict of two nodes, resolved with one resolver (the public query makes one). A conflict
/// is decided by resource: the pair is the accesses', and the two nodes must then run in the order
/// the program gives them. Two effects outside the program that name different resources are not a
/// pair at all (design principles 3.3, F9); an effect that names none is the global resource and is
/// an access of unknown class (Resolver::node), so it meets every effect through the same loop.
ResourceConflict conflictOf(
    const Resolver &resolver, const GCGraph &graph, gc_node_ref_t a, const NodeResources &ra,
    gc_node_ref_t b, const NodeResources &rb) {
    ResourceConflict conflict;
    if (a == b) {
        return conflict;
    }
    for (size_t i = 0; i < ra.accesses.size(); ++i) {
        for (size_t j = 0; j < rb.accesses.size(); ++j) {
            const NodeAccess &x = ra.accesses[i];
            const NodeAccess &y = rb.accesses[j];
            if (!sem::writes(x.mode) && !sem::writes(y.mode)) {
                continue;
            }
            if (!compatible(x.resource, y.resource)) {
                continue;
            }
            // An ambient resource is named by its name only, which compatible() compared; a
            // resource named by a value meets it only when one of the two is of unknown class.
            const bool ambientX = x.resource.cls == ResourceClass::Ambient;
            const bool ambientY = y.resource.cls == ResourceClass::Ambient;
            if (!ambientX && !ambientY && !x.anyValue && !y.anyValue &&
                !resolver.mayAlias(graph, x.value, y.value)) {
                continue;
            }
            if (!regionsOverlap(graph, a, x, b, y)) {
                continue;
            }
            conflict.kind    = ConflictKind::Resource;
            conflict.accessA = i;
            conflict.accessB = j;
            conflict.why     = accessText(x) + " / " + accessText(y);
            return conflict;
        }
    }
    return conflict;
}

/// conflictOf resolved for the two nodes: the reading most callers want.
ResourceConflict
conflictWith(const Resolver &resolver, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    return conflictOf(resolver, graph, a, resolver.node(graph, a), b, resolver.node(graph, b));
}

} // namespace

ResourceConflict
resourceConflict(const Program &program, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    Resolver resolver(program, [&program](const GCGraph *callee) {
        return footprintOf(program, callee);
    });
    return conflictWith(resolver, graph, a, b);
}

bool dependsOn(const GCGraph &graph, gc_node_ref_t later, gc_node_ref_t earlier) {
    return later != earlier && reaches(graph, later, earlier, false);
}

bool orderedBefore(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    return a != b && reaches(graph, b, a, true);
}

namespace {

/// Whether `a` and `b` lie in different arms of one branch: never both run. The arms a node is
/// in are the Arm regions around it.
bool exclusive(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    std::vector<std::pair<gc_node_ref_t, uint32_t>> armsOfA; // (BRCH, arm)
    for (uint32_t r = graph.nodeRegion(a); r != 0 && r < graph.regionCount();
         r          = graph.region(r)->parent) {
        const GCRegion &region = *graph.region(r);
        if (region.kind == GCRegionKind::Arm) {
            armsOfA.emplace_back(region.owner, region.index);
        }
    }
    for (uint32_t r = graph.nodeRegion(b); r != 0 && r < graph.regionCount();
         r          = graph.region(r)->parent) {
        const GCRegion &region = *graph.region(r);
        if (region.kind != GCRegionKind::Arm) {
            continue;
        }
        for (const auto &[brch, arm] : armsOfA) {
            if (brch == region.owner && arm != region.index) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::vector<RequiredOrdering> requiredOrderings(const Program &program, const GCGraph &graph) {
    // One walk for the whole query (each callee's footprint is a finished fixpoint).
    FootprintWalk walk(program, core::OperatorTable::instance().declarationEpoch());
    Resolver resolver(program, [&walk](const GCGraph *callee) { return walk.run(callee); });
    std::vector<gc_node_ref_t> touching;
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (!resolver.node(graph, ref).none()) {
            touching.push_back(ref);
        }
    }
    std::vector<RequiredOrdering> pairs;
    for (size_t i = 0; i < touching.size(); ++i) {
        for (size_t j = i + 1; j < touching.size(); ++j) {
            if (exclusive(graph, touching[i], touching[j])) {
                continue; // different arms of one branch: they never both run
            }
            const ResourceConflict c = conflictWith(resolver, graph, touching[i], touching[j]);
            if (!c) {
                continue;
            }
            RequiredOrdering o{.first = touching[i], .second = touching[j], .why = c.kind};
            if (orderedBefore(graph, touching[i], touching[j])) {
                o.ordered = true;
            } else if (orderedBefore(graph, touching[j], touching[i])) {
                o.first   = touching[j];
                o.second  = touching[i];
                o.ordered = true;
            }
            o.byData = o.ordered && dependsOn(graph, o.second, o.first);
            pairs.push_back(o);
        }
    }
    return pairs;
}

// ---- Text --------------------------------------------------------------------------------------

std::string toString(const NodeAccess &access) {
    std::string out = std::format(
        "{} {}",
        sem::accessModeName(access.mode),
        sem::resourceClassName(access.resource.cls));
    if (!access.resource.name.empty()) {
        out += " " + access.resource.name;
    }
    if (access.anyValue) {
        out += " (any)";
    } else if (access.value != kInvalidNodeRef) {
        out += std::format(" (node {})", access.value);
    }
    if (access.region.kind == sem::ResourceRegion::Kind::Mapped) {
        switch (access.region.map) {
        case sem::ResourceRegion::Map::Norm:
            out += std::format(" region of norm {}", access.region.operand);
            break;
        case sem::ResourceRegion::Map::With:
            out += std::format(" region of with {}", access.region.operand);
            break;
        case sem::ResourceRegion::Map::Write:
            out += " region of the write";
            break;
        }
    }
    return out;
}

std::string toString(const NodeResources &r) {
    if (r.none()) {
        return "none";
    }
    std::string out;
    const auto part = [&](const std::string &text) { out += (out.empty() ? "" : "; ") + text; };
    for (const NodeAccess &access : r.accesses) {
        part(toString(access));
    }
    if (r.result.kind != ResultAlias::Kind::Fresh) {
        std::string result = std::format("result {}", sem::resultAliasName(r.result.kind));
        if (r.result.operand != 0xFF) {
            result += std::format(" {}", r.result.operand);
        }
        part(result);
    }
    if (r.allocates) {
        std::string allocates =
            std::format("allocates {}", sem::resourceClassName(r.allocates->cls));
        if (!r.allocates->name.empty()) {
            allocates += " " + r.allocates->name;
        }
        part(allocates);
    }
    if (r.external) {
        part("external");
    }
    if (!r.declared) {
        part("conservative");
    }
    return out;
}

std::string toString(const GraphFootprint &fp) {
    std::string out;
    const auto part = [&](const std::string &text) { out += (out.empty() ? "" : "; ") + text; };
    for (const sem::ResourceAccess &access : fp.accesses) {
        part(sem::toString(access));
    }
    for (ResourceClass cls : fp.anyOf) {
        part(std::format("rmw {} (any)", sem::resourceClassName(cls)));
    }
    if (fp.result.kind != ResultAlias::Kind::Unknown) {
        std::string result = std::format("result {}", sem::resultAliasName(fp.result.kind));
        if (fp.result.operand != 0xFF) {
            result += std::format(" {}", fp.result.operand);
        }
        part(result);
    }
    if (fp.external) {
        part("external");
    }
    if (!fp.declared) {
        part("conservative");
    }
    return out.empty() ? "none" : out;
}

// ---- Effect containment (analysis 4.1, the second half of the query section above) --------------

namespace {

/// The footprint as a signature, so that `covers` can answer whether a declaration covers it: the
/// same mapping the resolver uses to read a declared clause back onto a call site's operands, run
/// the other way. `withCount` is the callee's, so that a With binding keeps its own numbering.
sem::ResourceSignature signatureOfFootprint(const GraphFootprint &footprint, size_t withCount) {
    sem::ResourceSignature signature;
    signature.mode     = sem::ResourceSignature::Mode::Concrete;
    signature.result   = footprint.result;
    signature.anyOf    = footprint.anyOf;
    signature.external = footprint.external;
    for (const sem::ResourceAccess &access : footprint.accesses) {
        if (access.resource.cls == ResourceClass::Ambient ||
            access.binding.kind == sem::ResourceBinding::Kind::Ambient) {
            const auto same = std::find_if(
                signature.ambient.begin(),
                signature.ambient.end(),
                [&](const sem::ResourceSignature::AmbientUse &other) {
                    return other.name == access.resource.name;
                });
            if (same == signature.ambient.end()) {
                sem::ResourceSignature::AmbientUse use{
                    .name = access.resource.name,
                    .mode = access.mode};
                use.unordered = access.unordered;
                signature.ambient.push_back(use);
            } else {
                same->mode      = sem::uniteAccess(same->mode, access.mode);
                same->unordered = same->unordered || access.unordered;
            }
            continue;
        }
        if (access.binding.kind == sem::ResourceBinding::Kind::Capture) {
            // A capture: the position in the closure is all a declaration can name.
            if (std::find(
                    signature.captures.begin(),
                    signature.captures.end(),
                    access.binding.index) == signature.captures.end()) {
                signature.captures.push_back(access.binding.index);
            }
            continue;
        }
        // A With parameter keeps its own numbering; an ordinary one follows the with parameters.
        const size_t index = access.binding.kind == sem::ResourceBinding::Kind::With
                                 ? size_t(access.binding.index)
                                 : withCount + access.binding.index;
        const sem::ResourceSignature::ParamUse use{
            .index = static_cast<uint8_t>(index),
            .cls   = access.resource.cls,
            .name  = access.resource.name,
            .mode  = access.mode,
        };
        const auto same = std::find_if(
            signature.params.begin(),
            signature.params.end(),
            [&](const sem::ResourceSignature::ParamUse &other) {
                return other.index == use.index;
            });
        if (same == signature.params.end()) {
            signature.params.push_back(use);
        } else {
            same->mode = sem::uniteAccess(same->mode, use.mode);
        }
    }
    std::sort(signature.params.begin(), signature.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    return signature;
}

std::string accessUseText(const sem::ResourceAccess &access) {
    const std::string name = !access.resource.name.empty()
                                 ? sem::resourceDisplayName(access.resource.name)
                                 : std::string(sem::resourceClassName(access.resource.cls));
    return std::format("{} {}", access.mode == sem::AccessMode::Read ? "read" : "write", name);
}

/// What a footprint does beyond a declaration, and whether any of it is a reading the analysis
/// cannot name: the accesses the declaration does not cover, one line each, and the one flag both
/// callers need to answer with a diagnostic that names a node (`unnamed`).
struct BeyondCoverage {
    std::vector<std::string> beyond;
    /// A reading of unknown class the declaration does not promise: refused rather than counted,
    /// and the one line of `beyond` a node has to be found for.
    bool unnamed = false;
};

/// The accesses of `footprint` that `declared` does not cover, each judged by `covers` alone: one
/// access is beyond the declaration when a source that is that access by itself is not covered.
///
/// A reading of unknown class is judged like any other and refused when the clause does not
/// promise it. It used to be counted and passed -- "a possibility is not an access" -- but a clause
/// that cannot name what the body reaches proves nothing about it, and a body that may reach past
/// its own promise is what the check is for. A declaration that does
/// promise it (`any unknown`, which a widened signature can carry) still covers it: the refusal is
/// the clause's answer applied to the reading, not an unconditional one.
///
/// Two readings stay counted and passed, for their own reasons: a cell of a variable (mutability
/// is declared where the variable is, design effects plan 5.1), and a class the footprint names
/// but a clause cannot spell (`any object`, `any handle`: the analysis knows the class, and design
/// principles 3.3 models "may reach any one of them" deliberately -- refusing those is an item of
/// its own).
BeyondCoverage beyondCoverage(
    const sem::ResourceSignature &declared, const GraphFootprint &footprint, size_t withCount,
    EffectContainmentReport *report) {
    BeyondCoverage answer;
    std::vector<std::string> &beyond = answer.beyond;
    // The classes the footprint cannot name, counted one by one: a count that grows is how a
    // checker that stops looking is made to say so, and the unknown one now also refuses.
    if (report != nullptr) {
        for (ResourceClass cls : footprint.anyOf) {
            switch (cls) {
            case ResourceClass::Unknown:
                ++report->anyUnknown;
                break;
            case ResourceClass::Object:
                ++report->anyObject;
                break;
            case ResourceClass::Handle:
                ++report->anyHandle;
                break;
            case ResourceClass::Variable:
                ++report->variableCell;
                break;
            default:
                ++report->other;
                break;
            }
        }
    }
    for (ResourceClass cls : footprint.anyOf) {
        if (cls != ResourceClass::Unknown) {
            continue; // a class the analysis can name is the reading design 3.3 keeps conservative
        }
        GraphFootprint single;
        single.anyOf.push_back(cls);
        if (!sem::covers(declared, signatureOfFootprint(single, withCount))) {
            answer.unnamed = true;
            beyond.push_back(std::format("any {}", sem::resourceClassName(cls)));
        }
    }
    for (const sem::ResourceAccess &access : footprint.accesses) {
        // A cell of a variable is not in a `uses` clause (design effects plan 5.1: mutability is
        // declared where the variable is), so it is counted and passed.
        if (access.resource.cls == ResourceClass::Variable) {
            if (report != nullptr) {
                ++report->variableCell;
            }
            continue;
        }
        const bool unnamed = access.resource.cls == ResourceClass::Unknown;
        if (unnamed && report != nullptr) {
            ++report->anyUnknown;
        }
        GraphFootprint single;
        single.accesses.push_back(access);
        if (!sem::covers(declared, signatureOfFootprint(single, withCount))) {
            answer.unnamed = answer.unnamed || unnamed;
            beyond.push_back(accessUseText(access));
        }
    }
    // An effect outside the program that names no resource: a concrete reading only, because an
    // undeclared operator's "may have an external effect" is a possibility, not a fact.
    if (footprint.declared && footprint.external && !declared.external) {
        beyond.push_back("an effect outside the program");
    }
    std::sort(beyond.begin(), beyond.end());
    beyond.erase(std::unique(beyond.begin(), beyond.end()), beyond.end());
    return answer;
}

/// Why the analysis cannot name what `ref` reaches, in one clause: a refusal names the node, and
/// this is the half that says why the resource is not one a `uses` clause can state.
std::string unnamedReasonOf(const Program &program, const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    if (node == nullptr) {
        return "the node cannot be read";
    }
    if (node->kind == GCNodeKind::Oper) {
        const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
        if (body == nullptr) {
            return "its operator declares nothing, so what it reaches is read conservatively";
        }
        if (core::OperatorTable::instance().semantics(body->oper) == nullptr) {
            return std::format(
                "'{}' declares no resources, so every resource its operands may name is read "
                "conservatively: the operator's declaration is where one would be named",
                body->uri());
        }
        return "its declaration names no resource the analysis can read";
    }
    if (node->kind == GCNodeKind::Func || node->kind == GCNodeKind::Call) {
        const auto with = graph.withInputsOf(ref);
        const core::type::FunctionType *type =
            with.empty() ? nullptr : functionTypeOfValue(graph, valueBehindGates(graph, with[0]));
        if (type == nullptr) {
            return "the function value it runs has no function type, so no clause says what it "
                   "reaches";
        }
        const sem::ResourceSignature &clause = type->resources();
        if (clause.mode == sem::ResourceSignature::Mode::Generic) {
            return "the function value it runs publishes no resource clause (its type promises "
                   "nothing), so nothing says what it reaches";
        }
        const GCGraph *callee = program.calleeOf(graph, ref);
        if (callee == nullptr) {
            return "the body behind the function value it runs is not one the graph names, so "
                   "nothing says what it reaches";
        }
        return std::format(
            "the body of '{}' it runs reaches a resource no clause there names, and this call "
            "carries that reading up with it",
            callee->name());
    }
    return "the analysis cannot name the resource it reaches";
}

/// Where the analysis first fails to name what a body reaches: the node, and why.
struct UnnamedReading {
    gc_node_ref_t node = kInvalidNodeRef;
    std::string why;
};

/// The first node of `graph` (by node order) whose own accesses reach a resource of unknown class,
/// with the reason -- the node a refusal names. Its own walk, because the two readers that ask
/// (the call site and the commit) hold their footprints differently; the fixpoint is cached, so a
/// second walk of a graph already measured costs one lookup.
std::optional<UnnamedReading> firstUnnamedReading(const Program &program, const GCGraph &graph) {
    FootprintWalk walk(program, core::OperatorTable::instance().declarationEpoch());
    Resolver resolver(program, [&walk](const GCGraph *callee) { return walk.run(callee); });
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (graph.node(ref) == nullptr) {
            continue;
        }
        const NodeResources resources = resolver.node(graph, ref);
        const bool reaches            = std::any_of(
            resources.accesses.begin(),
            resources.accesses.end(),
            [](const NodeAccess &access) { return access.resource.cls == ResourceClass::Unknown; });
        if (reaches) {
            return UnnamedReading{.node = ref, .why = unnamedReasonOf(program, graph, ref)};
        }
    }
    return std::nullopt;
}

/// The declared clause of a call site and the graph it runs, or nullopt when the site declares
/// nothing to compare (`nullptr` out-params) -- a call through a value the graph does not resolve.
struct CallSite {
    const sem::ResourceSignature *declared = nullptr;
    const GCGraph *callee                  = nullptr;
    size_t withCount                       = 0;
};

CallSite declaredClauseAt(const Program &program, const GCGraph &graph, gc_node_ref_t ref) {
    CallSite site;
    const GCNodeKind kind = graph.node(ref)->kind;
    if (kind == GCNodeKind::Func) {
        site.callee = program.calleeOf(graph, ref);
        if (site.callee == nullptr || site.callee->funcType() == nullptr) {
            return {};
        }
        site.declared  = &site.callee->funcType()->resources();
        site.withCount = site.callee->funcType()->withTypesCount();
        return site;
    }
    // A call: the type of the function value in `with` 0 is what the call site declares.
    const auto with = graph.withInputsOf(ref);
    if (with.empty()) {
        return {};
    }
    const gc_node_ref_t value = valueBehindGates(graph, with[0]);
    const auto *type          = functionTypeOfValue(graph, value);
    if (type == nullptr) {
        return {};
    }
    // A call site whose value type writes no clause is *judged*, not declined: "no promise, no
    // breach" is a different sentence from "a reading the checker could not name", and the two must
    // not collapse into one branch (`commit_checks.a_type_that_promises_nothing` pins the counts
    // apart). So this site hands the clause on as it is and `beyondCoverage` is what answers -- the
    // shared `functionValueClause` could only turn a `Generic` type into "declined" here, which is
    // the answer `Resolver::call` needs and this check must not give.
    site.declared  = &type->resources();
    site.withCount = type->withTypesCount();
    // The body behind it, when the value names a function statically: without it there is no
    // footprint to compare, and the type promises nothing it is known to break.
    if (const ::Function *fn = staticFunctionOf(graph, value)) {
        site.callee = fn->graph();
    }
    return site;
}

} // namespace

std::optional<EffectContainment> effectContainmentViolation(
    const Program &program, const GCGraph &graph, EffectContainmentReport *report) {
    for (gc_cnt_t i = 0; i < graph.nodeCount(); ++i) {
        const gc_node_ref_t ref = static_cast<gc_node_ref_t>(i);
        const GCNode *node      = graph.node(ref);
        if (node == nullptr) {
            continue;
        }
        if (node->kind != GCNodeKind::Func && node->kind != GCNodeKind::Call) {
            continue;
        }
        const CallSite site = declaredClauseAt(program, graph, ref);
        if (site.declared == nullptr || site.callee == nullptr) {
            if (report != nullptr) {
                ++report->unresolved;
            }
            continue;
        }
        if (report != nullptr) {
            ++report->checked;
        }
        if (site.declared->mode != sem::ResourceSignature::Mode::Concrete) {
            continue; // a type that promises nothing covers everything (sem::covers)
        }
        const GraphFootprint footprint = footprintOf(program, site.callee);
        const BeyondCoverage coverage =
            beyondCoverage(*site.declared, footprint, site.withCount, report);
        if (coverage.beyond.empty()) {
            continue;
        }
        EffectContainment violation{
            .call     = ref,
            .declared = sem::toString(*site.declared),
            .beyond   = coverage.beyond,
        };
        // A reading of unknown class is refused rather than counted, and the refusal names the
        // node of the body that makes it -- the call site above says which promise is broken, not
        // where. `firstUnnamedReading` answers `nullopt` only when the unknown came from nowhere
        // this body holds, which cannot happen (a callee's `anyOf` is carried on the node that
        // runs it); a refusal without the node would still be a refusal, so it is kept.
        if (coverage.unnamed) {
            if (const auto unnamed = firstUnnamedReading(program, *site.callee)) {
                violation.body       = site.callee;
                violation.unnamed    = unnamed->node;
                violation.unnamedWhy = unnamed->why;
            }
        }
        return violation;
    }
    return std::nullopt;
}

// ---- The declaration over a body a pass rewrote ------------------------------------------------

sem::ResourceSignature bodySignature(const Program &program, const GCGraph &graph) {
    // The graph's own numbering is its type's: a parameter use is the parameter, with the `with`
    // ones first. A graph without a type has none to number against, and a footprint measured that
    // way is a clause no caller could read; the caller compares it against something else anyway.
    const size_t withCount =
        graph.funcType() != nullptr ? graph.funcType()->withTypesCount() : size_t(0);
    return signatureOfFootprint(footprintOf(program, &graph), withCount);
}

sem::ResourceSignature
widened(const sem::ResourceSignature &declared, const sem::ResourceSignature &body) {
    if (declared.mode == sem::ResourceSignature::Mode::Generic) {
        // A type that promises nothing already covers everything: there is no clause to widen.
        return declared;
    }
    if (body.mode == sem::ResourceSignature::Mode::Generic) {
        return declared;
    }
    sem::ResourceSignature wider = declared;
    for (const auto &use : body.params) {
        const auto same = std::find_if(
            wider.params.begin(),
            wider.params.end(),
            [&](const sem::ResourceSignature::ParamUse &t) { return t.index == use.index; });
        if (same == wider.params.end()) {
            wider.params.push_back(use);
        } else {
            same->mode = sem::uniteAccess(same->mode, use.mode);
        }
    }
    std::sort(wider.params.begin(), wider.params.end(), [](const auto &a, const auto &b) {
        return a.index < b.index;
    });
    for (const auto &use : body.ambient) {
        const auto same = std::find_if(
            wider.ambient.begin(),
            wider.ambient.end(),
            [&](const sem::ResourceSignature::AmbientUse &t) { return t.name == use.name; });
        if (same == wider.ambient.end()) {
            wider.ambient.push_back(use);
        } else {
            same->mode      = sem::uniteAccess(same->mode, use.mode);
            same->unordered = same->unordered || use.unordered;
        }
    }
    std::sort(wider.ambient.begin(), wider.ambient.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    for (uint8_t capture : body.captures) {
        if (std::find(wider.captures.begin(), wider.captures.end(), capture) ==
            wider.captures.end()) {
            wider.captures.push_back(capture);
        }
    }
    std::sort(wider.captures.begin(), wider.captures.end());
    for (ResourceClass cls : body.anyOf) {
        if (std::find(wider.anyOf.begin(), wider.anyOf.end(), cls) == wider.anyOf.end()) {
            wider.anyOf.push_back(cls);
        }
    }
    if (!wider.allocates) {
        wider.allocates = body.allocates;
    }
    if (wider.result.kind == sem::ResultAlias::Kind::Unknown) {
        wider.result = body.result;
    }
    wider.external = wider.external || body.external;
    // A clause that names an ambient resource carries the effect that comes with it, the same way
    // a written one does (`ambientCarriesExternal`): the two agree, so a widened clause is not
    // weaker than the body it was built from.
    sem::ambientCarriesExternal(wider);
    return wider;
}

// ---- The commit checks (analysis 4.1), one question for every commit path
// ------------------------

std::string_view commitCheckName(CommitCheck check) {
    switch (check) {
    case CommitCheck::Effect:
        return "effect";
    case CommitCheck::Order:
        return "order";
    }
    return "effect";
}

namespace {

/// A node as a commit diagnostic names it: the graph, the node, and what the graph says it is. A
/// reader is looking at a program, so a bare number would not be enough (`describeNodeOf` in
/// execute/pass.cpp says the same thing for the pass-boundary diagnostics; the two texts are
/// about different questions and neither is a copy of the other's code).
std::string namedNode(const Program &program, const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    std::string what   = "a node";
    if (node != nullptr) {
        if (node->kind == GCNodeKind::Func || node->kind == GCNodeKind::Call) {
            const GCGraph *callee = program.calleeOf(graph, ref);
            what = callee == nullptr ? std::string("a call") : "a call of '" + callee->name() + "'";
        } else if (node->kind == GCNodeKind::Oper) {
            if (const auto *body = graph.nodeBodyAs<GCOperBody>(ref)) {
                what = std::string(body->uri());
            } else {
                what = "an operator";
            }
        }
    }
    return std::format("node {} of graph '{}' ({})", ref, graph.name(), what);
}

/// One access of one node in the spelling `beyondCoverage` answers in, so that an access the
/// declaration does not cover can be traced back to the node that makes it: a footprint is a sum
/// over the whole body and a diagnostic has to name a node.
std::string nodeAccessText(const NodeAccess &access) {
    const std::string name = !access.resource.name.empty()
                                 ? sem::resourceDisplayName(access.resource.name)
                                 : std::string(sem::resourceClassName(access.resource.cls));
    return std::format("{} {}", access.mode == sem::AccessMode::Read ? "read" : "write", name);
}

/// The accesses of every node of one graph, resolved at most once each. The commit checks ask for a
/// node's accesses once per pair it takes part in and once per access those contain; resolving them
/// again per question would put the resolver's work inside a loop over pairs, where nothing it
/// reads depends on the pair.
class NodeAccessTable {
  public:
    NodeAccessTable(const GCGraph &graph, const Resolver &resolver)
        : graph_(graph), resolver_(resolver), rows_(graph.nodeCount()) {}

    const NodeResources &of(gc_node_ref_t ref) const {
        static const NodeResources empty;
        if (ref >= rows_.size()) {
            return empty;
        }
        std::optional<NodeResources> &row = rows_[ref];
        if (!row.has_value()) {
            row = resolver_.node(graph_, ref);
        }
        return *row;
    }

    /// The conflict of two nodes, from the accesses already resolved.
    ResourceConflict conflict(gc_node_ref_t a, gc_node_ref_t b) const {
        return conflictOf(resolver_, graph_, a, of(a), b, of(b));
    }

  private:
    const GCGraph &graph_;
    const Resolver &resolver_;
    mutable std::vector<std::optional<NodeResources>> rows_;
};

/// Which reader asks the effect question, because the same fact is said for two of them: a commit
/// about to publish `graph`, and the graph that commit started from. The second is not this edit's
/// doing, and -- since that graph is the compiler's own product -- a reach past its type there is a
/// defect of the compiler, not a reason to refuse the program, so its sentence says that instead.
enum class EffectReader : uint8_t {
    Publishing,    // the body this commit would publish
    BeforeTheEdit, // the body the commit started from
};

/// Analysis 4.1, effect containment, read over the whole body of `graph`: what the nodes' operator
/// declarations make it reach (principle 3.3, resolved bottom-up) must stay inside the clause the
/// graph's own type declares. A reading the analysis cannot name (class Unknown) is refused like
/// any other access the clause does not cover, and the diagnostic names the node and the reason; a
/// cell of a variable is still passed over (its mutability is declared where the variable is),
/// which is the line the call-site check draws too.
///
/// `reader` changes only the closing sentence: who is being told, and so whether the reach is
/// something the commit is about to do or something the compiler already handed over.
std::optional<CommitViolation> effectBeyondDeclaration(
    const Program &program, const GCGraph &graph, FootprintWalk &walk, const NodeAccessTable &table,
    EffectReader reader) {
    const auto *type = graph.funcType();
    if (type == nullptr) {
        return std::nullopt; // nothing declares anything here
    }
    const sem::ResourceSignature &declared = type->resources();
    if (declared.mode != sem::ResourceSignature::Mode::Concrete) {
        return std::nullopt; // a type that promises nothing covers everything
    }
    const GraphFootprint body     = walk.run(&graph);
    const BeyondCoverage coverage = beyondCoverage(declared, body, type->withTypesCount(), nullptr);
    const std::vector<std::string> &beyond = coverage.beyond;
    if (beyond.empty()) {
        return std::nullopt;
    }
    // The one sentence that differs. The graph a commit starts from was built by the front end and
    // accepted as it is, so a body outside its own declaration was let through there: it is the
    // compiler's defect, and the reading says so rather than blaming the program.
    const std::string closing =
        reader == EffectReader::Publishing
            ? std::string(
                  "the commit would publish a body that reaches past its own declared "
                  "effect")
            : std::string(
                  "the front end accepted a graph that is beyond its own type, which is a "
                  "defect of the compiler and not of the program: no commit can judge an "
                  "edit against it");
    const std::string declaredText = declared.ambient.empty() && declared.params.empty() &&
                                             declared.captures.empty() && declared.anyOf.empty() &&
                                             !declared.external
                                         ? std::string("nothing")
                                         : sem::toString(declared);
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (graph.node(ref) == nullptr) {
            continue;
        }
        const NodeResources resources = table.of(ref);
        // A reading of unknown class first, at the node that makes it: no clause spells it, so it
        // has no access text for the loop below to match, and it is the one beyond the check
        // refuses for what the analysis cannot say rather than for what it said.
        const auto unknown = std::find_if(
            resources.accesses.begin(),
            resources.accesses.end(),
            [](const NodeAccess &access) { return access.resource.cls == ResourceClass::Unknown; });
        if (coverage.unnamed && unknown != resources.accesses.end()) {
            return CommitViolation{
                .check  = CommitCheck::Effect,
                .graph  = &graph,
                .first  = ref,
                .origin = graph.nodeOrigin(ref),
                .text   = std::format(
                    "{}, in a graph whose type declares {}, makes an access no clause can name "
                    "({}): {} -- the commit would publish a body that reaches past its own "
                    "declared effect",
                    namedNode(program, graph, ref),
                    declaredText,
                    nodeAccessText(*unknown),
                    unnamedReasonOf(program, graph, ref)),
            };
        }
        for (const NodeAccess &access : resources.accesses) {
            if (access.resource.cls == ResourceClass::Variable ||
                access.resource.cls == ResourceClass::Unknown) {
                continue; // a type cannot spell either, and each is answered above or passed over
            }
            if (std::find(beyond.begin(), beyond.end(), nodeAccessText(access)) == beyond.end()) {
                continue;
            }
            return CommitViolation{
                .check  = CommitCheck::Effect,
                .graph  = &graph,
                .first  = ref,
                .origin = graph.nodeOrigin(ref),
                .text   = std::format(
                    "{}, in a graph whose type declares {}, makes an access the type does not "
                    "cover ({}): {}",
                    namedNode(program, graph, ref),
                    declaredText,
                    nodeAccessText(access),
                    closing),
            };
        }
        if (resources.external &&
            std::find(beyond.begin(), beyond.end(), std::string("an effect outside the program")) !=
                beyond.end()) {
            return CommitViolation{
                .check  = CommitCheck::Effect,
                .graph  = &graph,
                .first  = ref,
                .origin = graph.nodeOrigin(ref),
                .text   = std::format(
                    "{}, in a graph whose type declares {}, has an effect outside the program: {}",
                    namedNode(program, graph, ref),
                    declaredText,
                    closing),
            };
        }
    }
    return std::nullopt;
}

/// The region a node's accesses are judged in: the innermost region that runs with its parent --
/// the body, an arm, a loop's condition, a loop's body. A `Block` or an `Inline` region always runs
/// when its parent does and a `Dead` one is a place a rewrite emptied, so neither is a region of
/// its own for this question: their nodes count in the control region around them.
uint32_t judgedRegionOf(const GCGraph &graph, uint32_t region) {
    uint32_t at = region;
    for (uint32_t steps = 0; steps <= graph.regionCount(); ++steps) {
        if (at == 0) {
            return 0;
        }
        const GCRegion *one = graph.region(at);
        if (one == nullptr) {
            return 0;
        }
        switch (one->kind) {
        case GCRegionKind::Arm:
        case GCRegionKind::LoopCond:
        case GCRegionKind::LoopBody:
            return at;
        default:
            break;
        }
        if (one->parent == at) {
            return 0;
        }
        at = one->parent;
    }
    return 0;
}

/// One half of the tier (docs/design/graph-editing/README.cn.md 8.1), read from the access table
/// the checks already build: did the draft add an access? A node the draft made that carries none
/// (a constant, a copy of a value) adds none. The other half is `GraphDraft::lowLevelEdits`, and
/// the table is why both are read: a commit is asked by which edit it used, and a node made with no
/// access and a node changed in place with no new access are the two ways a draft can leave the
/// graph's accesses and orderings alone.
bool addsAnAccess(const NodeAccessTable &table, std::span<const gc_node_ref_t> made) {
    for (gc_node_ref_t ref : made) {
        if (!table.of(ref).none()) {
            return true;
        }
    }
    return false;
}

/// The regions a commit answers for: the region each node it touched is in -- one it made, or one
/// it changed in place -- and every region around that one up to the body. A region is touched by
/// what was written in it and by what was written anywhere under it: an owner node stands in the
/// region it sits in for every region under it (the `RegionEntrant` below), so a write spliced into
/// an arm changes the footprint the branch node carries outside, and the pair out there can be new
/// while neither of its nodes is one this commit touched. The regions the commit did not touch are
/// left alone: the program's own accesses there are already ordered, and asking again would decide
/// a question whose answer cannot have changed.
std::vector<uint8_t> touchedRegions(const GCGraph &graph, std::span<const gc_node_ref_t> made) {
    std::vector<uint8_t> touched(static_cast<size_t>(graph.regionCount()), 0);
    if (touched.empty()) {
        return touched;
    }
    for (gc_node_ref_t ref : made) {
        if (!graph.containsNodeRef(ref)) {
            continue;
        }
        uint32_t region = judgedRegionOf(graph, graph.nodeRegion(ref));
        for (uint32_t steps = 0; region != 0 && steps <= graph.regionCount(); ++steps) {
            touched[region]     = 1;
            const GCRegion *one = graph.region(region);
            if (one == nullptr) {
                break;
            }
            const uint32_t parent = judgedRegionOf(graph, one->parent);
            if (parent == region) {
                break;
            }
            region = parent;
        }
        // The body is every region's ancestor, and the region a commit that made a node answers in
        // whatever else it touched.
        touched[0] = 1;
    }
    return touched;
}

/// One thing that touches a resource in one region: a node of that region, or -- when a node has
/// accesses in a region under this one -- the owner node that stands here for everything under it.
struct RegionEntrant {
    gc_node_ref_t stands = kInvalidNodeRef;
    /// The nodes whose accesses `stands` answers for here.
    std::vector<gc_node_ref_t> members;
};

/// A pair of accesses the order reading found with no path between them, named canonically (the
/// smaller node reference first) so that two readings of two versions of one graph can be compared:
/// the ids of the nodes two versions share are the same ids, which is what lets a commit ask
/// whether a pair is new.
using UnorderedPair = std::pair<gc_node_ref_t, gc_node_ref_t>;

UnorderedPair unorderedPairKey(gc_node_ref_t a, gc_node_ref_t b) {
    return a < b ? UnorderedPair{a, b} : UnorderedPair{b, a};
}

/// Analysis 4.1, order completeness, read region by region: any two accesses to one resource of
/// which at least one writes must have a path between them in the region they both stand in --
/// a data, sync or order edge, or a region's `GATE` input. A resource its declaration names
/// `unordered` carries no promise and is passed over (principle 3.3).
///
/// Which regions are read is `touchedRegions`' answer, and which pairs are: those of which one side
/// answers for a node in `scope` -- the nodes this commit made and the ones it changed in place --
/// at the depth such a node sits. An owner node answers for what is under it, and it is the owner
/// that stands in the region around it.
///
/// `visit` is called for each such pair with no path between its two accesses, as
/// `(the side that stands first, the side that stands second, the first access, the second access,
/// the conflict)`, and answers whether to keep reading. One walk serves both of the commit's two
/// readings of this question: which pair to refuse (the first the graph did not already have), and
/// which pairs the graph already had (a before/after reading of an edit made in place).
template <typename Visit>
void forEachUnorderedPair(
    const GCGraph &graph, const NodeAccessTable &table, std::span<const gc_node_ref_t> scope,
    Visit visit) {
    const std::vector<uint8_t> touched = touchedRegions(graph, scope);
    std::vector<uint8_t> scopedNode(static_cast<size_t>(graph.nodeCount()), 0);
    for (gc_node_ref_t ref : scope) {
        if (ref < graph.nodeCount()) {
            scopedNode[ref] = 1;
        }
    }
    const auto answersFor = [&scopedNode](const RegionEntrant &one) {
        for (gc_node_ref_t member : one.members) {
            if (member < scopedNode.size() && scopedNode[member] != 0) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::vector<RegionEntrant>> regions(
        static_cast<size_t>(graph.regionCount() > 0 ? graph.regionCount() : 1));
    const auto entrantAt = [&](uint32_t region, gc_node_ref_t stands) -> RegionEntrant & {
        std::vector<RegionEntrant> &here = regions[region < regions.size() ? region : 0];
        for (RegionEntrant &one : here) {
            if (one.stands == stands) {
                return one;
            }
        }
        here.push_back(RegionEntrant{.stands = stands});
        return here.back();
    };
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const GCNode *node = graph.node(ref);
        if (node == nullptr || table.of(ref).none()) {
            continue;
        }
        // The node itself, in the control region it sits in ...
        uint32_t region = judgedRegionOf(graph, graph.nodeRegion(ref));
        entrantAt(region, ref).members.push_back(ref);
        // ... and the owner of each control region around it, standing for it in the region that
        // region sits in (an arm, a loop's condition and a loop's body are what an owner has).
        for (uint32_t steps = 0; region != 0 && steps <= graph.regionCount(); ++steps) {
            const GCRegion *one = graph.region(region);
            if (one == nullptr) {
                break;
            }
            const uint32_t parent = judgedRegionOf(graph, one->parent);
            if (one->owner != kInvalidNodeRef) {
                entrantAt(parent, one->owner).members.push_back(ref);
            }
            if (parent == region) {
                break;
            }
            region = parent;
        }
    }
    for (size_t at = 0; at < regions.size(); ++at) {
        if (at >= touched.size() || !touched[at]) {
            continue; // a region this commit did not touch, where nothing can have changed
        }
        const std::vector<RegionEntrant> &here = regions[at];
        for (size_t i = 0; i < here.size(); ++i) {
            for (size_t j = i + 1; j < here.size(); ++j) {
                const RegionEntrant &x = here[i];
                const RegionEntrant &y = here[j];
                if (x.stands == y.stands || x.stands == kInvalidNodeRef ||
                    y.stands == kInvalidNodeRef) {
                    continue;
                }
                // A pair is judged when either side answers for a node this commit touched -- and
                // the side that answers for it need not be the node itself: an owner node stands in
                // its region for everything under it, so a write spliced into an arm turns the
                // branch node into a writer the body did not have while the branch node is not a
                // node this commit made. Two nodes the commit neither made nor now carries are two
                // the program already had in the relation it already had them in.
                if (!answersFor(x) && !answersFor(y)) {
                    continue;
                }
                // An owner node and a node it stands for here are one effect seen at two depths,
                // not two effects: the owner's access in this region *is* the access of what is
                // under it. A pair of them would ask whether an effect is ordered against itself,
                // which is not a question -- and the two sides are then the same access, or one
                // side's own node is a member of the other.
                const auto covers = [](const RegionEntrant &side, gc_node_ref_t node) {
                    return std::find(side.members.begin(), side.members.end(), node) !=
                           side.members.end();
                };
                if (covers(x, y.stands) || covers(y, x.stands) || x.members == y.members) {
                    continue;
                }
                for (gc_node_ref_t first : x.members) {
                    for (gc_node_ref_t second : y.members) {
                        if (first == second) {
                            continue;
                        }
                        const ResourceConflict conflict = table.conflict(first, second);
                        if (!conflict) {
                            continue;
                        }
                        if (table.of(first).accesses[conflict.accessA].unordered ||
                            table.of(second).accesses[conflict.accessB].unordered) {
                            continue; // the declaration promises the order does not matter
                        }
                        if (orderedBefore(graph, x.stands, y.stands) ||
                            orderedBefore(graph, y.stands, x.stands)) {
                            continue;
                        }
                        if (!visit(x.stands, y.stands, first, second, conflict)) {
                            return;
                        }
                    }
                }
            }
        }
    }
}

/// The order question as the commit asks it: the first pair in the regions the commit touched that
/// no path orders, and that the graph did not already leave unordered (`already`).
std::optional<CommitViolation> orderIncomplete(
    const Program &program, const GCGraph &graph, const NodeAccessTable &table,
    std::span<const gc_node_ref_t> scope, const std::vector<UnorderedPair> &already) {
    std::optional<CommitViolation> found;
    forEachUnorderedPair(
        graph,
        table,
        scope,
        [&](gc_node_ref_t x,
            gc_node_ref_t y,
            gc_node_ref_t first,
            gc_node_ref_t second,
            const ResourceConflict &conflict) {
            if (std::find(already.begin(), already.end(), unorderedPairKey(first, second)) !=
                already.end()) {
                // The graph left these two unordered before this commit touched either of them, so
                // this commit did not break it: refusing here would refuse the program for what it
                // already was, which is a question for the pass boundary and not for an edit.
                return true;
            }
            const uint64_t origin =
                graph.nodeOrigin(y) != 0 ? graph.nodeOrigin(y) : graph.nodeOrigin(x);
            found = CommitViolation{
                .check  = CommitCheck::Order,
                .graph  = &graph,
                .first  = x,
                .second = y,
                .origin = origin,
                .text   = std::format(
                    "{}, in one region of graph '{}', has no order against {}, and at "
                    "least one of the two writes ({}): no data edge, no sync edge, no "
                    "order edge and no region's GATE input makes one wait for the "
                    "other, so the commit would publish two effects on one resource "
                    "that the program leaves free to happen in either order",
                    namedNode(program, graph, x),
                    graph.name(),
                    namedNode(program, graph, y),
                    nodeAccessText(table.of(first).accesses[conflict.accessA]) + " / " +
                        nodeAccessText(table.of(second).accesses[conflict.accessB])),
            };
            return false;
        });
    return found;
}

} // namespace

std::optional<CommitViolation> commitViolation(
    const Program &program, const GCGraph &graph, std::span<const gc_node_ref_t> made,
    std::span<const gc_node_ref_t> changed, const GCGraph *before, const Program *beforeProgram,
    bool asksEffects, bool asksOrder) {
    // The tier first, and before any resolver runs: docs/design/graph-editing/README.cn.md 8.1's
    // table decides which of the six checks a commit is asked, and what decides that is which edit
    // the draft used -- read off the draft's edit record (`GraphDraft::editTier`) and not derived
    // from "did it make a node". A record that names only the edits which keep both promises --
    // `replace`, `erase`, `eval`, `order` and the two signature edits -- owes no answer about
    // either, and a rewrite whose only interface is the graph module pays one size check per graph
    // and nothing else. A record that names `inline` or a kernel primitive is asked both, because
    // those can have changed them.
    if (!asksEffects && !asksOrder) {
        return std::nullopt;
    }
    // One walk for the whole question: each callee's footprint is a finished fixpoint.
    FootprintWalk walk(program, core::OperatorTable::instance().declarationEpoch());
    Resolver resolver(program, [&walk](const GCGraph *callee) { return walk.run(callee); });
    const NodeAccessTable table(graph, resolver);
    if (changed.empty() && !addsAnAccess(table, made)) {
        return std::nullopt;
    }
    // The two readings the in-place half owes are asked of what the edit *did*, and the graph it
    // started from is what says that: `before` is the graph as it was. An edit of existing nodes in
    // place cannot leave a pair of accesses unordered where the program already left them so -- and
    // refusing that would refuse the program for what it already was, which the pass boundary asks
    // and a commit does not. A splice answers by construction (the node it made was in no pair
    // before), so that half keeps the whole-body reading it always had.
    //
    // The effect half is not the same shape, and it is not answered that way.
    // The graph a commit starts from is the compiler's own product -- the front end
    // built it, or a commit published it after answering for it -- so one whose body already
    // reaches past its own signature is a graph the front end accepted beyond its type. No program
    // writes that: it is a defect of the compiler, and it is reported as an internal error instead
    // of being stepped over. Stepping over it was worse than it sounds: the effect question about
    // the edited graph was then not asked at all, so an edit that added reach on a graph nobody
    // vouches for went unreported, and the skip looked like an answer.
    std::vector<UnorderedPair> alreadyUnordered;
    if (!changed.empty() && before != nullptr) {
        // The graph it started from, read in the program that published it: a call's footprint is
        // the callee's declared clause, and this commit is publishing new declarations for the
        // callees it rewrote. Reading `before` against them would measure the old body through a
        // promise it was never published under, and report the graph as beyond a type it satisfied
        // (the measurement constraint: `tiles` widens a callee and its caller in one commit).
        const Program &beforeIn = beforeProgram != nullptr ? *beforeProgram : program;
        FootprintWalk beforeWalk(beforeIn, core::OperatorTable::instance().declarationEpoch());
        Resolver beforeResolver(beforeIn, [&beforeWalk](const GCGraph *callee) {
            return beforeWalk.run(callee);
        });
        const NodeAccessTable beforeTable(*before, beforeResolver);
        // The effect half: the two readings of it (what the graph it started from declared, and
        // what the graph it publishes declares) are one question, so the record answers for them
        // together. A record that does not owe it asks neither -- today no edit but `inline` and a
        // primitive can change what the body reaches, and a named edit's nodes are not in `changed`
        // at all, so the skip is the one the table has always made.
        if (asksEffects) {
            if (const auto beforeBeyond = effectBeyondDeclaration(
                    beforeIn,
                    *before,
                    beforeWalk,
                    beforeTable,
                    EffectReader::BeforeTheEdit)) {
                throw GraphBeforeEditBeyondType(
                    std::format(
                        "the graph '{}' a commit was asked to edit already reached past the type "
                        "its "
                        "own "
                        "signature declares, before the edit: {}",
                        before->name(),
                        beforeBeyond->text));
            }
            if (auto effect =
                    effectBeyondDeclaration(program, graph, walk, table, EffectReader::Publishing)) {
                return effect;
            }
        }
        if (asksOrder) {
            forEachUnorderedPair(
                *before,
                beforeTable,
                changed,
                [&alreadyUnordered](
                    gc_node_ref_t /*x*/,
                    gc_node_ref_t /*y*/,
                    gc_node_ref_t first,
                    gc_node_ref_t second,
                    const ResourceConflict &) {
                    alreadyUnordered.push_back(unorderedPairKey(first, second));
                    return true;
                });
        }
    } else if (asksEffects) {
        if (auto effect =
                effectBeyondDeclaration(program, graph, walk, table, EffectReader::Publishing)) {
            return effect;
        }
    }
    if (!asksOrder) {
        return std::nullopt;
    }
    // The order reading answers for the regions the commit touched, and judges a pair when one side
    // stands for a node it touched: the nodes it made and the nodes it changed in place are the
    // same question there, and an in-place edit answers for its node's region like a splice does.
    std::vector<gc_node_ref_t> scope(made.begin(), made.end());
    scope.insert(scope.end(), changed.begin(), changed.end());
    return orderIncomplete(program, graph, table, scope, alreadyUnordered);
}

bool commitAddsAccess(
    const Program &program, const GCGraph &graph, std::span<const gc_node_ref_t> made) {
    if (made.empty()) {
        return false;
    }
    FootprintWalk walk(program, core::OperatorTable::instance().declarationEpoch());
    Resolver resolver(program, [&walk](const GCGraph *callee) { return walk.run(callee); });
    const NodeAccessTable table(graph, resolver);
    return addsAnAccess(table, made);
}

/// Records the refusal as the diagnostic both commit paths carry. It is here, next to the reading,
/// so that a pass and the graph module's `commit` cannot each answer "which diagnostic is a refused
/// commit" differently. The caller still stops: a recorded diagnostic is a report, not a control
/// flow.
void reportCommitViolation(
    const std::shared_ptr<camel::core::context::Context> &ctx, const std::string &site,
    const CommitViolation &violation) {
    if (ctx == nullptr) {
        return;
    }
    const core::error::RuntimeDiag which = [&] {
        switch (violation.check) {
        case CommitCheck::Effect:
            return core::error::RuntimeDiag::PassEffectBeyondType;
        case CommitCheck::Order:
            return core::error::RuntimeDiag::PassEffectOrder;
        }
        return core::error::RuntimeDiag::PassEffectBeyondType;
    }();
    auto diag = ctx->rtmDiags()->of(which);
    if (violation.origin != 0) {
        diag.atOrigin(violation.origin);
    }
    diag.commit(site, violation.text);
}

namespace {

/// The one cell the pass boundary reads (`commitChecksEnabled` below). It is not in the header: the
/// question is a build's answer, and a header that held it would offer a second copy of it to every
/// translation unit, which is exactly what the two paths must not have. The commit checks do not
/// read it: their tier is what the draft added (`commitAddsAccess`), which is a fact about the
/// draft, and a build that decided it would be a second answer to that fact.
std::atomic<bool> commitChecksCell =
#ifndef NDEBUG
    true;
#else
    false;
#endif

} // namespace

bool commitChecksEnabled() { return commitChecksCell.load(std::memory_order_relaxed); }

void setCommitChecksEnabled(bool enabled) {
    commitChecksCell.store(enabled, std::memory_order_relaxed);
}

// ---- The clause of a function value at an operand ----------------------------------------------
//
// One reading for the two ways a node's accesses come from a function value: a higher-order
// operator's function operand (the declaration names where it is), and the value a call node calls
// (its first `with` input). Both find the value behind the gates the graph put in front of it, and
// both ask `core::functionValueClauseAt` for the clause, the base its `with` parameters land at and
// how many there are, so the runtime footprint and the effect summary cannot disagree.

core::FunctionValueClause
clauseOfOperand(const GCGraph &graph, gc_node_ref_t node, const core::sem::ResourceBinding &where) {
    if (where.kind != core::sem::ResourceBinding::Kind::Norm &&
        where.kind != core::sem::ResourceBinding::Kind::With) {
        return core::FunctionValueClause{};
    }
    const auto operands = where.kind == core::sem::ResourceBinding::Kind::Norm
                              ? graph.normInputsOf(node)
                              : graph.withInputsOf(node);
    if (size_t(where.index) >= operands.size()) {
        return core::FunctionValueClause{};
    }
    const gc_node_ref_t value = valueBehindGates(graph, operands[where.index]);
    return core::functionValueClauseAt(
        functionTypeOfValue(graph, value),
        where.kind,
        size_t(where.index));
}

core::FunctionValueClause clauseOfCall(const GCGraph &graph, gc_node_ref_t node) {
    const auto with = graph.withInputsOf(node);
    if (with.empty()) {
        return core::FunctionValueClause{};
    }
    const gc_node_ref_t value = valueBehindGates(graph, with[0]);
    return core::functionValueClauseAt(
        functionTypeOfValue(graph, value),
        core::sem::ResourceBinding::Kind::With,
        0);
}

} // namespace camel::runtime
