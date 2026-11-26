// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/block_form.h"

#include "camel/core/operator_table.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::runtime {
namespace {

/// The same FNV-1a the portable hash uses, over explicitly little-endian words: a hash read in two
/// processes must not depend on how a word happens to be laid out.
struct Fnv {
    uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const unsigned char *p, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            h = (h ^ p[i]) * 0x100000001b3ull;
        }
    }
    void u64(uint64_t value) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) {
            b[i] = static_cast<unsigned char>(value >> (8 * i));
        }
        bytes(b, 8);
    }
    void str(std::string_view text) {
        u64(text.size());
        bytes(reinterpret_cast<const unsigned char *>(text.data()), text.size());
    }
};

/// One walk of one body. `stack` holds the graphs being hashed, outermost first: a call back into
/// one of them contributes its distance instead of recursing, which is what makes a recursive body
/// finite while still telling two different recursive shapes apart.
class Walker {
  public:
    explicit Walker(const Program &program) : program_(program) {}

    uint64_t hashGraph(const GCGraph *graph) {
        for (size_t i = 0; i < stack_.size(); ++i) {
            if (stack_[i] == graph) {
                Fnv h;
                h.str("called-again");
                h.u64(stack_.size() - i);
                return h.h;
            }
        }
        Fnv h;
        h.str("graph");
        // The function type mangled: the mangling keeps the parameters' types and drops their
        // names.
        h.str(graph->funcType() != nullptr ? graph->funcType()->mangle() : std::string_view{});
        h.u64(graph->normPorts().size());
        h.u64(graph->withPorts().size());
        h.u64(static_cast<uint64_t>(graph->returnKind()));
        stack_.push_back(graph);
        regions_.push_back(regionsOf(*graph));
        // Which node is which parameter: the graph's port lists, by position, and each region's
        // form ports, by position in that region. A parameter node's payload is not its identity --
        // two parameters can carry the same payload -- while a position in the signature it belongs
        // to is spelling-free and a name is not. A call inside the body hashes the callee against
        // the callee's own ports, so this map is put back when that walk returns: a node of this
        // graph read after the call is read against this graph's signature.
        std::unordered_map<gc_node_ref_t, uint64_t> outer = std::move(ports_);
        ports_                                            = positionsOf(*graph);
        std::unordered_map<gc_node_ref_t, uint64_t> memo;
        h.u64(hashNode(*graph, graph->exitNodeRef(), memo));
        ports_ = std::move(outer);
        regions_.pop_back();
        stack_.pop_back();
        return h.h;
    }

  private:
    /// The regions of one graph being hashed, each roster by the node that owns it, in the order
    /// the region table names them.
    using RegionTable = std::unordered_map<gc_node_ref_t, std::vector<uint32_t>>;

    /// A region is named by what the graph says it is: its kind, its number among the regions of
    /// its owner (an arm's arm number; a loop's condition and body are told apart by kind), and the
    /// signature of its form ports. The order of a roster is structural, so the walk enters a
    /// control node's regions in the same order however the graph was built; a region's own node
    /// list is never read, since that list is in creation order.
    static RegionTable regionsOf(const GCGraph &graph) {
        RegionTable regions;
        for (uint32_t id = 1; id < graph.regionCount(); ++id) {
            const GCRegion *region = graph.region(id);
            if (region == nullptr || region->kind == GCRegionKind::Dead) {
                continue;
            }
            regions[region->owner].push_back(id);
        }
        for (auto &roster : regions) {
            std::sort(
                roster.second.begin(),
                roster.second.end(),
                [&graph](uint32_t one, uint32_t other) {
                    const GCRegion *left  = graph.region(one);
                    const GCRegion *right = graph.region(other);
                    // An arm by its arm number, a loop's condition before the body it guards.
                    return left->kind != right->kind ? left->kind < right->kind
                                                     : left->index < right->index;
                });
        }
        return regions;
    }

