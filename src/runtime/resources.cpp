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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The resources of the nodes of a frozen graph (see resources.h).
 *
 * Footprints. The footprint of a graph is collected from its nodes, and a FUNC, LOOP or CALL
 * node's resources come from the footprints of the graphs it runs, so footprints of mutually
 * recursive graphs depend on each other. They are computed as a least fixpoint: a graph met
 * again while its own footprint is being collected contributes what is known of it so far,
 * and the walk is repeated until no footprint grows (each one is a set over a finite domain,
 * so it ends). A footprint that came out of a finished walk is cached by the closure revision of the graph
 * (closureRevision: a new one whenever the graph or a graph it refers to changes) and the
 * operator declaration epoch, which together fix everything it was computed from.
 */

#include "camel/runtime/resources.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/program.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
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

namespace {

// ---- Small helpers ----------------------------------------------------------------------------

/// The value a GATE forwards, followed to the first node that is not a GATE.
gc_node_ref_t throughGates(const GCGraph &graph, gc_node_ref_t ref) {
    for (gc_cnt_t hops = 0; hops <= graph.nodeCount() && graph.containsNodeRef(ref); ++hops) {
        if (graph.node(ref)->kind != GCNodeKind::Gate || graph.normInputsOf(ref).empty()) {
            break;
        }
        ref = graph.normInputsOf(ref).back();
    }
    return ref;
}

bool contains(std::span<const gc_node_ref_t> refs, gc_node_ref_t ref) {
    return std::find(refs.begin(), refs.end(), ref) != refs.end();
}

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
    ref                = throughGates(graph, ref);
    const GCNode *node = graph.node(ref);
    if (node->kind != GCNodeKind::Data || node->dataIndex >= 0 || graph.staticArea() == nullptr ||
        node->dataType() == nullptr ||
        node->dataType()->code() != core::type::TypeCode::Function) {
        return nullptr;
    }
    return core::rtdata::fromSlot<::Function *>(
        graph.staticArea()->get<slot_t>(static_cast<size_t>(-node->dataIndex)));
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
            grew_ = false;
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

    NodeResources node(const GCGraph &graph, gc_node_ref_t ref);

  private:
    NodeResources oper(const GCGraph &graph, gc_node_ref_t ref);
    NodeResources call(const GCGraph &graph, gc_node_ref_t ref);
    NodeResources loop(const GCGraph &graph, gc_node_ref_t ref);

