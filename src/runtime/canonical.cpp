// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The canonical order and numbering of a frozen graph (camel/runtime/canonical.h): a walk of the
// graph by its own structure, with the ties arranged so that the numbering table is smallest.

#include "camel/runtime/canonical.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

namespace camel::runtime {

namespace {

/// The role a port plays, as a number: the ports of one graph are told apart by their role and
/// their position, never by the name the source gave them.
enum class CanonicalPortRole : uint8_t { None, Normal, Context, Capture };

} // namespace

/// The canonical walk over one graph. It owns the numbering it produces and the temporary state a
/// tie's arrangements are compared on, so that comparing two arrangements costs a copy of a
/// vector of numbers and the walk they lead to.
class CanonicalWalk {
  public:
    CanonicalWalk([[maybe_unused]] const Program &program, const GCGraph &graph)
        : graph_(graph), places_(graph.nodeCount(), kNoCanonicalNumber),
          live_(graph.nodeCount(), false), read_(graph.nodeCount(), false),
          role_(graph.nodeCount(), CanonicalPortRole::None), active_(graph.nodeCount(), false) {
        for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
            const gc_node_ref_t ref = it.ref();
            if (ref >= places_.size()) {
                continue;
            }
            live_[ref] = true;
            refs_.push_back(ref);
        }
        noteRegions();
        noteReaders();
        notePortRoles();
    }

    std::vector<gc_node_ref_t> run() {
        if (refs_.empty()) {
            return {};
        }
        // The exit: the value the graph yields. Everything the value is computed from is numbered
        // before it, so a document's nodes read in order are also a legal dependency order.
        const gc_node_ref_t exit = graph_.exitNodeRef();
        if (isLive(exit)) {
            number(exit);
        }
        // Each region: its result, then the statements nothing reads (the external accesses and
        // the computations nobody uses). Region 0 is the body, which has no record in a graph
        // that has no other region.
        for (uint32_t id : regions_) {
            const GCRegion *region = graph_.region(id);
            if (region != nullptr && isLive(region->result)) {
                number(region->result);
            }
            arrange(statementsOf(id));
        }
        // The interface, in port order, then the capture ports. The capture table's order is the
        // order the source captured the values in, which is spelling, so the captures are one tie
        // group even when nothing reads them.
        for (gc_node_ref_t port : graph_.normPorts()) {
            number(port);
        }
        for (gc_node_ref_t port : graph_.withPorts()) {
            number(port);
        }
        arrange(capturePorts());
        // What the walk did not reach: a dead write, a computation whose value nothing uses. It
        // is not part of the default query of a program, but a document that prints the graph
        // prints it, so it is placed after everything the walk does reach, one region at a time.
        for (uint32_t id : regions_) {
            std::vector<gc_node_ref_t> leftovers;
            for (gc_node_ref_t ref : refs_) {
                if (places_[ref] != kNoCanonicalNumber || regionOf(ref) != id) {
                    continue;
                }
                leftovers.push_back(ref);
            }
            arrange(std::move(leftovers));
        }
        for (gc_node_ref_t ref : refs_) {
            place(ref);
        }
        return sequence_;
    }

  private:
    // ---- the walk -------------------------------------------------------------------------

    bool isLive(gc_node_ref_t ref) const {
        return ref != kInvalidNodeRef && ref < live_.size() && live_[ref];
    }

    uint32_t regionOf(gc_node_ref_t ref) const {
        return graph_.nodeRegion(ref);
    }

    /// The regions of the graph, the body first, in id order. A graph with no region but the body
    /// has an empty table, and the body is still region 0.
    void noteRegions() {
        regions_.push_back(0);
        for (uint32_t id = 1; id < graph_.regionCount(); ++id) {
            const GCRegion *region = graph_.region(id);
            if (region == nullptr || region->kind == GCRegionKind::Dead) {
                continue;
            }
            regions_.push_back(id);
        }
    }

    /// Which nodes are read by another node: its data, wait, sync and order predecessors are all
    /// "used" (a sync edge carries a use; an order edge is the order the program wrote between two
    /// uses of one resource). A node nothing reads is a statement: it runs for what it does.
    void noteReaders() {
        for (gc_node_ref_t ref : refs_) {
            const auto note = [&](gc_node_ref_t input) {
                if (isLive(input)) {
                    read_[input] = true;
                }
            };
            for (gc_node_ref_t input : graph_.normInputsOf(ref)) {
                note(input);
            }
            for (gc_node_ref_t input : graph_.withInputsOf(ref)) {
                note(input);
            }
            for (gc_node_ref_t input : graph_.ctrlInputsOf(ref)) {
                note(input);
            }
            for (const GCGraphSyncEdge &edge : graph_.syncPredsOf(ref)) {
                note(edge.pred);
            }
            for (const GCGraphOrderEdge &edge : graph_.orderPredsOf(ref)) {
                note(edge.pred);
            }
        }
    }

