// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The canonical order and numbering of a frozen graph (camel/runtime/canonical.h): a walk of the
// graph by its own structure, with the ties arranged so that the numbering table is smallest, and
// that minimum taken over the whole program rather than fixed per graph.

#include "camel/runtime/canonical.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/runtime/reachable.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_set>

namespace camel::runtime {

namespace {

/// The role a port plays, as a number: the ports of one graph are told apart by their role and
/// their position, never by the name the source gave them.
enum class CanonicalPortRole : uint8_t { None, Normal, Context, Capture };

/// The type of the value a node makes, which is what tells two nodes apart when the operator says
/// nothing: two captures of different types are two different things, and the numbering may read
/// that without reading a name. `nodeStaticSlotType`'s rule, on the runtime side.
const camel::core::type::Type *staticTypeOf(const GCGraph &graph, const GCNode &node) {
    if (node.dataIndex >= 0) {
        return node.dataType();
    }
    const auto types   = graph.staticDataType();
    const size_t index = static_cast<size_t>(-node.dataIndex);
    if (types != nullptr && index < types->size()) {
        return types->typeAt(index);
    }
    return node.dataType();
}

/// How many arrangements of one equal-key tie part are compared at most: what one walk may spend on
/// a tie before it settles for the structural order its keys and fragments already give.
constexpr size_t kMaxTieCandidates = 4096;

/// How many times a part's shape is refined by its neighbours' shapes before the walk stops: a part
/// the shape still leaves equal after that is one the graph does not separate as far as this
/// classification can see, and it is reported instead of being arranged silently.
constexpr size_t kShapeRounds = 16;

} // namespace

/// The canonical walk over one graph. It owns the numbering it produces and the temporary state a
/// tie's arrangements are compared on, so that comparing two arrangements costs a copy of a
/// vector of numbers and the walk they lead to.
///
/// `bound` is what the program has already numbered for the graphs this one binds: a binder -- the
/// FILL that makes a function value, the FUNC that calls a graph -- reads the graph it binds through
/// that graph's ports, by their numbers, and the numbering of a tie that pairs a captured value with
/// a capture port is compared on those numbers (canonical.h).
class CanonicalWalk {
  public:
    CanonicalWalk(
        const Program &program, const GCGraph &graph,
        const std::unordered_map<const GCGraph *, std::vector<uint32_t>> *bound,
        std::vector<CanonicalTie> *ties)
        : program_(program), graph_(graph), bound_(bound), ties_(ties),
          places_(graph.nodeCount(), kNoCanonicalNumber), live_(graph.nodeCount(), false),
          read_(graph.nodeCount(), false), role_(graph.nodeCount(), CanonicalPortRole::None),
          active_(graph.nodeCount(), false) {
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
        // What the walk did not reach: the default query of a program has no place for a node no
        // root reaches, so this section is what the order says about one.
        //
        // It is **a section and not an answer**: the tail goes after everything the walk does
        // reach, one region at a time, and inside itself it is ordered by the same tie rule the
        // rest of the graph uses -- the node key first, the equal-key part by the smallest
        // numbering table, and a part wider than the enumeration limit cut by each node's own
        // fragment (canonical.h). So the tail is a total order the graph decides: two writings of
        // one graph give one tail, and `same_graph` reads them as one document. What the position
        // is *not* is "where the node would stand if something reached it" -- there is no such
        // place, and inventing one would mean walking from a root the graph does not have.
        //
        // Measured before it was written down: of the 4037 `.cml` files in this repository, 207
        // have a graph the walk leaves a node of, and every one of those nodes is a region port
        // that nothing in its region reads -- `loop a = 0, b = 0 while b < n do (a + 1, b + 1)`
        // leaves the condition port of `a` with no reader. The section is real and common, not a
        // leftover of the model.
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
        // A node whose region the walk holds no record of -- a control region a publish left
        // `Dead`, which is what a graph built from a *subset* of a draft's nodes gets
        // (`planDraftPayload`) -- is a member of the same section and arranged the same way. It
        // is not placed here in NodeId order: that is the order the nodes were
        // created in, which is the spelling, a reading that would only look right because no program
        // in this repository reaches it today. The outside nodes are one group because a dead
        // region is not a structure the walk can order by.
        {
            std::vector<gc_node_ref_t> outside;
            for (gc_node_ref_t ref : refs_) {
                if (places_[ref] == kNoCanonicalNumber) {
                    outside.push_back(ref);
                }
            }
            arrange(std::move(outside));
        }
        // The last resort, and it is not the tail's ordinary path: `arrange` places every member of
        // its group unless the node is one the walk is *inside* right now (a cycle of the group's
        // own). A frozen graph has none, and a graph that did would get this order -- the group's
        // own, which for a graph no program can produce is the only answer there is.
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

    uint32_t regionOf(gc_node_ref_t ref) const { return graph_.nodeRegion(ref); }

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
        // A region's exit GATE reads the value the region hands out first and the writes it keeps
        // alive after it (`Builder::layRegionWriteEdges`, design principles 3.1). Nothing joins
        // those writes to one another, so
        // their place in the GATE's list is the order the source wrote them in, which is
        // spelling, and they are a tie group arranged like the capture table below. Without this the
        // list decides the numbering: two writings that swap `println(x)` and a write to a file
        // numbered the two in the order they were written, and the two writings, whose output is the same, read as
        // two documents instead of one
        // (`test/cases/trans/same_graph_writings.py`).
        const std::span<const gc_node_ref_t> norms = graph_.normInputsOf(ref);
        const GCNode *numbered                         = graph_.node(ref);
        if (numbered != nullptr && numbered->kind == GCNodeKind::Gate && norms.size() > 2) {
            number(norms.front());
            arrange(std::vector<gc_node_ref_t>(norms.begin() + 1, norms.end()));
        } else {
            for (gc_node_ref_t input : norms) {
                number(input);
            }
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
    /// The group is first cut by the node key: two nodes whose keys differ are already ordered by
    /// the key, because the key is the first thing their tables say and the smallest table puts the
    /// smallest key first. What the key leaves equal -- the nodes that are the same thing down to
    /// the numbers their ports carry and the type of the value they make -- is the only part whose
    /// order the table has to decide, and that part is enumerated when it is no wider than
    /// kMaxTieWidth. A wider part is not enumerated; it is cut by what each node writes on its own
    /// (its own fragment, and the numbers of the nodes that read it) and then by the shape it has in
    /// the graph, and a part both leave equal is reported through `ties` instead of being arranged
    /// in the order it came in.
    std::vector<gc_node_ref_t> smallestTable(const std::vector<gc_node_ref_t> &tied) {
        if (tied.size() <= 1) {
            return tied;
        }
        CanonicalTie tie;
        tie.graph = &graph_;
        tie.width = tied.size();

        std::vector<gc_node_ref_t> ordered = tied;
        std::vector<std::string> keys;
        keys.reserve(ordered.size());
        for (gc_node_ref_t ref : ordered) {
            keys.push_back(nodeKey(ref));
        }
        std::vector<size_t> order(ordered.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return keys[a] < keys[b];
        });
        std::vector<gc_node_ref_t> sorted;
        sorted.reserve(ordered.size());
        for (size_t i : order) {
            sorted.push_back(ordered[i]);
        }

        std::vector<gc_node_ref_t> result;
        result.reserve(sorted.size());
        size_t begin = 0;
        bool enumeratedAll = true;
        while (begin < sorted.size()) {
            size_t end = begin + 1;
            const std::string &key = keys[order[begin]];
            while (end < sorted.size() && keys[order[end]] == key) {
                ++end;
            }
            std::vector<gc_node_ref_t> run(
                sorted.begin() + static_cast<std::ptrdiff_t>(begin),
                sorted.begin() + static_cast<std::ptrdiff_t>(end));
            tie.equalKeyWidth = std::max(tie.equalKeyWidth, run.size());
            size_t unresolved = 0;
            const bool enumerated = arrangeEqualKeyRun(run, unresolved);
            tie.unresolvedWidth   = std::max(tie.unresolvedWidth, unresolved);
            enumeratedAll         = enumeratedAll && enumerated;
            result.insert(result.end(), run.begin(), run.end());
            begin = end;
        }
        tie.enumerated = enumeratedAll;
        if (ties_ != nullptr) {
            ties_->push_back(tie);
        }
        return result;
    }

    /// Order one run of nodes whose keys are equal: by every arrangement when the run is narrow
    /// enough, by the graph's own structure when it is not. Returns whether every arrangement was
    /// compared; `unresolved` is the width of the widest part the graph did not separate, which is
    /// the part `ties` reports.
    bool arrangeEqualKeyRun(std::vector<gc_node_ref_t> &run, size_t &unresolved) {
        unresolved = 0;
        if (run.size() <= 1) {
            return true;
        }
        // A tie inside a tie is arranged by the structural order alone: comparing the outer
        // arrangements must not enumerate the inner ones again, which would multiply the two
        // widths' factorials.
        if (enumerating_ || run.size() > kMaxTieWidth) {
            cutByFragmentAndShape(run, unresolved);
            return false;
        }
        enumerateSmallestTable(run);
        return true;
    }

    /// Order `run` by the smallest numbering table among its arrangements.
    void enumerateSmallestTable(std::vector<gc_node_ref_t> &run) {
        std::vector<gc_node_ref_t> candidate = run;
        std::sort(candidate.begin(), candidate.end());
        std::vector<gc_node_ref_t> best = candidate;
        std::string bestKey             = tableOf(candidate);
        size_t compared                 = 1;
        while (std::next_permutation(candidate.begin(), candidate.end())) {
            if (++compared > kMaxTieCandidates) {
                break;
            }
            const std::string key = tableOf(candidate);
            if (key < bestKey) {
                bestKey = key;
                best    = candidate;
            }
        }
        run = std::move(best);
    }

    /// Order a run the node key left equal by the graph's structure, and count what the structure
    /// leaves equal in `unresolved`.
    ///
    /// Two cuts, in the order of how much of the numbering they read. The fragment is the node's own
    /// table plus the numbers of the readers already numbered: it reads the decisions this walk has
    /// taken. The shape is the class `shapeLabels` gives the node, which reads no number at all. Both
    /// are functions of the graph, so the order they leave is the same for two writings of one
    /// graph, which is the property the numbering must have (design principles 9.5) and the one a
    /// stable sort on the nodes' arrival used to break. What the two cuts leave equal is a part the
    /// graph does not separate, and it is written back into `unresolved`: the walk has no criterion
    /// left for it, so it says so rather than presenting the order it met the nodes in as the
    /// graph's.
    void cutByFragmentAndShape(std::vector<gc_node_ref_t> &run, size_t &unresolved) {
        std::vector<std::pair<std::string, gc_node_ref_t>> keyed;
        keyed.reserve(run.size());
        for (gc_node_ref_t ref : run) {
            keyed.emplace_back(fragmentKey(ref), ref);
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        for (size_t i = 0; i < run.size(); ++i) {
            run[i] = keyed[i].second;
        }
        size_t begin = 0;
        while (begin < run.size()) {
            size_t end = begin + 1;
            while (end < run.size() && keyed[end].first == keyed[begin].first) {
                ++end;
            }
            if (end - begin > 1) {
                cutPartByShape(run, begin, end, unresolved);
            }
            begin = end;
        }
    }

    /// Cut `run[begin, end)` -- a part one fragment left equal -- by the shape its nodes have in the
    /// graph. What is still equal is left to `unresolved`.
    void cutPartByShape(std::vector<gc_node_ref_t> &run, size_t begin, size_t end, size_t &unresolved) {
        const std::vector<std::string> &shapes = shapeLabels();
        std::vector<std::pair<std::string, gc_node_ref_t>> keyed;
        keyed.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
            keyed.emplace_back(shapes[run[i]], run[i]);
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        for (size_t i = 0; i < keyed.size(); ++i) {
            run[begin + i] = keyed[i].second;
        }
        size_t at = begin;
        while (at < end) {
            size_t stop = at + 1;
            while (stop < end && keyed[stop - begin].first == keyed[at - begin].first) {
                ++stop;
            }
            if (stop - at > 1) {
                // The part is not enumerated even when it is narrow: the tie it belongs to was too
                // wide to enumerate, and comparing the arrangements of a part of it would rebuild
                // the tables of everything below those nodes once per arrangement -- the cost the
                // limit exists to refuse. Two cuts that leave it equal mean the walk has no
                // criterion of the graph left for it, and it says so.
                unresolved = std::max(unresolved, stop - at);
            }
            at = stop;
        }
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
        places_      = savedPlaces;
        sequence_    = savedOrder;
        enumerating_ = savedEnumerating;
        active_.assign(active_.size(), false);
        return table;
    }

    /// What one node writes when it is numbered on its own: the fragment a tie part is cut by when
    /// it is too wide to enumerate. It is the node's key and everything its own walk emits, which
    /// is the part of the table that node is responsible for.
    std::string fragmentKey(gc_node_ref_t ref) {
        std::string text = tableOf({ref});
        // What reads it is not in the node's own fragment, and it is the rest of what two equal
        // fragments can differ in: the numbers of the nodes already numbered that take this one.
        text += "<-";
        std::vector<uint32_t> readers;
        const auto collect = [&](gc_node_ref_t user) {
            if (isLive(user) && places_[user] != kNoCanonicalNumber) {
                readers.push_back(places_[user]);
            }
        };
        for (gc_node_ref_t user : graph_.normUsersOf(ref)) {
            collect(user);
        }
        for (gc_node_ref_t user : graph_.withUsersOf(ref)) {
            collect(user);
        }
        for (gc_node_ref_t user : graph_.ctrlUsersOf(ref)) {
            collect(user);
        }
        for (gc_node_ref_t user : graph_.syncUsersOf(ref)) {
            collect(user);
        }
        for (gc_node_ref_t user : graph_.orderUsersOf(ref)) {
            collect(user);
        }
        std::sort(readers.begin(), readers.end());
        for (uint32_t number : readers) {
            text += std::to_string(number);
            text += ',';
        }
        return text;
    }

    /// What a node is, for comparing two arrangements' tables: its kind, the identity of what it
    /// carries (an operator's URI, the value a constant holds, the role of a port, a loop's form),
    /// the type of the value it makes, the numbers its ports carry, and -- for a binder -- the
    /// numbers of the ports of the graph it binds, paired with the values it passes to them.
    /// Nothing the source spelled: not a name, not a position, not a NodeId.
    ///
    /// `withNumbers` false leaves every number out, which is what the shape a tie is cut by needs: a
    /// number is a decision this walk has already taken, and a criterion that reads one is a
    /// criterion about the numbering rather than about the graph. What remains is the node's kind,
    /// what it carries and the type of its value; the edges are added by `shapeLabels`.
    std::string nodeKey(gc_node_ref_t ref, bool withNumbers = true) const {
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
        key += '=';
        key += typeText(ref);
        const auto append = [&](gc_node_ref_t input) {
            key += ',';
            key += withNumbers && input < places_.size() && places_[input] != kNoCanonicalNumber
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
        // The binding is the pair (this value, that port of the bound graph), and the port's number
        // is the bound graph's numbering: a key that leaves numbers out leaves the pair out too, and
        // the binding is still in the shape, as the with inputs the closure value carries.
        const GCGraph *callee = withNumbers ? boundGraphOf(ref) : nullptr;
        if (callee != nullptr) {
            const std::vector<uint32_t> *numbers = numbersOf(*callee);
            const std::span<const gc_node_ref_t> withs = graph_.withInputsOf(ref);
            const std::span<const gc_node_ref_t> ports =
                node->kind == GCNodeKind::Func ? callee->withPorts() : callee->closureNodes();
            // The pairs are written by the value's number, not by the capture table's order: the
            // order the source captured the values in is spelling, and a key that kept it would
            // decide a tie by it. The pair itself is the binding: this value, that port.
            std::vector<std::pair<uint32_t, uint32_t>> pairs;
            pairs.reserve(withs.size());
            for (size_t i = 0; i < withs.size(); ++i) {
                const uint32_t value = withs[i] < places_.size() &&
                                               places_[withs[i]] != kNoCanonicalNumber
                                           ? places_[withs[i]]
                                           : kNoCanonicalNumber;
                const uint32_t port = numbers != nullptr && i < ports.size() &&
                                              i < numbers->size() &&
                                              (*numbers)[ports[i]] != kNoCanonicalNumber
                                          ? (*numbers)[ports[i]]
                                          : kNoCanonicalNumber;
                pairs.emplace_back(value, port);
            }
            std::sort(pairs.begin(), pairs.end());
            for (const auto &pair : pairs) {
                key += '>';
                key += pair.first == kNoCanonicalNumber ? std::string("-")
                                                        : std::to_string(pair.first);
                key += "->";
                key += pair.second == kNoCanonicalNumber ? std::string("-")
                                                         : std::to_string(pair.second);
            }
        }
        return key;
    }

    /// The class of every live node under a classification the graph alone decides: a node's key
    /// with every number left out, refined round after round by the classes of the nodes it reads
    /// and the nodes that read it, until no class splits. Two nodes of one class are the same thing
    /// for everything this walk can ask the graph without numbering it, so a tie cut by this class
    /// is cut by the graph and not by the order the nodes were created in.
    ///
    /// The refinement is what tells apart two nodes whose own keys agree but whose readers do not,
    /// which is the case a tie wider than the enumeration limit most often holds: eight cells made
    /// from one constant have one key between them, and what differs is what uses each of them. A
    /// hash of a node's subtree cannot answer this -- two nodes with equal subtrees need not be
    /// interchangeable (canonical.h) -- because the difference is downstream of them.
    ///
    /// Every class is a rank among the distinct descriptions of that round, so a label stays short
    /// and two labels are equal exactly when the two nodes are in one class; the rank is read off
    /// the sorted descriptions, never off the order the nodes came in.
    const std::vector<std::string> &shapeLabels() {
        if (shapeLabelsReady_) {
            return shapeLabels_;
        }
        shapeLabels_.assign(places_.size(), std::string());
        for (gc_node_ref_t ref : refs_) {
            shapeLabels_[ref] = nodeKey(ref, false);
        }
        size_t classes = 0;
        for (size_t round = 0; round < kShapeRounds; ++round) {
            std::vector<std::string> described;
            described.reserve(refs_.size());
            for (gc_node_ref_t ref : refs_) {
                std::string text = shapeLabels_[ref];
                text += '<';
                text += edgeText(ref, false);
                text += '>';
                text += edgeText(ref, true);
                described.push_back(std::move(text));
            }
            const size_t refined = rankClasses(described);
            // One class per node is as fine as this classification can get, and a round that splits
            // nothing has reached its fixed point: what is still equal is what the graph, read this
            // way, does not separate.
            if (refined == classes || refined == refs_.size()) {
                break;
            }
            classes = refined;
        }
        shapeLabelsReady_ = true;
        return shapeLabels_;
    }

    /// The classes of `described` as text, one per `refs_` entry, and how many distinct ones there
    /// are. A class is the rank of its description among the sorted distinct descriptions, so that
    /// two nodes are in one class exactly when their descriptions are equal.
    size_t rankClasses(std::vector<std::string> described) {
        std::vector<std::string> distinct = described;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        for (size_t i = 0; i < refs_.size(); ++i) {
            const size_t rank = static_cast<size_t>(
                std::lower_bound(distinct.begin(), distinct.end(), described[i]) -
                distinct.begin());
            shapeLabels_[refs_[i]] = 'c' + std::to_string(rank);
        }
        return distinct.size();
    }

    /// The edges at one end of a node, as text: which port of which kind, and the class of the node
    /// at the other end. Sorting them makes the text a description of the neighbourhood and not of
    /// the order the graph stores its edges in.
    std::string edgeText(gc_node_ref_t ref, bool users) const {
        std::vector<std::string> edges;
        const auto note = [&](char kind, size_t port, gc_node_ref_t other) {
            edges.push_back(
                std::string(1, kind) + std::to_string(port) + ':' +
                (other < shapeLabels_.size() ? shapeLabels_[other] : std::string("-")));
        };
        size_t port = 0;
        if (!users) {
            for (gc_node_ref_t input : graph_.normInputsOf(ref)) {
                note('n', port++, input);
            }
            port = 0;
            for (gc_node_ref_t input : graph_.withInputsOf(ref)) {
                note('w', port++, input);
            }
            port = 0;
            for (gc_node_ref_t input : graph_.ctrlInputsOf(ref)) {
                note('c', port++, input);
            }
            port = 0;
            for (const GCGraphSyncEdge &edge : graph_.syncPredsOf(ref)) {
                note('s', port++, edge.pred);
            }
            port = 0;
            for (const GCGraphOrderEdge &edge : graph_.orderPredsOf(ref)) {
                note('o', port++, edge.pred);
            }
        } else {
            for (gc_node_ref_t user : graph_.normUsersOf(ref)) {
                note('N', port++, user);
            }
            // A with user may be a binder, and a binder's with inputs are the binding itself: this
            // value goes to that port of the graph the binder names. The port belongs to the graph
            // that is numbered before this one, so its number is a fact about that graph and not a
            // decision of this walk -- and it is what tells apart two values the nodes made
            // identically and passed to two ports the body uses differently. The slot the value
            // sits on is the order the source captured it in, which is spelling, so it is left out.
            for (gc_node_ref_t user : graph_.withUsersOf(ref)) {
                const std::string bound = bindingText(user, ref);
                if (bound.empty()) {
                    note('W', port++, user);
                    continue;
                }
                edges.push_back(
                    "B" + bound + ':' +
                    (user < shapeLabels_.size() ? shapeLabels_[user] : std::string("-")));
            }
            port = 0;
            for (gc_node_ref_t user : graph_.ctrlUsersOf(ref)) {
                note('C', port++, user);
            }
            port = 0;
            for (gc_node_ref_t user : graph_.syncUsersOf(ref)) {
                note('S', port++, user);
            }
            port = 0;
            for (gc_node_ref_t user : graph_.orderUsersOf(ref)) {
                note('O', port++, user);
            }
        }
        std::sort(edges.begin(), edges.end());
        std::string text;
        for (const std::string &edge : edges) {
            text += edge;
            text += ';';
        }
        return text;
    }

    /// The number of the port of the bound graph that this value is passed to, as text, or empty
    /// when `user` binds no graph or does not take this value as one of its with inputs. The number
    /// is the bound graph's own canonical number for that port, taken before this graph is numbered
    /// (CanonicalProgram numbers the graphs a graph binds first), so it is a fact about that graph.
    std::string bindingText(gc_node_ref_t user, gc_node_ref_t ref) const {
        const GCGraph *callee = boundGraphOf(user);
        const GCNode *record  = graph_.node(user);
        if (callee == nullptr || record == nullptr) {
            return {};
        }
        const std::vector<uint32_t> *numbers = numbersOf(*callee);
        if (numbers == nullptr) {
            return {};
        }
        const std::span<const gc_node_ref_t> ports =
            record->kind == GCNodeKind::Func ? callee->withPorts() : callee->closureNodes();
        const std::span<const gc_node_ref_t> withs = graph_.withInputsOf(user);
        for (size_t i = 0; i < withs.size(); ++i) {
            if (withs[i] != ref || i >= ports.size() || i >= numbers->size()) {
                continue;
            }
            const uint32_t number = (*numbers)[ports[i]];
            return number == kNoCanonicalNumber ? std::string()
                                                : std::to_string(number);
        }
        return {};
    }

    /// The graph a node binds: the graph a FUNC calls, and the graph the function value of a
    /// closure FILL is. nullptr for every other node.
    const GCGraph *boundGraphOf(gc_node_ref_t ref) const {
        const GCNode *node = graph_.node(ref);
        if (node == nullptr) {
            return nullptr;
        }
        if (node->kind == GCNodeKind::Func) {
            return program_.calleeOf(graph_, ref);
        }
        if (!isClosureValue(ref)) {
            return nullptr;
        }
        const std::span<const gc_node_ref_t> inputs = graph_.normInputsOf(ref);
        if (inputs.empty()) {
            return nullptr;
        }
        const gc_node_ref_t value = inputs.front();
        const GCNode *source     = graph_.node(value);
        if (source == nullptr) {
            return nullptr;
        }
        if (source->kind == GCNodeKind::Func) {
            return program_.calleeOf(graph_, value);
        }
        if (source->kind != GCNodeKind::Data || source->dataIndex >= 0) {
            return nullptr;
        }
        const auto slots   = graph_.staticSlots();
        const size_t index = static_cast<size_t>(-source->dataIndex);
        if (index >= slots.size()) {
            return nullptr;
        }
        const camel::core::type::Type *type = staticTypeOf(graph_, *source);
        if (type == nullptr || type->code() != camel::core::type::TypeCode::Function) {
            return nullptr;
        }
        const auto *function = camel::core::rtdata::fromSlot<::Function *>(slots[index]);
        return function != nullptr ? function->graph() : nullptr;
    }

    /// The numbers of a graph this one binds, when the program has numbered it already.
    const std::vector<uint32_t> *numbersOf(const GCGraph &graph) const {
        if (bound_ == nullptr) {
            return nullptr;
        }
        const auto at = bound_->find(&graph);
        return at == bound_->end() ? nullptr : &at->second;
    }

    /// The type of the value a node makes, as text: cached, because a word of the key per node per
    /// arrangement would otherwise be built again and again.
    const std::string &typeText(gc_node_ref_t ref) const {
        if (ref >= typeText_.size()) {
            typeText_.resize(places_.size());
            typeTextDone_.resize(places_.size(), false);
        }
        if (!typeTextDone_[ref]) {
            const GCNode *node = graph_.node(ref);
            const camel::core::type::Type *type =
                node == nullptr ? nullptr : staticTypeOf(graph_, *node);
            typeText_[ref]   = type == nullptr ? std::string() : type->toString();
            typeTextDone_[ref] = true;
        }
        return typeText_[ref];
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

    // program-holder-exempt: borrow-or-foreign-type -- the walk reads the program its caller
    // holds, to ask which graph a FUNC calls and which one a closure value's function value is
    // (`calleeOf`, `boundGraphOf`); a walk is built on the stack inside CanonicalProgram's
    // constructor and dies there, so it lives only while the caller's program does and never keeps
    // one.
    const Program &program_;
    const GCGraph &graph_;
    const std::unordered_map<const GCGraph *, std::vector<uint32_t>> *bound_ = nullptr;
    std::vector<CanonicalTie> *ties_ = nullptr;
    std::vector<gc_node_ref_t> refs_;
    std::vector<uint32_t> places_; // NodeId -> position in `sequence_`
    std::vector<bool> live_;
    std::vector<bool> read_;
    std::vector<CanonicalPortRole> role_;
    std::vector<bool> active_;
    bool enumerating_ = false; // true while an outer tie's arrangements are being compared
    std::vector<gc_node_ref_t> sequence_;
    std::vector<uint32_t> regions_;
    mutable std::vector<std::string> typeText_;
    mutable std::vector<bool> typeTextDone_;
    // The class of every node under the classification `shapeLabels` refines: built once per walk,
    // when a tie first needs it, and never from a number, so a comparison inside `tableOf` does not
    // change it.
    std::vector<std::string> shapeLabels_;
    bool shapeLabelsReady_ = false;
};

// ---- the program -------------------------------------------------------------------------------

CanonicalProgram::CanonicalProgram(const Program &program, const GCGraph &root) {
    if (!root.hasNodePayload()) {
        return;
    }
    // Every graph the root reaches, binders after the graphs they bind: a binder reads the graph it
    // binds through that graph's ports, so the numbers of the bound graph are what the binder's own
    // numbering is compared on. A cycle (a recursive graph binds itself) is closed by the round
    // below: the second round reads the numbers the first one left.
    std::vector<GCGraph *> order;
    std::unordered_set<const GCGraph *> seen;
    const std::function<void(GCGraph *)> visit = [&](GCGraph *graph) {
        if (graph == nullptr || !seen.insert(graph).second) {
            return;
        }
        program.traceGraphs(*graph, [&](GCGraph *next) { visit(next); });
        order.push_back(graph);
    };
    visit(const_cast<GCGraph *>(&root));
    for (GCGraph *graph : collectReachableGraphs(program, const_cast<GCGraph *>(&root))) {
        visit(graph);
    }

    std::unordered_map<const GCGraph *, std::vector<uint32_t>> numbers;
    std::unordered_map<const GCGraph *, std::vector<gc_node_ref_t>> sequences;
    std::vector<CanonicalTie> ties;
    for (size_t round = 0; round < 2; ++round) {
        ties.clear();
        for (GCGraph *graph : order) {
            CanonicalWalk walk(program, *graph, &numbers, &ties);
            std::vector<gc_node_ref_t> sequence = walk.run();
            std::vector<uint32_t> places(graph->nodeCount(), kNoCanonicalNumber);
            for (size_t offset = 0; offset < sequence.size(); ++offset) {
                if (sequence[offset] < places.size()) {
                    places[sequence[offset]] = static_cast<uint32_t>(offset);
                }
            }
            numbers[graph]   = std::move(places);
            sequences[graph] = std::move(sequence);
        }
    }
    for (GCGraph *graph : order) {
        GraphNumbers entry;
        entry.order   = std::move(sequences[graph]);
        entry.numbers = std::move(numbers[graph]);
        graphs_[graph] = std::move(entry);
    }
    ties_ = std::move(ties);
    std::stable_sort(ties_.begin(), ties_.end(), [](const CanonicalTie &a, const CanonicalTie &b) {
        return a.width > b.width;
    });
}

namespace {

const std::vector<gc_node_ref_t> &emptyOrder() {
    static const std::vector<gc_node_ref_t> empty;
    return empty;
}

const std::vector<uint32_t> &emptyNumbers() {
    static const std::vector<uint32_t> empty;
    return empty;
}

} // namespace

const std::vector<gc_node_ref_t> &CanonicalProgram::order(const GCGraph &graph) const {
    const auto at = graphs_.find(&graph);
    return at == graphs_.end() ? emptyOrder() : at->second.order;
}

const std::vector<uint32_t> &CanonicalProgram::numbers(const GCGraph &graph) const {
    const auto at = graphs_.find(&graph);
    return at == graphs_.end() ? emptyNumbers() : at->second.numbers;
}

std::string CanonicalProgram::tiesText() const {
    size_t wide       = 0; // groups wider than one node
    size_t widest     = 0;
    size_t pending    = 0; // groups an equal-key part of which was too wide to enumerate
    size_t unresolved = 0; // groups holding a part no criterion of the graph separated
    std::ostringstream os;
    for (const CanonicalTie &tie : ties_) {
        if (tie.width <= 1) {
            continue;
        }
        ++wide;
        widest = std::max(widest, tie.width);
        if (!tie.enumerated) {
            ++pending;
        }
        if (tie.unresolvedWidth > 1) {
            ++unresolved;
        }
        os << "tie width " << tie.width << " (equal keys " << tie.equalKeyWidth << ", "
           << (tie.enumerated ? "enumerated" : "not enumerated");
        if (tie.unresolvedWidth > 1) {
            os << ", unresolved " << tie.unresolvedWidth;
        }
        os << ") in graph "
           << (tie.graph != nullptr ? std::string(tie.graph->name()) : std::string("<none>"))
           << '\n';
    }
    std::ostringstream summary;
    summary << "canonical ties: " << wide << " group(s) wider than one node, widest " << widest
            << ", over the limit " << pending << " (kMaxTieWidth " << kMaxTieWidth << ")\n";
    if (pending != 0) {
        summary << "  an equal-key part wider than the limit is not enumerated: it is cut by each "
                   "node's own fragment, then by the shape the node has in the graph (its class "
                   "among the nodes it reads and the nodes that read it, and the port of the graph "
                   "it binds that a captured value is passed to)\n";
    }
    if (unresolved != 0) {
        summary << "  " << unresolved << " group(s) hold a part no criterion of the graph "
                   "separated, wider than the enumeration limit allows: those nodes are not a "
                   "compared minimum either, and they are placed in the order the walk met them -- "
                   "the order the graph was built in, which the numbering is not supposed to read "
                   "(design principles 9.5)\n";
    }
    return summary.str() + os.str();
}

std::vector<gc_node_ref_t> canonicalNodeOrder(const Program &program, const GCGraph &graph) {
    const CanonicalProgram numbered(program, graph);
    const std::vector<gc_node_ref_t> &order = numbered.order(graph);
    return std::vector<gc_node_ref_t>(order.begin(), order.end());
}

std::vector<uint32_t> canonicalNodeNumbers(const Program &program, const GCGraph &graph) {
    const CanonicalProgram numbered(program, graph);
    const std::vector<uint32_t> &numbers = numbered.numbers(graph);
    return std::vector<uint32_t>(numbers.begin(), numbers.end());
}

} // namespace camel::runtime