    /// The position of every parameter node of `graph`: its own ports, then each region's form
    /// ports in the order that region writes them.
    static std::unordered_map<gc_node_ref_t, uint64_t> positionsOf(const GCGraph &graph) {
        std::unordered_map<gc_node_ref_t, uint64_t> positions;
        uint64_t index = 0;
        for (gc_node_ref_t port : graph.normPorts()) {
            positions.emplace(port, index++);
        }
        for (gc_node_ref_t port : graph.withPorts()) {
            positions.emplace(port, index++);
        }
        for (uint32_t id = 1; id < graph.regionCount(); ++id) {
            const GCRegion *region = graph.region(id);
            if (region == nullptr || region->kind == GCRegionKind::Dead) {
                continue;
            }
            uint64_t position = 0;
            for (gc_node_ref_t port : graph.regionPorts(id)) {
                positions.emplace(port, position++);
            }
        }
        return positions;
    }

    /// The canonical form of one region: what it is, the signature of its form ports, each form
    /// port by its position in the region, and the value the region yields. The statements of a
    /// region are reached through that value -- a region's exit reads the value it hands out and
    /// keeps the writes it must not drop -- so a difference that lives only inside a branch arm or
    /// a loop body is a difference in the hash. A pure computation of the region that nothing reads
    /// is not part of the body's visible structure and is hashed nowhere else either.
    uint64_t hashRegion(
        const GCGraph &graph, uint32_t id, std::unordered_map<gc_node_ref_t, uint64_t> &memo) {
        const GCRegion *region = graph.region(id);
        Fnv h;
        h.str("region");
        h.u64(static_cast<uint64_t>(region->kind));
        h.u64(region->index);
        if (region->params != nullptr) {
            h.str(region->params->mangle());
        } else {
            h.str(std::string_view{});
        }
        const std::span<const gc_node_ref_t> formPorts = graph.regionPorts(id);
        h.u64(formPorts.size());
        for (gc_node_ref_t port : formPorts) {
            h.u64(hashNode(graph, port, memo));
        }
        if (region->result != kInvalidNodeRef) {
            h.u64(hashNode(graph, region->result, memo));
        }
        return h.h;
    }