    /// The role of every port, so that two ports are told apart by what they are and not by the
    /// name they were given.
    void notePortRoles() {
        for (gc_node_ref_t port : graph_.normPorts()) {
            if (isLive(port)) {
                role_[port] = CanonicalPortRole::Normal;
            }
        }
        for (gc_node_ref_t port : graph_.withPorts()) {
            if (isLive(port)) {
                role_[port] = CanonicalPortRole::Context;
            }
        }
        for (gc_node_ref_t port : graph_.closureNodes()) {
            if (isLive(port)) {
                role_[port] = CanonicalPortRole::Capture;
            }
        }
    }

    std::vector<gc_node_ref_t> statementsOf(uint32_t region) const {
        std::vector<gc_node_ref_t> statements;
        for (gc_node_ref_t ref : refs_) {
            if (read_[ref] || places_[ref] != kNoCanonicalNumber) {
                continue;
            }
            if (regionOf(ref) != region || graph_.node(ref)->kind == GCNodeKind::Port) {
                continue;
            }
            statements.push_back(ref);
        }
        return statements;
    }

    std::vector<gc_node_ref_t> capturePorts() const {
        std::vector<gc_node_ref_t> ports;
        for (gc_node_ref_t port : graph_.closureNodes()) {
            if (isLive(port)) {
                ports.push_back(port);
            }
        }
        return ports;
    }

    /// Number `ref` and everything it is computed from: its ports in order, the regions of a
    /// control node in region order, then its sync and order predecessors.
    void number(gc_node_ref_t ref) {
        if (!isLive(ref) || places_[ref] != kNoCanonicalNumber || active_[ref]) {
            return;
        }
        active_[ref] = true;
        for (gc_node_ref_t input : graph_.normInputsOf(ref)) {
            number(input);
        }
        // A closure value is a FILL of a function value (its first normal input) with the values
        // the function captured, linked as its with inputs in the order of the closure ports -- the
        // order the source captured them in, which is spelling (design principles 9). They are a
        // tie group, arranged like any other.
        const std::span<const gc_node_ref_t> withs = graph_.withInputsOf(ref);
        if (isClosureValue(ref) && withs.size() > 1) {
            arrange(std::vector<gc_node_ref_t>(withs.begin(), withs.end()));
        } else {
            for (gc_node_ref_t input : withs) {
                number(input);
            }
        }
        for (gc_node_ref_t input : graph_.ctrlInputsOf(ref)) {
            number(input);
        }
        for (uint32_t offset = 0; offset < regionCount(); ++offset) {
            const uint32_t id     = regions_[offset];
            const GCRegion *entry = graph_.region(id);
            if (entry != nullptr && entry->owner == ref) {
                number(entry->result);
            }
        }
        for (const GCGraphSyncEdge &edge : graph_.syncPredsOf(ref)) {
            number(edge.pred);
        }
        for (const GCGraphOrderEdge &edge : graph_.orderPredsOf(ref)) {
            number(edge.pred);
        }
        active_[ref] = false;
        place(ref);
    }

    size_t regionCount() const { return regions_.size(); }

    /// Whether `ref` is a closure value: a FILL of a function value with the values it captured.
    bool isClosureValue(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        return node != nullptr && node->kind == GCNodeKind::Fill &&
               graph_.nodeBodyAs<GCFillBody>(ref)->fillKind == GCFillKind::FunctionClosure;
    }

    void place(gc_node_ref_t ref) {
        if (!isLive(ref) || places_[ref] != kNoCanonicalNumber) {
            return;
        }
        places_[ref] = static_cast<uint32_t>(sequence_.size());
        sequence_.push_back(ref);
    }

    // ---- ties -----------------------------------------------------------------------------

