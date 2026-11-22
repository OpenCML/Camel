// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The fact framework: everything derived from a graph, one store, one way to ask (core
 * abstractions section 4; docs/design/op-semantics section 10).
 *
 * A fact is a declared function of a graph: a node's shape, a graph's effect summary, its
 * execution plan, its canonical form, an index instance, a loop's domain. The framework owns all
 * of them: it computes a fact when it is first asked for, keeps it, answers the next asker from
 * the store, and drops it when what it was computed from changed. A reader -- the graph builder,
 * a commit, a pass, a back end -- asks the framework and never computes a fact for itself, and
 * never keeps a cache of its own beside the store.
 *
 * The anchor is the view. A fact is about one or more (graph view, node) pairs, where the view is
 * the read-only interface to a graph the graph lane gives (core abstractions sections 1 and 2):
 * a frozen graph and an editing session both give it, so a fact can be asked for while a program
 * is being built as well as about the graph it was built into. A fact about a frozen view lives
 * as long as the program that holds it and is answered again and again; a fact about a session's
 * view dies with the session, because what it was computed from does not outlive it.
 *
 * A fact says what it read, and that record is what invalidates it. A computation reports the
 * nodes and regions it read, the names it resolved and the graphs those names were bound to, and
 * the operator records it asked; the stamp of the stored fact holds that, so a fact is recomputed
 * when one of those changed and is kept when the graph changed elsewhere. A computation that
 * cannot report what it read declares the conservative dependence instead (`Dependence::Anchors`:
 * any change in the graph it is anchored on, and in the graphs that graph reaches, makes it
 * stale) -- today's rule, which is what a fact computed by code that was written before this
 * framework keeps. The precise rule is the reported one, and a kind that reports is the shape a
 * new fact is written in.
 *
 * Merging and monotonicity. A kind that is an analysis of parts states how two of its values
 * combine (`Kind::merge`): the value of a union is the merge of the values of the parts, and the
 * value of nothing is the identity. A kind that is a whole-graph answer with no such law (an
 * execution plan, a canonical order) states no merge and is only cached by version. A kind may
 * also state that it is monotone: adding nodes or edges never makes a stored value false. Both
 * are declarations of the kind, and the test tool that enumerates rule and fact declarations
 * checks them.
 *
 * The store belongs to nothing that is dumped: facts are process-local, they are not copied into
 * a graph and they change no document a program prints.
 *
 * What is registered today and computed elsewhere: the effect summary, the use index, the
 * execution plan, the control regions and the writing extraction each keep their own cache
 * outside this framework. They are registered here as kinds with the conservative dependence and
 * a compute that calls the code that exists, so that every reader already asks the framework for
 * them and gets one answer from one place; moving the computation itself inside the framework is
 * a separate change, and changes no answer.
 *
 * Thread-safety: every member of the registry and of a store may be called from any thread. A
 * kind is registered while modules load; a value is computed at most once per stamp, even when
 * several threads ask at the same time.
 */

#pragma once

#include "camel/ops/operator_record.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace camel::graph {
/// The read-only view of a graph (core abstractions sections 1 and 2). The framework asks it for
/// two things and nothing else: an identity that names the same graph across versions, and the
/// version of its content. Everything else about a graph a fact reads, it reads through the view.
class GraphView;
} // namespace camel::graph

namespace camel::facts {

/// The number a view gives a node: stable on one version line of the graph, which is all a fact
/// needs, since a fact is about one version (docs/design/graph-store).
using NodeId = std::uint32_t;

/// The anchor that is the graph as a whole rather than one node.
inline constexpr NodeId kWholeGraph = UINT32_MAX;

/// The number a view gives one of its regions (an arm of a branch, a loop's condition or body).
using RegionId = std::uint32_t;
inline constexpr RegionId kNoRegion = UINT32_MAX;

/// What a fact is about: a node of a view, or the view as a whole.
struct Anchor {
    // program-holder-exempt: long-lived-cache -- a store keeps anchors for the life of the
    // program, and a pin here would root every view a fact was ever asked about.
    const graph::GraphView *view = nullptr;
    NodeId node                  = kWholeGraph;
};

/// A name a computation resolved while it ran, and the graph it was bound to at that moment. A
/// name may be bound to a graph again later; the fact is stale when it was, because what it read
/// through the name is then a different graph.
struct NameRead {
    const graph::GraphView *view = nullptr;
    std::string name;
    const graph::GraphView *holds = nullptr;
};

/// What a computation read: the record its stamp is made of.
struct Reads {
    /// The nodes whose own content it read (a node's version, not its graph's).
    std::vector<NodeId> nodes;
    /// The regions whose membership it read.
    std::vector<RegionId> regions;
    /// The names it resolved.
    std::vector<NameRead> names;
    /// The operator records it asked, in the order it asked them.
    std::vector<ops::OperId> records;

