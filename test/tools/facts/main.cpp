// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * camel-facts: the fact framework's own check (camel/facts/facts.h).
 *
 * `--self-check` walks the path the header declares and nothing in the tree walked before: the
 * rules a kind is registered under, a store that computes a fact once and answers the next asker
 * from what it kept, a stamp that no longer matches dropping the entry instead of answering it,
 * the validator comparing a fresh computation with the stored one, the read log, the merge law, the
 * bulk computation and the dump.
 *
 * The graph a fact is anchored on is a value the framework never dereferences -- it compares the
 * anchor -- so the check stands one in: a view of three nodes written here, and the identity of
 * "the graph" is that object's address. This check keeps its meaning when a real graph is the
 * store's first owner.
 */

#include "camel/facts/facts.h"
#include "camel/graph/graph.h"

#include "graph/graph_view.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace facts = camel::facts;
namespace g     = camel::graph;

int failures = 0;

/// The sentences that failed, repeated at the end: a runner that shows the last lines of the output
/// would otherwise drop the line that says what went wrong.
std::vector<std::string> failedChecks;

void check(bool ok, std::string_view what) {
    if (ok) {
        std::cout << "[PASS] " << what << "\n";
    } else {
        ++failures;
        failedChecks.emplace_back(what);
        std::cout << "[FAIL] " << what << "\n";
    }
}

/// Whether `body` throws std::invalid_argument whose message names `fragment`.
bool refused(std::string_view fragment, const std::function<void()> &body) {
    try {
        body();
    } catch (const std::invalid_argument &error) {
        return std::string(error.what()).find(fragment) != std::string::npos;
    } catch (const std::exception &) {
        return false;
    }
    return false;
}

/// A graph of three nodes answered through the view: a port, the exit and an operator. It stores
/// nothing derived, so every query is the payload's answer and the check knows what a fact should
/// have read. It has no static area and no label of its own: the check asks about facts, and a
/// reader that asked this graph's name or its slots would be told what it does not have.
class ThreeNodeView final : public g::GraphView {
  public:
    camel::core::type::FunctionType *funcType() const override { return nullptr; }
    std::vector<g::NodeRef> ports(g::PortGroup) const override { return {}; }
    g::NodeRef exit() const override { return 1; }
    std::string_view name() const override { return {}; }

    std::uint32_t nodeSlotCount() const override { return 3; }
    bool contains(g::NodeRef ref) const override { return ref < 3; }
    g::NodeKind kindOf(g::NodeRef ref) const override {
        switch (ref) {
        case 0:
            return g::NodeKind::Port;
        case 1:
            return g::NodeKind::Data;
        default:
            return g::NodeKind::Oper;
        }
    }
    const camel::core::type::Type *typeOf(g::NodeRef) const override { return nullptr; }
    std::optional<::slot_t> constantOf(g::NodeRef) const override { return std::nullopt; }
    std::optional<std::uint32_t> staticIndexOf(g::NodeRef) const override { return std::nullopt; }
    std::uint32_t staticSlotCount() const override { return 0; }
    std::uint64_t originOf(g::NodeRef) const override { return 0; }

    std::span<const g::NodeRef> inputs(g::NodeRef, g::PortGroup) const override { return {}; }
    std::span<const g::NodeRef> users(g::NodeRef, g::PortGroup) const override { return {}; }
    std::vector<g::NodeRef> predecessors(g::NodeRef, g::EdgeKind) const override { return {}; }
    std::vector<g::NodeRef> successors(g::NodeRef, g::EdgeKind) const override { return {}; }
    std::span<const g::Edge> edges(g::EdgeKind) const override { return {}; }

    // The tag set and the two permission columns: this graph carries no tag on any node, so it
    // answers the empty set and no permission -- the same answer its empty static area and empty
    // name give, and the one a reader that asked would be entitled to.
    camel::core::attr::AttrSetId attrsOf(g::NodeRef) const override {
        return camel::core::attr::kNoAttrs;
    }
    camel::core::sem::PermissionMask permissionsOf(g::NodeRef) const override {
        return camel::core::sem::kNoPermissions;
    }
    camel::core::sem::PermissionMask effectivePermissionsOf(
        g::NodeRef, const camel::core::sem::NumericPolicy &) const override {
        return camel::core::sem::kNoPermissions;
    }

