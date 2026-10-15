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
 * Graph facts (docs/design/op-semantics, section 10): what a pass or a back end derived about
 * the program, kept beside it, so that whoever asks again gets the answer without deriving it
 * again, and never gets an answer that no longer holds.
 *
 * Three classes of information (design 10.1):
 *   (i)   GraphContent  what cannot be recovered from the graph (the numeric permissions the
 *                       program granted): it is the graph's content. A kind of this class reads
 *                       it from the graph each time; it is never stored here and never stale.
 *   (ii)  Derived       what can be derived from the program (a node's tensor shape, its index
 *                       instance, a graph's effects): computed on demand, cached here with a
 *                       stamp, recomputed when the stamp no longer matches.
 *   (iii) Target        what depends on the target as well (a CUDA layout, a schedule): may be
 *                       cached here for the process, with the hash of the target configuration
 *                       in its stamp; never persisted by the core (a back end keeps its own
 *                       compile cache for that).
 *
 * Kinds. Every kind of fact is registered once (registerKind): its key `owner.name`, the owner
 * being `core` for the core's kinds and a module's name for a module's (`tiles.layout`: the
 * prefix `tiles.` is reserved to the module tiles), a version, the C++ type of its value, its
 * class, its scope, how its staleness is decided (Dependency) and its hooks: how to compute it
 * (also the validator: verify() recomputes and compares), how to print it, and for classes (i)
 * and (ii) optionally how to encode and decode it for a cache (persist.h).
 *
 * Anchors. A fact is about one or more (graph, node) pairs, node kInvalidNodeRef for a graph as
 * a whole: alias(n1, n7) anchors two nodes of one graph (CrossNode); "the element port of the
 * body graph has the domain of the outer loop" anchors two graphs (CrossGraph). The store
 * indexes a fact by its first anchor; any number of anchors is allowed.
 *
 * Stamps (design 10.3, 10.4). Every stored fact carries a stamp: the revision of every graph it
 * read, the operator declaration epoch (OperatorTable::declarationEpoch) when its kind reads
 * declarations, and the target hash (class iii). A read compares the stamp with the graphs the
 * reader holds, in every build, and a stale fact reads as absent (and is dropped); the caller
 * computes it again. Until graph-model step 5 brings stable GraphIds and revisions:
 *   - a graph's identity across rewrites is GCGraph::lineage() (kept by every commit for the
 *     rewritten copy of a graph; a new graph gets a new one), and
 *   - its revision is GCGraph::serial(), a process-wide serial number: a commit keeps it for a
 *     graph it did not change and gives a new one to a graph it changed. The serial is
 *     transitive: a graph is changed when it was edited or when a graph it refers to (callee,
 *     nested graph, graph of a function constant, at any depth) was changed. A fact reads its
 *     anchors' graphs and, through them, only graphs they refer to; so the anchors' serials are
 *     the revisions of every graph it read, and the stamp holds one serial per anchored graph
 *     (the conservative rule: any change anywhere below makes the fact stale).
 * A kind with Dependency::Cone also stores a hash of what it read (its dependency cone); when a
 * revision changed, the cone is hashed again and, when it is equal, the fact stays valid with a
 * new stamp (the precise rule, design 10.3).
 *
 * The store belongs to the program: Context::facts(). Facts are process-local; they are not
 * copied into the graphs and do not change any dump of the program.
 *
 * Thread-safety: every member of FactStore and the registry may be called from any thread.
 */

#pragma once

#include "camel/runtime/graph.h"

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