    /// It read a record: the stamp holds the operator registry's epoch as well, since a record
    /// completed after the fact was computed may change the answer.
    bool declarations() const { return !records.empty(); }
};

/**
 * What a computation writes its Reads into while it runs. A computation that reports nothing has
 * read nothing, and a fact computed from it is stale as soon as anything it is anchored on
 * changes; that is why a kind states its dependence as well as reporting.
 */
class ReadLog {
  public:
    void node(NodeId id) { reads_.nodes.push_back(id); }
    void region(RegionId id) { reads_.regions.push_back(id); }
    void name(const graph::GraphView &view, std::string_view name, const graph::GraphView *holds) {
        reads_.names.push_back(NameRead{&view, std::string(name), holds});
    }
    void record(ops::OperId id) { reads_.records.push_back(id); }

    /// The record so far, sorted and de-duplicated: two computations that read the same things
    /// produce the same stamp whatever order they read them in.
    Reads take();
    const Reads &peek() const { return reads_; }

  private:
    Reads reads_;
};

/// The class of a kind of fact (core abstractions section 4; docs/design/op-semantics 10.1).
enum class Class : std::uint8_t {
    /// What cannot be recovered from the graph -- the numeric permissions a program granted: the
    /// graph's content. A kind of this class reads it from the view each time; it is never
    /// stored, so it is never stale.
    Content,
    /// What can be derived from the graph, and is stored with a stamp until the stamp no longer
    /// matches.
    Derived,
    /// What depends on the target as well (a layout, a schedule): stored for the process with the
    /// hash of the target configuration in its stamp. Never persisted by the core; a back end
    /// keeps its own compile cache for what outlives the process.
    Target,
};

/// What a fact is about.
enum class Scope : std::uint8_t {
    Node,       // one node
    Graph,      // a view as a whole
    CrossNode,  // several nodes of one view
    CrossGraph, // nodes or views of several views
};

/// How a stored fact is found stale.
enum class Dependence : std::uint8_t {
    /// The computation reports what it read (ReadLog): the fact is stale when a node, region,
    /// name binding or operator record it reported changed.
    Reported,
    /// The conservative rule: any change in the anchored view, or in a view it reaches (a callee,
    /// a nested graph, a function constant's graph, at any depth), makes it stale. What a fact
    /// computed by code written before this framework declares.
    Anchors,
};

using KindId = std::uint32_t;
inline constexpr KindId kNoKind = UINT32_MAX;

/// The type-erased registration of a kind, as the registry keeps it.
struct KindInfo {
    /// `owner.name`: `core.effects`, `tiles.layout`. The prefix before the first dot is reserved
    /// to that owner; the core's is `core`.
    std::string key;
    std::string owner;
    std::uint16_t version = 1;
    std::type_index payload = typeid(void);
    Class cls              = Class::Derived;
    Scope scope            = Scope::Node;
    Dependence dependence  = Dependence::Reported;
    /// Whether the values of this kind merge at all (see Kind::merge).
    bool merges = false;
    /// Whether adding nodes or edges to a graph can make a stored value false.
    bool monotone = false;

    /// How to compute it, which is also the validator: a verify recomputes and compares. Null only
    /// for a kind computed by its owner alone (a module's fact) or for a Content kind (whose
    /// value is read from the view).
    std::function<std::shared_ptr<const void>(std::span<const Anchor>, ReadLog &)> compute;
    /// The law that makes two values one value (see Kind::merge). Null when the kind has none.
    std::function<std::shared_ptr<const void>(const void *left, const void *right)> merge;
    std::function<bool(const void *, const void *)> equal;
    std::function<std::string(const void *)> print;