    /// The canonical form of one node: what it is, then the canonical forms of what it takes, in
    /// the order its ports are written, then its regions. Memoised within one graph, since a node
    /// reached twice is one node -- the memo is a walk of this hash and never enters the value.
    uint64_t hashNode(
        const GCGraph &graph, gc_node_ref_t ref,
        std::unordered_map<gc_node_ref_t, uint64_t> &memo) {
        if (const auto found = memo.find(ref); found != memo.end()) {
            return found->second;
        }
        const GCNode *node = graph.node(ref);
        if (node == nullptr) {
            return 0;
        }
        Fnv h;
        h.str("node");
        h.u64(static_cast<uint64_t>(node->kind));
        h.u64(node->flags);
        h.str(node->dataType() != nullptr ? node->dataType()->mangle() : std::string_view{});
        h.u64(graph.nodePermissions(ref));
        if (const uint32_t attrs = graph.nodeAttrs(ref); attrs != 0) {
            h.str("attrs:" + camel::core::attr::text(attrs));
        }
        // A port node is a parameter, whatever kind the builder gave it: its identity is its
        // position in the signature it belongs to. Its payload does not say which parameter it is
        // (two parameters of one body can carry the same payload), and a name would be spelling.
        if (const auto port = ports_.find(ref); port != ports_.end()) {
            h.str("param");
            h.u64(port->second);
            memo.emplace(ref, h.h);
            return h.h;
        }
        switch (node->kind) {
        case GCNodeKind::Oper: {
            const camel::core::OperId oper = graph.nodeBodyAs<GCOperBody>(ref)->oper;
            const auto &table              = camel::core::OperatorTable::instance();
            h.str(table.uri(oper));
            const std::optional<camel::core::OperatorTraits> traits = table.find(oper);
            h.u64(
                !traits ? 0xFF
                        : (uint64_t(traits->pure) | uint64_t(traits->elementwise) << 1 |
                           uint64_t(traits->stateOnly) << 2 | uint64_t(traits->mayFault) << 3 |
                           uint64_t(traits->mayNotTerminate) << 6));
            break;
        }
        case GCNodeKind::Fill: {
            const auto *body = graph.nodeBodyAs<GCFillBody>(ref);
            h.u64(static_cast<uint64_t>(body->fillKind));
            for (gc_slot_idx_t slot : body->slots()) {
                // A parameter's identity is its position in the signature: a name does not enter,
                // and two bodies that differ in which parameter they use must not collide.
                h.u64(static_cast<uint64_t>(static_cast<int64_t>(slot)));
            }
            break;
        }
        case GCNodeKind::Data: {
            if (node->dataIndex < 0 && graph.staticArea() != nullptr) {
                const camel::core::type::Type *type = node->dataType();
                const slot_t slot = graph.staticArea()->get<slot_t>(size_t(-node->dataIndex));
                if (type != nullptr && type->isGCTraced()) {
                    if (auto *object =
                            camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(slot)) {
                        std::ostringstream text;
                        object->print(text, type);
                        h.str(text.str());
                    } else {
                        h.str("null");
                    }
                } else {
                    h.u64(static_cast<uint64_t>(slot));
                }
            }
            break;
        }
        case GCNodeKind::Func:
        case GCNodeKind::Call: {
            if (GCGraph *callee = program_.calleeOf(graph, ref)) {
                // By the callee's form and never by its name: a function's name is spelling.
                h.u64(hashGraph(callee));
            }
            break;
        }
        case GCNodeKind::Loop:
            h.u64(static_cast<uint64_t>(graph.loopKindOf(ref)));
            break;
        case GCNodeKind::Brch:
            h.u64(graph.nodeBodyAs<GCBrchBody>(ref)->armCount);
            break;
        case GCNodeKind::Join:
            h.u64(graph.nodeBodyAs<GCJoinBody>(ref)->armCount);
            break;
        default:
            break;
        }
        memo.emplace(ref, 0);
        std::vector<gc_node_ref_t> children;
        for (gc_node_ref_t in : graph.normInputsOf(ref)) {
            children.push_back(in);
        }
        for (gc_node_ref_t in : graph.withInputsOf(ref)) {
            children.push_back(in);
        }
        for (gc_node_ref_t in : graph.ctrlInputsOf(ref)) {
            children.push_back(in);
        }
        h.u64(children.size());
        for (gc_node_ref_t child : children) {
            h.u64(hashNode(graph, child, memo));
        }
        // The regions this node owns, after its ports: a branch's arms in arm order, a loop's
        // condition before the body it guards. The roster is copied first, because hashing a region
        // can hash another graph, and that walk lays its own regions down on top of these.
        std::vector<uint32_t> owned;
        if (const auto roster = regions_.back().find(ref); roster != regions_.back().end()) {
            owned = roster->second;
        }
        h.u64(owned.size());
        for (uint32_t id : owned) {
            h.u64(hashRegion(graph, id, memo));
        }
        memo[ref] = h.h;
        return h.h;
    }

    const Program &program_;
    std::vector<const GCGraph *> stack_;
    /// The regions of each graph being hashed, outermost first, beside `stack_`.
    std::vector<RegionTable> regions_;
    /// The graph being hashed, port refs to their position in the signature they belong to.
    std::unordered_map<gc_node_ref_t, uint64_t> ports_;
};

} // namespace

uint64_t blockFormHash(const Program &program, const GCGraph *graph) {
    if (graph == nullptr) {
        return 0;
    }
    return Walker(program).hashGraph(graph);
}

} // namespace camel::runtime