namespace camel::runtime::fact {

/// The class of a kind of fact (design 10.1; see the file comment).
enum class FactClass : uint8_t { GraphContent, Derived, Target };

/// What a fact is about.
enum class Scope : uint8_t {
    Node,       // one node
    Graph,      // a graph as a whole
    CrossNode,  // several nodes of one graph
    CrossGraph, // nodes or graphs of several graphs
};

/// How a stored fact is found stale (design 10.3).
enum class Dependency : uint8_t {
    Anchors, // stale when the revision of an anchored graph changed (the conservative rule)
    Cone,    // as Anchors, but rehash the dependency cone first: equal means still valid
};

using KindId = uint32_t;
inline constexpr KindId kNoKind = UINT32_MAX;

/// What a fact is about: a node of a graph, or the graph as a whole (node == kInvalidNodeRef).
struct Anchor {
    const GCGraph *graph = nullptr;
    gc_node_ref_t node   = kInvalidNodeRef;
};

/// The value of a fact, type-erased; the kind records the type (KindInfo::payload).
using Value = std::shared_ptr<const void>;

/// A stamp (design 10.3): when it was computed, in the terms a reader can compare.
struct Stamp {
    /// (lineage, serial) of every graph the fact read, one per distinct anchored graph.
    std::vector<std::pair<uint64_t, uint64_t>> graphs;
    /// OperatorTable::declarationEpoch() when computed; 0 for a kind that reads no declaration.
    uint64_t declarationEpoch = 0;
    /// The hash of the target configuration (class iii); 0 for target-independent kinds.
    uint64_t targetHash = 0;
    /// The hash of the dependency cone (Dependency::Cone); 0 otherwise.
    uint64_t cone = 0;
};

/// A registered kind, type-erased. Built from a KindSpec<T> by registerKind.
struct KindInfo {
    std::string key; // "core.shape", "tiles.layout"
    std::string owner;
    uint16_t version = 1;
    std::type_index payload = typeid(void);
    FactClass cls           = FactClass::Derived;
    Scope scope             = Scope::Node;
    Dependency dependency   = Dependency::Anchors;
    bool readsDeclarations  = true;
    std::function<Value(std::span<const Anchor>)> compute;           // nullptr value: no fact
    std::function<bool(const void *, const void *)> equal;            // required with compute
    std::function<std::string(const void *)> print;                   // required
    std::function<uint64_t(std::span<const Anchor>)> cone;            // Dependency::Cone
    std::function<std::string(const void *)> encode;                  // optional, (i) and (ii)
    std::function<Value(std::string_view)> decode;                    // with encode
    /// The anchors in `graph` at which the kind has a fact to compute (computeFacts): by
    /// default every node (Node) or the graph (Graph); CrossNode and CrossGraph kinds that
    /// want to be computed in bulk say where.
    std::function<std::vector<std::vector<Anchor>>(const GCGraph &)> anchorsIn;
};

/**
 * The registration of a kind whose values are `T`. Hooks:
 *   compute  the value at the anchors, or nullopt when there is none (an OPER whose operator
 *            declares no index semantics has no instance). Required for GraphContent (the value
 *            is read from the graph) and for verify(); a kind computed only by its owner (a
 *            module's analysis) may leave it empty.
 *   print    one line of text (camel inspect --facts). Required.
 *   cone     Dependency::Cone: a hash of everything compute reads at the anchors.
 *   encode / decode   a portable text form of the value (persist.h): GraphContent and Derived
 *            only. decode returns nullopt for a text it does not accept.
 * Values are compared with `==` (verify(), the round trip of encode and decode).
 */
template <typename T> struct KindSpec {
    std::string key;
    std::string owner;
    uint16_t version       = 1;
    FactClass cls          = FactClass::Derived;
    Scope scope            = Scope::Node;
    Dependency dependency  = Dependency::Anchors;
    bool readsDeclarations = true;
    std::function<std::optional<T>(std::span<const Anchor>)> compute;
    std::function<std::string(const T &)> print;
    std::function<uint64_t(std::span<const Anchor>)> cone;
    std::function<std::string(const T &)> encode;
    std::function<std::optional<T>(std::string_view)> decode;
    std::function<std::vector<std::vector<Anchor>>(const GCGraph &)> anchorsIn;
};

/// Registers a kind, type-erased (see registerKind<T>). Throws std::invalid_argument naming the
/// key and the rule it breaks: the key is `owner.name` with a non-empty name; the owner `core`
/// is the core's; the same key registered again must be the same kind (same version, payload
/// type, class and scope): it then returns the existing id (a module loaded twice); a
/// GraphContent kind has compute; a Target kind has no encode/decode (the core never persists
/// it); encode and decode come together; Dependency::Cone has cone; print is given.
KindId registerKind(KindInfo info);

template <typename T> KindId registerKind(KindSpec<T> spec) {
    KindInfo info{
        .key               = std::move(spec.key),
        .owner             = std::move(spec.owner),
        .version           = spec.version,
        .payload           = typeid(T),
        .cls               = spec.cls,
        .scope             = spec.scope,
        .dependency        = spec.dependency,
        .readsDeclarations = spec.readsDeclarations,
    };
    if (spec.compute) {
        info.compute = [compute = std::move(spec.compute)](std::span<const Anchor> anchors) -> Value {
            std::optional<T> value = compute(anchors);
            return value ? std::make_shared<const T>(std::move(*value)) : nullptr;
        };
        info.equal = [](const void *a, const void *b) {
            return *static_cast<const T *>(a) == *static_cast<const T *>(b);
        };
    }
    if (spec.print) {
        info.print = [print = std::move(spec.print)](const void *value) {
            return print(*static_cast<const T *>(value));
        };
    }
    info.cone      = std::move(spec.cone);
    info.anchorsIn = std::move(spec.anchorsIn);
    if (spec.encode) {
        info.encode = [encode = std::move(spec.encode)](const void *value) {
            return encode(*static_cast<const T *>(value));
        };
    }
    if (spec.decode) {
        info.decode = [decode = std::move(spec.decode)](std::string_view text) -> Value {
            std::optional<T> value = decode(text);
            return value ? std::make_shared<const T>(std::move(*value)) : nullptr;
        };
        if (!info.equal) {
            info.equal = [](const void *a, const void *b) {
                return *static_cast<const T *>(a) == *static_cast<const T *>(b);
            };
        }
    }
    return registerKind(std::move(info));
}

/// The id of the kind `key`, kNoKind when none is registered.
KindId findKind(std::string_view key);
/// The registration of `kind` (valid until the process exits).
const KindInfo &kindInfo(KindId kind);
/// Every registered kind, in registration order.
std::vector<KindId> registeredKinds();

/// What a read found (FactStore::probe).
enum class Status : uint8_t {
    Absent,      // nothing stored
    Valid,       // stored, and the stamp matches
    Revalidated, // stored, a revision changed, the dependency cone did not: valid, restamped
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
    /// (lineage, node) of each anchor.
    std::vector<std::pair<uint64_t, gc_node_ref_t>> anchors;
    Value value;
    Stamp stamp;
};

class FactStore {
  public:
    FactStore();
    ~FactStore();
    FactStore(const FactStore &)            = delete;
    FactStore &operator=(const FactStore &) = delete;