    /// The anchors in `view` at which the kind has a fact to compute, for `computeAll`: by default
    /// every node (Node scope) or the view (Graph scope); a CrossNode or CrossGraph kind that
    /// wants to be computed in bulk says where.
    std::function<std::vector<std::vector<Anchor>>(const graph::GraphView &)> anchorsIn;
};

/**
 * The declaration of a kind whose values are `T`. Hooks, all but `print` optional:
 *
 *   compute   the value at the anchors, or nullopt when there is none (an operator that declares
 *             no index semantics has no instance), reporting what it read into `log`.
 *   merge     the law that makes two values one: with `left` the value of one part and `right`
 *             the value of another, the value of the union. Stating it is what makes the kind an
 *             analysis rather than a cache (design principles red line 4); a kind with no merge
 *             must state why in its own file.
 *   print     one line of text, which is what `camel inspect --facts` shows. Required.
 * Values are compared with `==` (verify, and the round trip of a module's encoding).
 */
template <typename T> struct Kind {
    std::string key;
    std::string owner;
    std::uint16_t version = 1;
    Class cls              = Class::Derived;
    Scope scope            = Scope::Node;
    Dependence dependence  = Dependence::Reported;
    bool monotone          = false;
    std::function<std::optional<T>(std::span<const Anchor>, ReadLog &)> compute;
    std::function<std::optional<T>(const T &left, const T &right)> merge;
    std::function<std::string(const T &)> print;
    std::function<std::vector<std::vector<Anchor>>(const graph::GraphView &)> anchorsIn;
};

/// Registers a kind (the type-erased form; `registerKind<T>` below builds it). Throws
/// std::invalid_argument naming the key and the broken rule: the key is `owner.name` with the
/// owner before the first dot; the same key registered again must be the same kind (version,
/// payload type, class and scope), in which case the existing id comes back (a module loaded
/// twice); `print` is given; a Content kind has no merge and no compute that stores anything.
KindId registerKind(KindInfo info);

template <typename T> KindId registerKind(Kind<T> kind) {
    KindInfo info{
        .key          = std::move(kind.key),
        .owner        = std::move(kind.owner),
        .version      = kind.version,
        .payload      = typeid(T),
        .cls          = kind.cls,
        .scope        = kind.scope,
        .dependence   = kind.dependence,
        .merges       = bool(kind.merge),
        .monotone     = kind.monotone,
        .anchorsIn    = std::move(kind.anchorsIn),
    };
    if (kind.compute) {
        info.compute = [compute = std::move(kind.compute)](
                           std::span<const Anchor> anchors,
                           ReadLog &log) -> std::shared_ptr<const void> {
            std::optional<T> value = compute(anchors, log);
            return value ? std::make_shared<const T>(std::move(*value)) : nullptr;
        };
        info.equal = [](const void *left, const void *right) {
            return *static_cast<const T *>(left) == *static_cast<const T *>(right);
        };
    }
    if (kind.merge) {
        info.merge = [merge = std::move(kind.merge)](const void *left, const void *right) {
            std::optional<T> value =
                merge(*static_cast<const T *>(left), *static_cast<const T *>(right));
            return value ? std::make_shared<const T>(std::move(*value)) : nullptr;
        };
    }
    if (kind.print) {
        info.print = [print = std::move(kind.print)](const void *value) {
            return print(*static_cast<const T *>(value));
        };
    }
    return registerKind(std::move(info));
}

/// The id of the kind `key`, kNoKind when none is registered.
KindId findKind(std::string_view key);
/// The registration of `kind` (valid until the process exits).
const KindInfo &kindInfo(KindId kind);
/// Every registered kind, in registration order.
std::vector<KindId> registeredKinds();

/// What a read found.
enum class Status : std::uint8_t {
    Absent,      // nothing stored
    Valid,       // stored, and the stamp matches
    Revalidated, // stored, a revision changed, what the computation reported did not: still valid
    Stale,       // stored, but the stamp no longer matches: dropped
};

std::string_view statusName(Status status);

/// Counts since the store was created (or since resetCounts).
struct Counts {
    size_t puts = 0, hits = 0, misses = 0, stale = 0, revalidated = 0, computed = 0;
};

/// One stored fact, as forEach shows it.
struct StoredFact {
    KindId kind = kNoKind;
    /// The anchor of each view the fact is about.
    std::vector<std::pair<const graph::GraphView *, NodeId>> anchors;
    std::shared_ptr<const void> value;
    /// The version of each anchored view, and what the computation reported.
    std::vector<std::pair<const graph::GraphView *, std::uint64_t>> versions;
    Reads reads;
    std::uint64_t epoch   = 0;
    std::uint64_t target  = 0;
};

/**
 * The facts of one program version, or of one editing session, by kind and anchor.
 *
 * There is one store per program and one per session, and the owner creates it: a fact about a
 * frozen view is answered for as long as the program exists, and a fact about a session's view
 * is dropped with the session, because the view it was computed from does not outlive it. Nobody
 * else keeps a cache of a fact; a reader that finds its own cache beside this store has it there
 * for a reason that should be a kind of fact instead.
 */
class Store {
  public:
    /// `persistent` when the views it will be asked about outlive every change of them (a
    /// program's frozen views) and the answers are worth keeping; false for a session, whose
    /// answers are dropped with it in any case.
    explicit Store(bool persistent = true);
    ~Store();
    Store(const Store &)            = delete;
    Store &operator=(const Store &) = delete;