    std::uint32_t regionCount() const override { return 1; }
    g::RegionRecord region(g::RegionId) const override { return {}; }
    std::span<const g::NodeRef> regionParameters(g::RegionId) const override { return {}; }
    std::vector<g::NodeRef> regionNodes(g::RegionId) const override { return {}; }
    g::RegionId regionOf(g::NodeRef) const override { return 0; }
    bool isAncestorOf(g::RegionId ancestor, g::RegionId region) const override {
        return ancestor == region;
    }
    std::vector<g::RegionId> regionsOwnedBy(g::NodeRef) const override { return {}; }

    g::CalleeRef calleeOf(g::NodeRef) const override { return {}; }
    camel::core::OperId operatorOf(g::NodeRef ref) const override {
        return ref == 2 ? camel::core::OperId(7) : camel::core::kNoOper;
    }
    g::LoopForm loopFormOf(g::NodeRef) const override { return {}; }
};

/// The one graph the check has: an object whose address is the identity a fact is anchored on.
class CheckGraph final : public g::Graph {
  public:
    explicit CheckGraph(const g::GraphView &view) : view_(view) {}
    const g::GraphView &view() const override { return view_; }

  private:
    const g::GraphView &view_;
};

/// The version of each graph's content, as the owner of the graph would report it.
std::unordered_map<const g::Graph *, std::uint64_t> versions;

void bumpVersion(const g::Graph &graph) { ++versions[&graph]; }

/// One anchor as the span a store member takes: the check asks about one node at a time.
std::span<const facts::Anchor> asSpan(const facts::Anchor &anchor) { return {&anchor, 1}; }

/// What `check.node_tag` is worth at a node: the node it is about and a mark the computation put
/// beside it. The mark is a constant, so two computations of the same node agree -- a value that
/// changed on every computation would make the validator unable to say anything.
struct NodeTag {
    std::uint32_t node = 0;
    std::uint32_t mark = 0;
    bool operator==(const NodeTag &) const = default;
};

/// What `check.counter` is worth: a number the check moves between the store's answer and the
/// validator's fresh computation, so the validator has one value that agrees and one that does not.
std::uint64_t counter = 1;

/// What `check.set` is worth at a set of nodes: a sorted list of them.
struct NodeSet {
    std::vector<std::uint32_t> nodes;
    bool operator==(const NodeSet &) const = default;
};