    /// Stores `value` (of the kind's payload type) at `anchors`, stamped from their graphs now,
    /// replacing what was there. Throws std::invalid_argument for a GraphContent kind (its
    /// value is the graph's content), a Target kind without a target hash, a value of another
    /// type, or anchors that do not fit the kind's scope.
    void putValue(KindId kind, std::span<const Anchor> anchors, Value value, uint64_t targetHash = 0);

    /// The value at `anchors` when one is stored and its stamp matches; nullptr otherwise (a
    /// stale fact is dropped). A GraphContent kind is read from the graph. `targetHash` must
    /// equal the stored one for a Target kind.
    Value getValue(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash = 0);

    /// getValue, else the kind's compute, stored (nullptr when compute has no value or the kind
    /// has no compute).
    Value obtainValue(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash = 0);

    /// What a read at `anchors` finds, without computing anything (a stale fact is dropped, a
    /// revalidated one restamped, as getValue would).
    Status probe(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash = 0);

    /// Recomputes the Derived fact stored at `anchors` and compares it with the stored value
    /// (design 10.5, the validator): nullopt when nothing valid is stored, the kind has no
    /// compute or is not Derived, else whether they are equal.
    std::optional<bool> verify(KindId kind, std::span<const Anchor> anchors);

    /// Drops the fact at `anchors`; drops every fact anchored (first anchor) on `graph`'s
    /// lineage; drops everything.
    void invalidate(KindId kind, std::span<const Anchor> anchors);
    void invalidateGraph(const GCGraph *graph);
    void clear();