    /// Gets the value at `anchors`, computing and storing it when there is none: the question
    /// every reader asks. Null when the kind's computation has no value there.
    std::shared_ptr<const void>
    obtainValue(KindId kind, std::span<const Anchor> anchors, std::uint64_t target = 0);
    /// Gets a stored value whose stamp matches, without computing anything.
    std::shared_ptr<const void>
    getValue(KindId kind, std::span<const Anchor> anchors, std::uint64_t target = 0);
    /// Stores `value` at `anchors`, replacing what was there. Throws std::invalid_argument for a
    /// Content kind (its value is the view's content and is never stored), a Target kind without
    /// a target hash, a value of another type, or anchors that do not fit the kind's scope.
    void putValue(
        KindId kind,
        std::span<const Anchor> anchors,
        std::shared_ptr<const void> value,
        std::uint64_t target = 0);
    /// What a read at `anchors` finds, discarding a stale entry as `getValue` would, and saying
    /// which of the four answers it was.
    Status probe(KindId kind, std::span<const Anchor> anchors, std::uint64_t target = 0);

    /// Recomputes a Derived fact and compares it with what is stored: nullopt when nothing valid
    /// is stored, the kind has no compute, or the kind is not Derived; otherwise whether the two
    /// are equal. This is the validator of a kind, and what a test asks rather than trusting it.
    std::optional<bool> verify(KindId kind, std::span<const Anchor> anchors);

    /// Drops the fact at `anchors`, every fact anchored on `view`, or everything.
    void invalidate(KindId kind, std::span<const Anchor> anchors);
    void invalidateView(const graph::GraphView *view);
    void clear();

    /// Calls `visitor` for every stored fact, without checking a stamp: what is stored.
    void forEach(const std::function<void(const StoredFact &)> &visitor) const;
    size_t size() const;

    Counts counts() const;
    void resetCounts();

    // ---- Typed access ------------------------------------------------------------------------

    template <typename T>
    void put(KindId kind, std::span<const Anchor> anchors, T value, std::uint64_t target = 0) {
        checkType(kind, typeid(T));
        putValue(kind, anchors, std::make_shared<const T>(std::move(value)), target);
    }
    template <typename T>
    std::shared_ptr<const T>
    get(KindId kind, std::span<const Anchor> anchors, std::uint64_t target = 0) {
        checkType(kind, typeid(T));
        return std::static_pointer_cast<const T>(getValue(kind, anchors, target));
    }
    template <typename T>
    std::shared_ptr<const T>
    obtain(KindId kind, std::span<const Anchor> anchors, std::uint64_t target = 0) {
        checkType(kind, typeid(T));
        return std::static_pointer_cast<const T>(obtainValue(kind, anchors, target));
    }

    // One anchor (a Node- or Graph-scoped fact).
    template <typename T>
    void put(KindId kind, const Anchor &anchor, T value, std::uint64_t target = 0) {
        put<T>(kind, std::span<const Anchor>(&anchor, 1), std::move(value), target);
    }
    template <typename T>
    std::shared_ptr<const T> get(KindId kind, const Anchor &anchor, std::uint64_t target = 0) {
        return get<T>(kind, std::span<const Anchor>(&anchor, 1), target);
    }
    template <typename T>
    std::shared_ptr<const T>
    obtain(KindId kind, const Anchor &anchor, std::uint64_t target = 0) {
        return obtain<T>(kind, std::span<const Anchor>(&anchor, 1), target);
    }

  private:
    /// One stored fact: what it is about, the value, and what makes it stale.
    struct Entry {
        std::vector<std::pair<const graph::GraphView *, NodeId>> anchors;
        std::shared_ptr<const void> value;
        std::vector<std::pair<const graph::GraphView *, std::uint64_t>> versions;
        Reads reads;
        std::uint64_t epoch  = 0;
        std::uint64_t target = 0;
    };
    struct Key {
        KindId kind;
        const graph::GraphView *view;
        NodeId node;

        bool operator==(const Key &) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key &key) const;
    };

    static void checkType(KindId kind, const std::type_info &type);
    Entry *find(KindId kind, std::span<const Anchor> anchors, std::uint64_t target, Status &status);

    bool persistent_;
    mutable std::mutex mutex_;
    std::unordered_map<Key, std::vector<Entry>, KeyHash> entries_;
    Counts counts_;
};

/// Computes and stores, for every view reachable from `root`, the facts of every registered kind
/// that has compute and is not a Content kind, at every anchor the kind names (`anchorsIn`): what
/// a consumer would obtain one by one. Facts already stored and valid are kept.
void computeAll(Store &store, const graph::GraphView &root);

/**
 * `camel inspect --facts`: one line `FACT {json}` per fact about the views reachable from
 * `root`. Fields: "kind", "version", "class" ("content", "derived", "target"), "anchors" (a list
 * of {"graph", "node"}, the node omitted for a view as a whole), "value" (the kind's print),
 * "stamp" ({"versions": [...], "reads": {"nodes", "regions", "names", "records"}, "epoch",
 * "target"}; null for a Content kind). A stale fact is not printed, and is dropped.
 */
void dump(Store &store, const graph::GraphView &root, std::ostream &os);

} // namespace camel::facts