int runSelfCheck() {
    // ---- The registry ------------------------------------------------------------------------
    const facts::KindId nodeTag = facts::registerKind<NodeTag>(facts::Kind<NodeTag>{
        .key    = "check.node_tag",
        .owner  = "check",
        .scope  = facts::Scope::Node,
        .compute =
            [](std::span<const facts::Anchor> anchors, const facts::GraphAccess &, facts::ReadLog &log) {
                const facts::Anchor &anchor = anchors[0];
                log.node(anchor.node);
                return std::optional<NodeTag>(NodeTag{anchor.node, 7});
            },
        .print = [](const NodeTag &tag) {
            return std::string("node ") + std::to_string(tag.node);
        },
    });
    check(facts::findKind("check.node_tag") == nodeTag, "a kind registers and is found again by its key");

    const facts::KindId again = facts::registerKind<NodeTag>(facts::Kind<NodeTag>{
        .key    = "check.node_tag",
        .owner  = "check",
        .scope  = facts::Scope::Node,
        .compute =
            [](std::span<const facts::Anchor> anchors, const facts::GraphAccess &, facts::ReadLog &) {
                return std::optional<NodeTag>(NodeTag{anchors[0].node, 7});
            },
        .print = [](const NodeTag &tag) {
            return std::string("node ") + std::to_string(tag.node);
        },
    });
    check(
        again == nodeTag,
        "the same key registered again is the same kind, and answers the id it already had");

    check(
        refused("check.node_tag", [&] {
            (void)facts::registerKind<NodeTag>(facts::Kind<NodeTag>{
                .key   = "check.node_tag",
                .owner = "check",
                .scope = facts::Scope::Graph,
                .print = [](const NodeTag &tag) {
                    return std::string("node ") + std::to_string(tag.node);
                },
            });
        }),
        "a kind registered again as another kind is refused, naming its key");

    check(
        refused("'<owner>.<name>'", [] {
            (void)facts::registerKind<NodeTag>(facts::Kind<NodeTag>{
                .key   = "node_tag",
                .owner = "check",
                .print = [](const NodeTag &) { return std::string(); },
            });
        }),
        "a kind whose key is not '<owner>.<name>' is refused");

    check(
        refused("printed", [] {
            (void)facts::registerKind<NodeTag>(facts::Kind<NodeTag>{
                .key   = "check.unprinted",
                .owner = "check",
            });
        }),
        "a kind that does not say how its values are printed is refused");

    check(
        refused("states no merge", [] {
            (void)facts::registerKind<NodeSet>(facts::Kind<NodeSet>{
                .key    = "check.content",
                .owner  = "check",
                .cls    = facts::Class::Content,
                .scope  = facts::Scope::Graph,
                .compute =
                    [](std::span<const facts::Anchor>, const facts::GraphAccess &, facts::ReadLog &) {
                        return std::optional<NodeSet>(NodeSet{{0, 1, 2}});
                    },
                .merge =
                    [](const NodeSet &left, const NodeSet &right) {
                        NodeSet both = left;
                        both.nodes.insert(both.nodes.end(), right.nodes.begin(), right.nodes.end());
                        return std::optional<NodeSet>(both);
                    },
                .print = [](const NodeSet &set) { return std::to_string(set.nodes.size()); },
            });
        }),
        "a Content kind that states a merge is refused: it is read, never stored");

    check(
        refused("is the graph's content", [] {
            (void)facts::registerKind<NodeSet>(facts::Kind<NodeSet>{
                .key   = "check.reads_nothing",
                .owner = "check",
                .cls   = facts::Class::Content,
                .scope = facts::Scope::Graph,
                .print = [](const NodeSet &set) { return std::to_string(set.nodes.size()); },
            });
        }),
        "a Content kind that does not read the graph is refused");

    // The kinds the store is exercised with.
    const facts::KindId counterKind = facts::registerKind<std::uint64_t>(facts::Kind<std::uint64_t>{
        .key    = "check.counter",
        .owner  = "check",
        .scope  = facts::Scope::Node,
        .compute =
            [](std::span<const facts::Anchor>, const facts::GraphAccess &, facts::ReadLog &) {
                return std::optional<std::uint64_t>(counter);
            },
        .print = [](std::uint64_t value) { return std::to_string(value); },
    });
    const facts::KindId setKind = facts::registerKind<NodeSet>(facts::Kind<NodeSet>{
        .key      = "check.set",
        .owner    = "check",
        .scope    = facts::Scope::CrossNode,
        .monotone = true,
        .merge =
            [](const NodeSet &left, const NodeSet &right) {
                NodeSet both = left;
                both.nodes.insert(both.nodes.end(), right.nodes.begin(), right.nodes.end());
                std::sort(both.nodes.begin(), both.nodes.end());
                both.nodes.erase(std::unique(both.nodes.begin(), both.nodes.end()), both.nodes.end());
                return std::optional<NodeSet>(both);
            },
        .print = [](const NodeSet &set) { return std::to_string(set.nodes.size()); },
    });
    const facts::KindId contentKind = facts::registerKind<NodeSet>(facts::Kind<NodeSet>{
        .key    = "check.nodes",
        .owner  = "check",
        .cls    = facts::Class::Content,
        .scope  = facts::Scope::Graph,
        .compute =
            [](std::span<const facts::Anchor> anchors, const facts::GraphAccess &graphs, facts::ReadLog &) {
                NodeSet nodes;
                if (const g::GraphView *view = graphs.view ? graphs.view(*anchors[0].graph) : nullptr) {
                    for (g::NodeRef ref : view->nodes()) {
                        nodes.nodes.push_back(ref);
                    }
                }
                return std::optional<NodeSet>(nodes);
            },
        .print = [](const NodeSet &set) { return std::to_string(set.nodes.size()); },
    });
    const facts::KindId targetKind = facts::registerKind<std::uint64_t>(facts::Kind<std::uint64_t>{
        .key   = "check.layout",
        .owner = "check",
        .cls   = facts::Class::Target,
        .scope = facts::Scope::Node,
        .print = [](std::uint64_t value) { return std::to_string(value); },
    });

    // ---- The store ---------------------------------------------------------------------------
    ThreeNodeView view;
    CheckGraph graph(view);
    const facts::GraphAccess access{
        .view    = [](const g::Graph &each) { return &each.view(); },
        .version = [](const g::Graph &each) {
            const auto it = versions.find(&each);
            return it == versions.end() ? std::uint64_t{0} : it->second;
        },
    };
    facts::Store store(access);
    const facts::Anchor node2{&graph, 2};

    const auto first = store.obtain<NodeTag>(nodeTag, node2);
    check(
        first != nullptr && first->node == 2 && first->mark == 7,
        "the first ask computes the fact and answers it");
    check(store.counts().computed == 1, "the first ask is one computation");

    const auto second = store.obtain<NodeTag>(nodeTag, node2);
    check(
        second == first && store.counts().computed == 1 && store.counts().hits == 1,
        "the second ask answers from what the store kept, and computes nothing");

    bumpVersion(graph);
    check(
        store.probe(nodeTag, asSpan(node2)) == facts::Status::Stale,
        "a version that moved is the answer 'stale', not the stored fact");
    const auto recomputed = store.obtain<NodeTag>(nodeTag, node2);
    check(
        recomputed != nullptr && store.counts().computed == 2,
        "a version that moved drops the entry instead of answering it");

    // ---- The validator -----------------------------------------------------------------------
    counter = 1;
    const facts::Anchor node0{&graph, 0};
    check(
        store.obtain<std::uint64_t>(counterKind, node0) != nullptr,
        "a counter fact is computed and kept");
    check(
        store.verify(counterKind, asSpan(node0)) == std::optional<bool>(true),
        "the validator says a stored value that agrees with a fresh computation is the fact");
    counter = 2;
    check(
        store.verify(counterKind, asSpan(node0)) == std::optional<bool>(false),
        "the validator says a stored value that disagrees with a fresh computation is not the fact");
    counter = 1;
    check(
        store.verify(setKind, asSpan(node0)) == std::nullopt,
        "the validator says nothing about a kind that has no computation");

    // ---- Content, target and the anchors -----------------------------------------------------
    const size_t beforeContent = store.size();
    const auto content = store.get<NodeSet>(contentKind, facts::Anchor{&graph, facts::kWholeGraph});
    const auto again2  = store.get<NodeSet>(contentKind, facts::Anchor{&graph, facts::kWholeGraph});
    check(
        content != nullptr && content->nodes.size() == 3 && again2 != nullptr &&
            store.size() == beforeContent,
        "a Content kind is read from the view and stored nowhere");
    check(
        store.probe(contentKind, asSpan(facts::Anchor{&graph, facts::kWholeGraph})) ==
            facts::Status::Valid,
        "a Content kind is never stale: there is nothing stored to be stale");

    check(
        refused("hash of its target", [&] {
            store.put<std::uint64_t>(targetKind, node2, 42);
        }),
        "a Target kind without a target hash is refused");
    store.put<std::uint64_t>(targetKind, node2, 42, 7);
    check(
        store.get<std::uint64_t>(targetKind, node2, 7) != nullptr &&
            store.get<std::uint64_t>(targetKind, node2, 9) == nullptr,
        "a Target kind is stored under the hash of its target, and nowhere else");

    check(
        refused("not a node of its graph", [&] {
            (void)store.probe(nodeTag, asSpan(facts::Anchor{&graph, 9}));
        }),
        "a fact about a node the graph does not have is refused");

    // ---- The read log ------------------------------------------------------------------------
    facts::ReadLog log;
    log.node(5);
    log.node(2);
    log.node(5);
    log.region(1);
    log.region(1);
    log.record(camel::ops::OperId(3));
    log.name(graph, "f", &graph);
    log.name(graph, "f", &graph);
    const facts::Reads reads = log.take();
    check(
        reads.nodes.size() == 2 && reads.nodes[0] == 2 && reads.nodes[1] == 5 &&
            reads.regions.size() == 1 && reads.records.size() == 1 && reads.names.size() == 1 &&
            reads.declarations(),
        "the read log sorts and de-duplicates what a computation reported");
    check(log.peek().nodes.empty(), "taking the log empties it");

    // ---- The merge law -----------------------------------------------------------------------
    const facts::KindInfo &setInfo = facts::kindInfo(setKind);
    const NodeSet leftPart{{1, 2}};
    const NodeSet rightPart{{2, 3}};
    const std::shared_ptr<const void> merged = setInfo.merge(&leftPart, &rightPart);
    const auto *mergedSet = static_cast<const NodeSet *>(merged.get());
    check(
        mergedSet != nullptr && mergedSet->nodes.size() == 3 && setInfo.merges && setInfo.monotone,
        "a kind that states a merge law merges two parts into the value of their union");

    // ---- The bulk computation and the dump ---------------------------------------------------
    facts::Store bulk(access);
    facts::computeAll(bulk, graph, view);
    // The store holds a fact at every anchor a kind names and at no other place. The total is not
    // pinned: it moves when a kind is registered (a graph-scoped kind adds the graph's one anchor,
    // and a kind whose computation answers nothing about a graph that is not a frozen one adds
    // none), so a total that has to be edited for each new kind stops being a reading of this
    // rule. What is pinned is the rule itself: each of the three nodes has a fact, and nothing is
    // anchored on a node this graph does not have.
    std::array<bool, 3> nodeFilled{false, false, false};
    bool strays = false;
    bulk.forEach([&](const facts::StoredFact &fact) {
        for (const facts::Anchor &anchor : fact.anchors) {
            if (anchor.graph != &graph) {
                strays = true;
            } else if (anchor.node == facts::kWholeGraph) {
                continue;
            } else if (anchor.node < nodeFilled.size()) {
                nodeFilled[anchor.node] = true;
            } else {
                strays = true;
            }
        }
    });
    check(
        !strays && nodeFilled[0] && nodeFilled[1] && nodeFilled[2],
        "computeAll fills the store at every anchor the kind names, and nowhere else");

    std::ostringstream printed;
    facts::dump(bulk, graph, view, printed);
    const std::string text = printed.str();
    size_t lines    = 0;
    bool sawDerived = false;
    bool sawContent = false;
    for (size_t at = 0; at < text.size();) {
        const size_t end = text.find('\n', at);
        const std::string_view line(
            text.data() + at, (end == std::string::npos ? text.size() : end) - at);
        at = end == std::string::npos ? text.size() : end + 1;
        if (line.empty()) {
            continue;
        }
        ++lines;
        sawDerived = sawDerived || line.find("\"class\":\"derived\"") != std::string_view::npos;
        sawContent = sawContent || line.find("\"class\":\"content\"") != std::string_view::npos;
    }
    check(
        lines >= 2 && sawDerived && sawContent,
        "the dump prints one FACT line per stored fact, naming the kind and its class");

    bulk.invalidateGraph(&graph);
    check(bulk.size() == 0, "dropping a graph's facts drops every fact anchored on it");

    store.invalidate(nodeTag, asSpan(node2));
    check(store.size() > 0, "dropping one fact leaves the others");

    if (failures == 0) {
        std::cout << "facts self-check: passed\n";
        return 0;
    }
    std::cout << "facts self-check: " << failures << " failed\n";
    for (const std::string &what : failedChecks) {
        std::cout << "  failed: " << what << "\n";
    }
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--self-check") {
            try {
                return runSelfCheck();
            } catch (const std::exception &error) {
                std::cout << "facts self-check: threw: " << error.what() << "\n";
                return 1;
            }
        }
    }
    std::cout << "usage: camel-facts --self-check\n";
    return 2;
}