    /// The footprint `fp` of a graph run by node `ref`, with each binding resolved to an input
    /// of the node by `bind` (kInvalidNodeRef: the resource cannot be told).
    template <typename Bind>
    NodeResources fromFootprint(const GraphFootprint &fp, Bind &&bind) const;

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
            const ValueOrigin origin = originOf(*graph, access.value);
            if (origin.kind == ValueOrigin::Kind::Fresh) {
                continue; // made by this activation: invisible to its caller
            }
            if (origin.kind == ValueOrigin::Kind::Unknown) {
                addAny(fp, access.resource.cls);
                continue;
            }
            // A port: which one.
            const auto indexIn = [&](std::span<const gc_node_ref_t> ports) {
                return static_cast<uint8_t>(std::find(ports.begin(), ports.end(), origin.node) - ports.begin());
            };
            sem::ResourceBinding binding;
            if (contains(graph->normPorts(), origin.node)) {
                binding = {sem::ResourceBinding::Kind::Norm, indexIn(graph->normPorts())};
            } else if (contains(graph->withPorts(), origin.node)) {
                binding = {sem::ResourceBinding::Kind::With, indexIn(graph->withPorts())};
            } else {
                binding = {sem::ResourceBinding::Kind::Capture, indexIn(graph->closureNodes())};
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
    active_.erase(graph);
    grew_ |= merge(memo_[graph], fp);
    done_.insert(graph);
    return memo_[graph];
}

template <typename Bind>
NodeResources Resolver::fromFootprint(const GraphFootprint &fp, Bind &&bind) const {
    NodeResources r;
    r.external = fp.external;
    r.declared = fp.declared;
    // What a graph returns may be anything it received: not known here.
    r.result = ResultAlias{.kind = ResultAlias::Kind::Unknown};
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

NodeResources Resolver::node(const GCGraph &graph, gc_node_ref_t ref) {
    switch (graph.node(ref)->kind) {
    case GCNodeKind::Oper:
        return oper(graph, ref);
    case GCNodeKind::Func: {
        const auto norm = graph.normInputsOf(ref);
        const auto with = graph.withInputsOf(ref);
        return fromFootprint(
            footprint_(program_.calleeOf(graph, ref)),
            [&](const sem::ResourceBinding &b) -> gc_node_ref_t {
                if (b.kind == sem::ResourceBinding::Kind::Norm && b.index < norm.size()) {
                    return norm[b.index];
                }
                if (b.kind == sem::ResourceBinding::Kind::With && b.index < with.size()) {
                    return with[b.index];
                }
                return kInvalidNodeRef; // a capture of a directly called graph: not an input
            });
    }
    case GCNodeKind::Call:
        return call(graph, ref);
    case GCNodeKind::Loop:
        return loop(graph, ref);
    case GCNodeKind::Copy:
        // A copy is a new object nobody else refers to.
        return NodeResources{};
    default:
        return NodeResources{};
    }
}

NodeResources Resolver::oper(const GCGraph &graph, gc_node_ref_t ref) {
    const auto &table           = core::OperatorTable::instance();
    const core::OperId id       = graph.nodeBodyAs<GCOperBody>(ref)->oper;
    const auto traits           = table.find(id);
    const sem::OperatorSemantics *s = table.semantics(id);
    const auto norm             = graph.normInputsOf(ref);
    const auto with             = graph.withInputsOf(ref);

    NodeResources r;
    sem::ResourceSpec spec;
    if (s != nullptr && s->resources) {
        spec = *s->resources;
    } else {
        spec       = sem::conservativeResources(
            norm.size(), with.size(), traits && traits->pure, table.hasExternalEffect(id));
        r.declared = traits && traits->pure;
    }
    r.result    = spec.result;
    r.allocates = spec.allocates;
    r.external  = spec.externalEffect;
    for (const sem::ResourceAccess &access : spec.accesses) {
        NodeAccess a{.resource = access.resource, .mode = access.mode, .region = access.region};
        switch (access.binding.kind) {
        case sem::ResourceBinding::Kind::Norm:
            a.value = access.binding.index < norm.size() ? norm[access.binding.index] : kInvalidNodeRef;
            break;
        case sem::ResourceBinding::Kind::With:
            a.value = access.binding.index < with.size() ? with[access.binding.index] : kInvalidNodeRef;
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

NodeResources Resolver::call(const GCGraph &graph, gc_node_ref_t ref) {
    const auto norm = graph.normInputsOf(ref);
    const auto with = graph.withInputsOf(ref);
    // The callee is With input 0: a known function when it is a constant, or a closure this
    // graph builds from one (a FILL of a constant function and the captured values).
    const GCGraph *callee = nullptr;
    std::vector<gc_node_ref_t> captures; // by capture position, kInvalidNodeRef when unknown
    if (!with.empty()) {
        const gc_node_ref_t value = throughGates(graph, with[0]);
        if (const ::Function *fn = staticFunctionOf(graph, value)) {
            callee = fn->graph();
        } else if (graph.node(value)->kind == GCNodeKind::Fill && !graph.normInputsOf(value).empty()) {
            if (const ::Function *fn = staticFunctionOf(graph, graph.normInputsOf(value)[0])) {
                callee          = fn->graph();
                const auto *body = graph.nodeBodyAs<GCFillBody>(value);
                const auto slots = body->slots();
                const auto inputs = graph.withInputsOf(value);
                for (size_t i = 0; i < slots.size() && i < inputs.size(); ++i) {
                    const size_t position = static_cast<size_t>(slots[i]);
                    if (captures.size() <= position) {
                        captures.resize(position + 1, kInvalidNodeRef);
                    }
                    captures[position] = inputs[i];
                }
            }
        }
    }
    if (callee == nullptr) {
        // A call through a value: anything may happen.
        NodeResources r;
        r.accesses.push_back(NodeAccess{.resource = {}, .anyValue = true});
        r.result   = ResultAlias{.kind = ResultAlias::Kind::Unknown};
        r.external = true;
        r.declared = false;
        return r;
    }
    return fromFootprint(footprint_(callee), [&](const sem::ResourceBinding &b) -> gc_node_ref_t {
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

NodeResources Resolver::loop(const GCGraph &graph, gc_node_ref_t ref) {
    const LoopView view(program_, graph, ref);
    const auto captures = view.captures();
    NodeResources r;
    r.result = ResultAlias{.kind = ResultAlias::Kind::Unknown};
    for (const GCGraph *part : {static_cast<const GCGraph *>(view.condGraph()),
                                static_cast<const GCGraph *>(view.bodyGraph())}) {
        if (part == nullptr) {
            continue;
        }
        // The capture ports of a loop graph are its with ports once it is called, its closure
        // ports otherwise (as LoopView reads them); its norm ports are the element and the state,
        // which change from round to round: not an input of the node.
        const bool capturesAreWith = !part->withPorts().empty();
        NodeResources partR = fromFootprint(footprint_(part), [&](const sem::ResourceBinding &b) {
            const bool capture = capturesAreWith ? b.kind == sem::ResourceBinding::Kind::With
                                                 : b.kind == sem::ResourceBinding::Kind::Capture;
            return capture && b.index < captures.size() ? captures[b.index] : kInvalidNodeRef;
        });
        r.accesses.insert(r.accesses.end(), partR.accesses.begin(), partR.accesses.end());
        r.external |= partR.external;
        r.declared &= partR.declared;
    }
    return r;
}

// ---- Regions -----------------------------------------------------------------------------------

/// Whether two accesses' regions may overlap (only meaningful for one resource).
bool regionsOverlap(
    const GCGraph &graph, gc_node_ref_t a, const NodeAccess &x, gc_node_ref_t b, const NodeAccess &y) {
    const auto boundsAt = [&](gc_node_ref_t node, const NodeAccess &access) -> std::optional<RegionBounds> {
        if (access.region.kind != sem::ResourceRegion::Kind::Mapped) {
            return std::nullopt;
        }
        const auto instance = instanceOf(graph, node);
        return instance ? regionBounds(*instance, access.region) : std::nullopt;
    };
    return regionsMayOverlap(boundsAt(a, x), boundsAt(b, y));
}

// ---- Paths -------------------------------------------------------------------------------------

/// Whether `to` reaches `from` backwards through inputs (Norm and With; Ctrl too when asked).
bool reaches(const GCGraph &graph, gc_node_ref_t from, gc_node_ref_t to, bool ctrl) {
    std::vector<bool> seen(graph.nodeCount(), false);
    std::vector<gc_node_ref_t> work{from};
    while (!work.empty()) {
        const gc_node_ref_t ref = work.back();
        work.pop_back();
        const auto visit = [&](std::span<const gc_node_ref_t> inputs) {
            for (gc_node_ref_t input : inputs) {
                if (graph.containsNodeRef(input) && !seen[input]) {
                    seen[input] = true;
                    work.push_back(input);
                }
            }
        };
        visit(graph.normInputsOf(ref));
        visit(graph.withInputsOf(ref));
        if (ctrl) {
            visit(graph.ctrlInputsOf(ref));
        }
        if (graph.containsNodeRef(to) && seen[to]) {
            return true;
        }
    }
    return false;
}

std::string accessText(const NodeAccess &access) { return toString(access); }

} // namespace

// ---- Public queries ------------------------------------------------------------------------------

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

ValueOrigin originOf(const GCGraph &graph, gc_node_ref_t value) {
    const auto &table = core::OperatorTable::instance();
    for (gc_cnt_t hops = 0; hops <= graph.nodeCount() && graph.containsNodeRef(value); ++hops) {
        value              = throughGates(graph, value);
        const GCNode *node = graph.node(value);
        switch (node->kind) {
        case GCNodeKind::Port:
            return ValueOrigin{ValueOrigin::Kind::Port, value};
        case GCNodeKind::Copy:
            return ValueOrigin{ValueOrigin::Kind::Fresh, value};
        case GCNodeKind::Oper: {
            const sem::OperatorSemantics *s =
                table.semantics(graph.nodeBodyAs<GCOperBody>(value)->oper);
            if (s == nullptr || !s->resources) {
                return ValueOrigin{ValueOrigin::Kind::Unknown, value};
            }
            const ResultAlias &result = s->resources->result;
            const auto norm           = graph.normInputsOf(value);
            if (result.kind == ResultAlias::Kind::Fresh) {
                return ValueOrigin{ValueOrigin::Kind::Fresh, value};
            }
            if ((result.kind == ResultAlias::Kind::Same || result.kind == ResultAlias::Kind::View) &&
                result.operand < norm.size()) {
                value = norm[result.operand];
                continue; // the operand's origin
            }
            return ValueOrigin{ValueOrigin::Kind::Unknown, value};
        }
        default:
            return ValueOrigin{ValueOrigin::Kind::Unknown, value};
        }
    }
    return ValueOrigin{ValueOrigin::Kind::Unknown, value};
}

bool mayAlias(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    const ValueOrigin x = originOf(graph, a);
    const ValueOrigin y = originOf(graph, b);
    if (x.kind == ValueOrigin::Kind::Unknown || y.kind == ValueOrigin::Kind::Unknown || x == y) {
        return true;
    }
    // Two different fresh allocations, or a fresh one and a received value: different objects.
    // Two received values may be one (a caller passes one cell twice).
    return x.kind == ValueOrigin::Kind::Port && y.kind == ValueOrigin::Kind::Port;
}

std::string_view conflictKindName(ConflictKind kind) {
    switch (kind) {
    case ConflictKind::None:
        return "none";
    case ConflictKind::Resource:
        return "resource";
    case ConflictKind::External:
        return "external";
    }
    return "none";
}

ResourceConflict
resourceConflict(const Program &program, const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    ResourceConflict conflict;
    if (a == b) {
        return conflict;
    }
    const NodeResources ra = resourcesOf(program, graph, a);
    const NodeResources rb = resourcesOf(program, graph, b);
    if (ra.external && rb.external) {
        conflict.kind = ConflictKind::External;
        conflict.why  = "both have effects outside the program";
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
                !mayAlias(graph, x.value, y.value)) {
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

bool dependsOn(const GCGraph &graph, gc_node_ref_t later, gc_node_ref_t earlier) {
    return later != earlier && reaches(graph, later, earlier, false);
}

bool orderedBefore(const GCGraph &graph, gc_node_ref_t a, gc_node_ref_t b) {
    return a != b && reaches(graph, b, a, true);
}

namespace {

/// The nodes each arm of each branch of `graph` reaches: per BRCH, per arm, the nodes its head
/// reaches forwards (itself included). A node reached by exactly one arm of a branch runs only
/// when that arm is taken.
std::vector<std::vector<std::vector<bool>>> armReach(const GCGraph &graph) {
    std::vector<std::vector<std::vector<bool>>> branches;
    for (gc_node_ref_t brch = 0; brch < graph.nodeCount(); ++brch) {
        if (graph.node(brch)->kind != GCNodeKind::Brch) {
            continue;
        }
        std::vector<std::vector<bool>> arms;
        for (const GCBranchArm &arm : graph.branchArmsOf(brch)) {
            std::vector<bool> seen(graph.nodeCount(), false);
            std::vector<gc_node_ref_t> work;
            if (graph.containsNodeRef(arm.head)) {
                seen[arm.head] = true;
                work.push_back(arm.head);
            }
            while (!work.empty()) {
                const gc_node_ref_t ref = work.back();
                work.pop_back();
                for (const auto outputs :
                     {graph.normOutputsOf(ref), graph.withOutputsOf(ref), graph.ctrlOutputsOf(ref)}) {
                    for (gc_node_ref_t next : outputs) {
                        if (graph.containsNodeRef(next) && !seen[next]) {
                            seen[next] = true;
                            work.push_back(next);
                        }
                    }
                }
            }
            arms.push_back(std::move(seen));
        }
        branches.push_back(std::move(arms));
    }
    return branches;
}

/// Whether `a` and `b` lie in different arms of one branch: never both run.
bool exclusive(
    const std::vector<std::vector<std::vector<bool>>> &branches, gc_node_ref_t a, gc_node_ref_t b) {
    for (const auto &arms : branches) {
        const auto onlyArm = [&](gc_node_ref_t node) -> std::optional<size_t> {
            std::optional<size_t> found;
            for (size_t i = 0; i < arms.size(); ++i) {
                if (arms[i][node]) {
                    if (found) {
                        return std::nullopt; // reached by two arms: after the join
                    }
                    found = i;
                }
            }
            return found;
        };
        const auto armA = onlyArm(a), armB = onlyArm(b);
        if (armA && armB && *armA != *armB) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<RequiredOrdering> requiredOrderings(const Program &program, const GCGraph &graph) {
    std::vector<gc_node_ref_t> touching;
    for (gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        if (!resourcesOf(program, graph, ref).none()) {
            touching.push_back(ref);
        }
    }
    const auto branches = armReach(graph);
    std::vector<RequiredOrdering> pairs;
    for (size_t i = 0; i < touching.size(); ++i) {
        for (size_t j = i + 1; j < touching.size(); ++j) {
            if (exclusive(branches, touching[i], touching[j])) {
                continue; // different arms of one branch: they never both run
            }
            const ResourceConflict c = resourceConflict(program, graph, touching[i], touching[j]);
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
        std::string allocates = std::format("allocates {}", sem::resourceClassName(r.allocates->cls));
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
    if (fp.external) {
        part("external");
    }
    if (!fp.declared) {
        part("conservative");
    }
    return out.empty() ? "none" : out;
}

} // namespace camel::runtime