    /// Calls `visitor` for every stored fact (no stamp is checked: what is stored).
    void forEach(const std::function<void(const StoredFact &)> &visitor) const;
    size_t size() const;

    Counts counts() const;
    void resetCounts();

    // ---- Typed access -------------------------------------------------------------------

    template <typename T>
    void put(KindId kind, std::span<const Anchor> anchors, T value, uint64_t targetHash = 0) {
        checkType(kind, typeid(T));
        putValue(kind, anchors, std::make_shared<const T>(std::move(value)), targetHash);
    }
    template <typename T>
    std::shared_ptr<const T> get(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash = 0) {
        checkType(kind, typeid(T));
        return std::static_pointer_cast<const T>(getValue(kind, anchors, targetHash));
    }
    template <typename T>
    std::shared_ptr<const T>
    obtain(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash = 0) {
        checkType(kind, typeid(T));
        return std::static_pointer_cast<const T>(obtainValue(kind, anchors, targetHash));
    }

    // One anchor (a Node or Graph fact).
    template <typename T>
    void put(KindId kind, const Anchor &anchor, T value, uint64_t targetHash = 0) {
        put<T>(kind, std::span<const Anchor>(&anchor, 1), std::move(value), targetHash);
    }
    template <typename T>
    std::shared_ptr<const T> get(KindId kind, const Anchor &anchor, uint64_t targetHash = 0) {
        return get<T>(kind, std::span<const Anchor>(&anchor, 1), targetHash);
    }
    template <typename T>
    std::shared_ptr<const T> obtain(KindId kind, const Anchor &anchor, uint64_t targetHash = 0) {
        return obtain<T>(kind, std::span<const Anchor>(&anchor, 1), targetHash);
    }

  private:
    struct Entry {
        std::vector<std::pair<uint64_t, gc_node_ref_t>> anchors;
        Value value;
        Stamp stamp;
    };
    struct Key {
        KindId kind;
        uint64_t lineage;
        gc_node_ref_t node;
        bool operator==(const Key &) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key &key) const;
    };

    static void checkType(KindId kind, const std::type_info &type);
    /// The stamp of a fact of `kind` at `anchors` computed now.
    static Stamp stampNow(const KindInfo &kind, std::span<const Anchor> anchors, uint64_t targetHash);
    /// Under mutex_: the entry at `anchors`, checked; nullptr when absent or stale (dropped).
    Entry *find(KindId kind, std::span<const Anchor> anchors, uint64_t targetHash, Status &status);

    mutable std::mutex mutex_;
    std::unordered_map<Key, std::vector<Entry>, KeyHash> entries_;
    Counts counts_;
};

/// Computes and stores, for every graph reachable from `root`, the facts of every registered
/// kind that has compute and is not GraphContent, at every anchor the kind names
/// (KindInfo::anchorsIn): what a consumer would obtain one by one. Facts already stored and
/// valid are kept.
void computeFacts(FactStore &store, GCGraph *root);

/**
 * `camel inspect --facts`: one line `FACT {json}` per fact about the graphs reachable from
 * `root`: the stored facts whose stamp matches those graphs, then the GraphContent facts read
 * from them. Fields: "kind", "version", "class" ("content", "derived", "target"), "anchors"
 * (a list of {"graph", "node"}, the node omitted for a graph as a whole), "value" (the kind's
 * print), "stamp" ({"revisions": [{"graph", "serial"}], "epoch", "target", "cone"}; null for
 * GraphContent). Stale facts are not printed (and are dropped). Ordered by kind, then graph and
 * node.
 */
void dumpFacts(FactStore &store, GCGraph *root, std::ostream &os);

} // namespace camel::runtime::fact