    /// Number a group whose relative order the ports do not determine.
    ///
    /// The graph may still order two members of the group (an order edge between two statements,
    /// or a path of them), and those orders are kept; what is left is a tie, and a tie is
    /// arranged so that the numbering table is smallest.
    void arrange(std::vector<gc_node_ref_t> group) {
        std::vector<gc_node_ref_t> pending;
        for (gc_node_ref_t ref : group) {
            if (isLive(ref) && places_[ref] == kNoCanonicalNumber) {
                pending.push_back(ref);
            }
        }
        if (pending.empty()) {
            return;
        }
        const std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> orders = orderWithin(pending);
        while (!pending.empty()) {
            std::vector<gc_node_ref_t> frontier;
            for (gc_node_ref_t ref : pending) {
                const bool waits = std::any_of(
                    orders.begin(),
                    orders.end(),
                    [&](const auto &pair) {
                        return pair.second == ref && contains(pending, pair.first);
                    });
                if (!waits) {
                    frontier.push_back(ref);
                }
            }
            if (frontier.empty()) {
                // No linear extension: the graph orders a cycle, which a frozen graph does not
                // have. Take what is left in the group's own order rather than dropping it.
                for (gc_node_ref_t ref : pending) {
                    number(ref);
                }
                return;
            }
            for (gc_node_ref_t ref : smallestTable(frontier)) {
                number(ref);
            }
            pending.erase(
                std::remove_if(
                    pending.begin(),
                    pending.end(),
                    [&](gc_node_ref_t ref) { return places_[ref] != kNoCanonicalNumber; }),
                pending.end());
        }
    }

    static bool contains(const std::vector<gc_node_ref_t> &list, gc_node_ref_t ref) {
        return std::find(list.begin(), list.end(), ref) != list.end();
    }

    /// The pairs of `group` one member orders before the other: a path of the graph's edges,
    /// where the two ends are in the group. A path that leaves the group says nothing about two
    /// statements: what is between them may be numbered in either place.
    std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>>
    orderWithin(const std::vector<gc_node_ref_t> &group) const {
        std::unordered_set<gc_node_ref_t> members(group.begin(), group.end());
        std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> orders;
        for (gc_node_ref_t first : group) {
            std::unordered_set<gc_node_ref_t> seen{first};
            std::vector<gc_node_ref_t> stack{first};
            while (!stack.empty()) {
                const gc_node_ref_t at = stack.back();
                stack.pop_back();
                const auto visit = [&](gc_node_ref_t next) {
                    if (!isLive(next) || !seen.insert(next).second) {
                        return;
                    }
                    if (members.contains(next)) {
                        orders.emplace_back(first, next);
                    }
                    stack.push_back(next);
                };
                for (gc_node_ref_t next : graph_.normUsersOf(at)) {
                    visit(next);
                }
                for (gc_node_ref_t next : graph_.withUsersOf(at)) {
                    visit(next);
                }
                for (gc_node_ref_t next : graph_.ctrlUsersOf(at)) {
                    visit(next);
                }
                for (gc_node_ref_t next : graph_.syncUsersOf(at)) {
                    visit(next);
                }
                for (gc_node_ref_t next : graph_.orderUsersOf(at)) {
                    visit(next);
                }
            }
        }
        return orders;
    }

    /// The arrangement of `tied` whose numbering table is smallest.
    ///
    /// Every arrangement is numbered, and the arrangements are compared by the list of the nodes
    /// they emit, in numbering order, each node described by what it is and by the numbers its
    /// ports already carry (nodeKey). At most kMaxTieWidth nodes are enumerated; a wider tie is
    /// arranged by the same key taken one node at a time, which is the same answer wherever the
    /// key tells the nodes apart.
    std::vector<gc_node_ref_t> smallestTable(const std::vector<gc_node_ref_t> &tied) {
        if (tied.size() <= 1) {
            return tied;
        }
        // A tie inside a tie is arranged by the key alone: comparing the outer arrangements must
        // not enumerate the inner ones again, which would multiply the two widths' factorials.
        if (tied.size() > kMaxTieWidth || enumerating_) {
            std::vector<gc_node_ref_t> ordered = tied;
            std::stable_sort(
                ordered.begin(),
                ordered.end(),
                [&](gc_node_ref_t a, gc_node_ref_t b) { return nodeKey(a) < nodeKey(b); });
            return ordered;
        }
        std::vector<gc_node_ref_t> candidate = tied;
        std::sort(candidate.begin(), candidate.end());
        std::vector<gc_node_ref_t> best = candidate;
        std::string bestKey             = tableOf(candidate);
        while (std::next_permutation(candidate.begin(), candidate.end())) {
            const std::string key = tableOf(candidate);
            if (key < bestKey) {
                bestKey = key;
                best    = candidate;
            }
        }
        return best;
    }

    /// The table one arrangement of a tie writes: the nodes it emits, in numbering order, each
    /// with the description nodeKey gives it.
    std::string tableOf(const std::vector<gc_node_ref_t> &arrangement) {
        const std::vector<uint32_t> savedPlaces     = places_;
        const std::vector<gc_node_ref_t> savedOrder = sequence_;
        const size_t begin                          = sequence_.size();
        const bool savedEnumerating                 = enumerating_;
        enumerating_                                = true;
        for (gc_node_ref_t ref : arrangement) {
            number(ref);
        }
        std::string table;
        for (size_t offset = begin; offset < sequence_.size(); ++offset) {
            table += nodeKey(sequence_[offset]);
            table += '\n';
        }
        places_    = savedPlaces;
        sequence_  = savedOrder;
        enumerating_ = savedEnumerating;
        active_.assign(active_.size(), false);
        return table;
    }

    /// What a node is, for comparing two arrangements' tables: its kind, the identity of what it
    /// carries (an operator's URI, the value a constant holds, the role of a port, a loop's form),
    /// and the numbers its ports carry. Nothing the source spelled: not a name, not a position,
    /// not a NodeId.
    std::string nodeKey(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        std::string key;
        key += static_cast<char>('A' + static_cast<int>(node->kind));
        switch (node->kind) {
        case GCNodeKind::Oper:
            key += ':';
            key += std::string(graph_.nodeBodyAs<GCOperBody>(ref)->uri());
            break;
        case GCNodeKind::Data:
            key += ':';
            key += constantText(ref);
            break;
        case GCNodeKind::Port:
            key += static_cast<char>('0' + static_cast<int>(role_[ref]));
            break;
        case GCNodeKind::Fill:
            key += static_cast<char>(
                '0' + static_cast<int>(graph_.nodeBodyAs<GCFillBody>(ref)->fillKind));
            break;
        case GCNodeKind::Loop:
            key += static_cast<char>('0' + static_cast<int>(graph_.loopKindOf(ref)));
            break;
        default:
            break;
        }
        const auto append = [&](gc_node_ref_t input) {
            key += ',';
            key += input < places_.size() && places_[input] != kNoCanonicalNumber
                       ? std::to_string(places_[input])
                       : std::string("-");
        };
        for (gc_node_ref_t input : graph_.normInputsOf(ref)) {
            append(input);
        }
        for (gc_node_ref_t input : graph_.withInputsOf(ref)) {
            key += '|';
            append(input);
        }
        return key;
    }

    /// The text of the value a constant node holds (a data node's static slot), empty when it
    /// holds none. Two constants with the same text are the same value, which is what the
    /// numbering may compare; the place the value sits at in the static area is not.
    std::string constantText(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        if (node == nullptr || node->dataIndex >= 0) {
            return {};
        }
        const auto slots   = graph_.staticSlots();
        const size_t index = static_cast<size_t>(-node->dataIndex);
        if (index >= slots.size()) {
            return {};
        }
        camel::core::type::Type *type = node->dataType();
        if (type == nullptr) {
            return {};
        }
        std::ostringstream os;
        camel::core::rtdata::printSlot(os, slots[index], type);
        return os.str();
    }

    const GCGraph &graph_;
    std::vector<gc_node_ref_t> refs_;
    std::vector<uint32_t> places_; // NodeId -> position in `sequence_`
    std::vector<bool> live_;
    std::vector<bool> read_;
    std::vector<CanonicalPortRole> role_;
    std::vector<bool> active_;
    bool enumerating_ = false; // true while an outer tie's arrangements are being compared
    std::vector<gc_node_ref_t> sequence_;
    std::vector<uint32_t> regions_;
};

std::vector<gc_node_ref_t> canonicalNodeOrder(const Program &program, const GCGraph &graph) {
    return CanonicalWalk(program, graph).run();
}

std::vector<uint32_t> canonicalNodeNumbers(const Program &program, const GCGraph &graph) {
    std::vector<uint32_t> numbers(graph.nodeCount(), kNoCanonicalNumber);
    const std::vector<gc_node_ref_t> order = canonicalNodeOrder(program, graph);
    for (size_t offset = 0; offset < order.size(); ++offset) {
        numbers[order[offset]] = static_cast<uint32_t>(offset);
    }
    return numbers;
}

} // namespace camel::runtime
