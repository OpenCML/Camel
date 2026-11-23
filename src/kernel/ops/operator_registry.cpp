// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The registry's implementation: the records by OperId, the two entrances that fill them, and the
 * completion that publishes one (operator_record.h).
 *
 * This file is the one place the reviewed header left undefined. `Registry::instance`, the two
 * `intern`s, the two `lookup`s, the two `find`s, `bind`, `size`, `declare`, `registerOperator`,
 * `complete` and `epoch` had declarations and no definition anywhere in the tree, and so had
 * `Record::effect()` and `Record::external()`. Nothing called any of them, so the absence was not
 * a link error; it was the reason a registration could be written and read by nobody.
 *
 * --- where the records live -------------------------------------------------
 * The registry's state is one function-local `Store` in this file. It is not a side table beside
 * the registry: nothing else has a copy of any item, and every reader of the registry goes through
 * `find`/`lookup`/`kernel` below. The header keeps no data member, so the interface stays the shape
 * that was reviewed.
 *
 * A record is heap-allocated once and never freed, which is what makes `find`'s answer, "the
 * pointer stays valid until the process exits", true. The pointer is published through a fixed
 * two-level table of atomics (4096 slots per chunk, 1024 chunks), so `find` takes no lock: it is
 * two acquire loads. The `OperId` -> record map that a lock protects is only the *names*: `intern`
 * and `lookup` take the mutex, and `declare`/`registerOperator`/`complete` take it while they write
 * a record. A record is written while modules load, and read after they have loaded; a reader that
 * runs concurrently with `declare` sees the items stated so far, which is the ordering the header
 * states ("A record is completed while modules load").
 *
 * --- two entrances, one item each -------------------------------------------
 * `declare` merges what a Camel declaration stated; `registerOperator` hands a `CppRegistration`'s
 * items to the same merge. An item the record already says is compared before it is merged, and a
 * disagreement throws `std::invalid_argument` naming the operator, the item and both statements --
 * the two entrances must not mean two different operators. A `Stated<T>` remembers which entrance
 * spoke, so "nobody said" keeps its own answer.
 *
 * Items whose value carries a `std::function` cannot be compared (`TypeFolder`, `InstantiateFn`,
 * `AttrGrant::value`, `CallSpec::checkFormat`, `BinaryLaws::elementOf`, and the type rule is a
 * `shared_ptr` that compares by pointer, not by rule). For those the first statement stands and a
 * second one is not refused: **待审** -- whether the record needs a comparable spelling of them (or
 * a `Stated`-style witness) is a question for the interface's review, and it is not decided here.
 *
 * --- the kernel -------------------------------------------------------------
 * `kernel(void *)` is not a record item ("a target registers its own implementations against the
 * same OperId and they do not enter the record"), so the pointer is kept beside the record, per
 * OperId, inside this registry, and `Registry::kernel(OperId)` (**待审**, added by this lane) is
 * its reader. It is not a side table: `registerOperator` is the only writer, and the registry is
 * the only owner.
 *
 * --- what this file does not settle -----------------------------------------
 * 1. `find` answers a record that is not `complete`d; the completed flag only refuses a later
 *    statement. Nothing in the tree completes a record yet, so a record that no one completed and
 *    a published one read the same today.
 * 2. `complete` checks what two statements of one fact can disagree about (the elementwise bit
 *    against an algebra whose laws hold element by element, an in-place form against its plain
 *    operator). The faults a `panic` names and the accesses a purity promise rules out are not
 *    re-derived: `panic` carries no list, and `uses` is one statement.
 * 3. A version: `intern` keeps the first key it was given for a URI. A later statement of another
 *    major version is merged into that record as if it were the same operator. `bind` is the one
 *    reader that compares versions, and it refuses a key the process cannot satisfy.
 */

#include "camel/core/resource_semantics.h"
#include "camel/ops/operator_record.h"

#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::ops {

// ---- `Record::effect()` and `Record::external()` --------------------------------------------
//
// The effect is read from `uses` and from nothing else (docs/design/op-semantics 4.2). `pure` is
// the empty clause and is not a second field, so there is no pair of statements here to compare:
// the clause decides, and a record nobody stated a clause for is the conservative reading.

bool Record::external() const {
    if (!uses.stated()) {
        // Nobody said: the operator is read as one that writes the global resource, which is
        // outside the program (design principles 3.3).
        return true;
    }
    if (uses.value.externalEffect) {
        return true;
    }
    if (uses.value.functionOperand) {
        // The accesses are a function value's, so an answer taken from the table cannot be
        // narrower than "may reach outside the program": the reader that has the value reads the
        // value's type (design principles 3.3).
        return true;
    }
    for (const sem::ResourceAccess &access : uses.value.accesses) {
        if (sem::reachesOutside(access.resource.cls)) {
            return true;
        }
    }
    return false;
}

sem::Effect Record::effect() const {
    if (!uses.stated()) {
        return sem::Effect::Undeclared;
    }
    if (uses.value.none() && !external()) {
        return sem::Effect::Pure;
    }
    return external() ? sem::Effect::Effectful : sem::Effect::StateOnly;
}

namespace {

// ---- The store ------------------------------------------------------------------------------

struct Slot {
    /// Published with a release store once the record is built; `find` reads it with an acquire
    /// load and neither lock nor atomic RMW.
    std::atomic<const Record *> record{nullptr};
    /// What the C++ entrance handed over with `kernel(void *)`: not a record item.
    std::atomic<void *> kernel{nullptr};
};

constexpr std::uint32_t kChunkShift = 12;
constexpr std::uint32_t kChunkSize  = std::uint32_t{1} << kChunkShift;
constexpr std::uint32_t kChunkMask  = kChunkSize - 1;
constexpr std::uint32_t kChunkCount = std::uint32_t{1} << 10; // 4,194,304 operators

struct Store {
    /// The lock-free read path: `id >> kChunkShift` picks a chunk, `id & kChunkMask` a slot in it.
    std::atomic<Slot *> chunks[kChunkCount]{};

    /// Everything below is written under `mutex`.
    std::mutex mutex;
    std::vector<std::unique_ptr<Slot[]>> chunkOwners;
    /// `records[id - 1]`; records are never moved and never freed, so `find`'s pointer stays valid.
    std::vector<std::unique_ptr<Record>> records;
    /// The URI text of each record, kept so that a `std::string_view` in a record can point at it
    /// (an `InPlaceForm` names its operator by view).
    std::vector<std::unique_ptr<std::string>> texts;
    std::vector<bool> completed;
    std::unordered_map<std::string, OperId> byName;
    std::uint32_t count = 1; // kNoOper is an id the registry answers for
    std::atomic<std::uint64_t> epoch{0};
};

Store &store() {
    static Store instance;
    return instance;
}

Slot *chunkOf(std::uint32_t id) {
    const std::uint32_t index = id >> kChunkShift;
    if (index >= kChunkCount) {
        return nullptr;
    }
    return store().chunks[index].load(std::memory_order_acquire);
}

/// The chunk `id` lives in, allocated under the caller's lock when it is the first id in it.
Slot *chunkFor(Store &s, std::uint32_t id) {
    const std::uint32_t index = id >> kChunkShift;
    if (index >= kChunkCount) {
        throw std::length_error(
            std::format(
                "the operator registry is full: id {} is past its {} records",
                id,
                kChunkCount * kChunkSize - 1));
    }
    Slot *chunk = s.chunks[index].load(std::memory_order_acquire);
    if (chunk == nullptr) {
        s.chunkOwners.push_back(std::make_unique<Slot[]>(kChunkSize));
        chunk = s.chunkOwners.back().get();
        s.chunks[index].store(chunk, std::memory_order_release);
    }
    return chunk;
}

/// The record of `id`, for a writer that already holds the lock; nullptr for `kNoOper` or an id
/// the registry never issued.
Record *mutableRecord(Store &s, OperId id) {
    if (id == kNoOper || id > s.records.size()) {
        return nullptr;
    }
    return s.records[id - 1].get();
}

const std::string &uriText(Store &s, OperId id) { return *s.texts[id - 1]; }

[[noreturn]] void refuseStatement(OperId id, std::string_view item, std::string_view why) {
    throw std::invalid_argument(std::format("operator {}: \"{}\" {}", id, item, why));
}

// ---- One item, one statement, two entrances ------------------------------------------------

/// A `Stated<T>` item: merged when the other entrance has not spoken, compared when it has.
template <typename T, typename Equal, typename Describe>
void mergeStated(
    std::string_view uri, std::string_view item, Stated<T> &into, const Stated<T> &from,
    Equal equal, Describe describe) {
    if (!from.stated()) {
        return;
    }
    if (!into.stated()) {
        into = from;
        return;
    }
    if (!equal(into.value, from.value)) {
        throw std::invalid_argument(
            std::format(
                "{}: \"{}\" is stated differently by the two entrances ({} and {})",
                uri,
                item,
                describe(into.value),
                describe(from.value)));
    }
    into.byCamel |= from.byCamel;
    into.byCpp |= from.byCpp;
}

/**
 * An item the record keeps as a bare value: a statement is the value that is not the "nothing
 * declared" default, and a second statement of another value is refused. A `std::function` item is
 * not passed here (this file's header, paragraph 3).
 */
template <typename T, typename Equal, typename Describe>
void mergeItem(
    std::string_view uri, std::string_view item, T &into, const T &from, const T &unstated,
    Equal equal, Describe describe) {
    if (equal(from, unstated)) {
        return; // the statement says nothing about this item
    }
    if (equal(into, unstated)) {
        into = from;
        return;
    }
    if (!equal(into, from)) {
        throw std::invalid_argument(
            std::format(
                "{}: \"{}\" is stated differently by the two entrances ({} and {})",
                uri,
                item,
                describe(into),
                describe(from)));
    }
}

bool sameSpecials(const sem::SpecialValues &a, const sem::SpecialValues &b) {
    return a.nans == b.nans && a.infinities == b.infinities && a.signedZeros == b.signedZeros &&
           a.subnormals == b.subnormals;
}

bool sameNumeric(const sem::NumericSemantics &a, const sem::NumericSemantics &b) {
    return a.cls == b.cls && a.ulpBound == b.ulpBound && a.compute == b.compute &&
           sameSpecials(a.specials, b.specials) && a.sensitiveTo == b.sensitiveTo &&
           a.algorithm == b.algorithm;
}

bool sameLaw(const sem::ReductionLaw &a, const sem::ReductionLaw &b) {
    // The fields a rule is selected by. The remaining bits (`initBits`, `identityBits`, the two
    // operands) belong to the same law; a rule stated twice with the same name and different bits
    // is compared by `operator==` below where the type has one.
    return a.name == b.name && a.kind == b.kind && a.input == b.input &&
           a.accumulator == b.accumulator && a.result == b.result &&
           a.exactAssociative == b.exactAssociative && a.exactCommutative == b.exactCommutative &&
           a.hasIdentity == b.hasIdentity && a.identityBits == b.identityBits &&
           a.initBits == b.initBits && a.empty == b.empty && a.order == b.order &&
           a.finalize == b.finalize;
}

// ---- The merge ------------------------------------------------------------------------------

/**
 * Merges one statement into a record. Called with the store's lock held, and only for a record that
 * is not completed yet.
 */
void mergeRecord(Record &into, const Record &from) {
    const std::string uri = into.uri();

    if (from.key.name != 0 &&
        (from.key.protocol != into.key.protocol || from.key.name != into.key.name)) {
        throw std::invalid_argument(
            std::format("{}: a statement of {} was merged into its record", uri, from.key.uri()));
    }
    if (from.id != kNoOper && from.id != into.id) {
        throw std::invalid_argument(
            std::format(
                "{}: a statement carries id {} and the record is {}",
                uri,
                from.id,
                into.id));
    }

    // ---- the properties keywords state ------------------------------------------------------

    mergeStated(
        uri,
        "uses",
        into.uses,
        from.uses,
        [](const sem::ResourceSpec &a, const sem::ResourceSpec &b) { return a == b; },
        [](const sem::ResourceSpec &v) { return std::format("{} access(es)", v.accesses.size()); });

    mergeStated(
        uri,
        "panic",
        into.panic,
        from.panic,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeStated(
        uri,
        "signature",
        into.signature,
        from.signature,
        [](const Signature &a, const Signature &b) {
            return a.type == b.type && a.fallible == b.fallible;
        },
        [](const Signature &v) {
            return std::format(
                "fallible={}, type={}",
                v.fallible,
                v.type != nullptr ? std::string_view("stated") : std::string_view("none"));
        });

    mergeStated(
        uri,
        "resolver",
        into.resolver,
        from.resolver,
        [](const type::resolver_ptr_t &a, const type::resolver_ptr_t &b) { return a == b; },
        [](const type::resolver_ptr_t &v) {
            return v != nullptr ? std::string_view("a type rule")
                                : std::string_view("no type rule");
        });

    // A `TypeFolder` is a `std::function`: the first statement stands (file header, paragraph 3).
    if (from.folder.stated() && !into.folder.stated()) {
        into.folder = from.folder;
    }

    // ---- what a pass asks about the operator ------------------------------------------------

    mergeItem(
        uri,
        "inPlace",
        into.inPlace,
        from.inPlace,
        std::optional<InPlaceForm>{},
        [](const std::optional<InPlaceForm> &a, const std::optional<InPlaceForm> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || (a->form == b->form && a->plain == b->plain);
        },
        [](const std::optional<InPlaceForm> &v) {
            return v.has_value() ? std::string(v->form) : std::string("none");
        });

    mergeItem(
        uri,
        "reusesOperand",
        into.reusesOperand,
        from.reusesOperand,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeItem(
        uri,
        "handsOn",
        into.handsOn,
        from.handsOn,
        std::optional<HandsOnValue>{},
        [](const std::optional<HandsOnValue> &a, const std::optional<HandsOnValue> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || a->onlyOutsideArrays == b->onlyOutsideArrays;
        },
        [](const std::optional<HandsOnValue> &v) {
            return v.has_value() ? std::string(
                                       v->onlyOutsideArrays ? "outside arrays only"
                                                            : "a value an operand holds")
                                 : std::string("none");
        });

    mergeItem(
        uri,
        "conversion",
        into.conversion,
        from.conversion,
        std::optional<ConversionTypes>{},
        [](const std::optional<ConversionTypes> &a, const std::optional<ConversionTypes> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || (a->source == b->source && a->target == b->target);
        },
        [](const std::optional<ConversionTypes> &v) {
            return v.has_value() ? std::format(
                                       "type {} -> type {}",
                                       static_cast<int>(v->source),
                                       static_cast<int>(v->target))
                                 : std::string("none");
        });

    mergeItem(
        uri,
        "grant",
        into.grant,
        from.grant,
        std::shared_ptr<const AttrGrant>{},
        [](const std::shared_ptr<const AttrGrant> &a, const std::shared_ptr<const AttrGrant> &b) {
            if (a == nullptr || b == nullptr) {
                return a == b;
            }
            return a->key == b->key; // the value function cannot be compared
        },
        [](const std::shared_ptr<const AttrGrant> &v) {
            return v != nullptr ? v->key : std::string("none");
        });

    mergeItem(
        uri,
        "callSpec",
        into.callSpec,
        from.callSpec,
        std::optional<CallSpec>{},
        [](const std::optional<CallSpec> &a, const std::optional<CallSpec> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() ||
                   (a->callSiteAtArity == b->callSiteAtArity &&
                    a->discardsCallbackResult == b->discardsCallbackResult &&
                    a->formatWithArg == b->formatWithArg && a->formatNormArg == b->formatNormArg);
        },
        [](const std::optional<CallSpec> &v) {
            return v.has_value()
                       ? std::format("format at {} of the with-arguments", v->formatWithArg)
                       : std::string("none");
        });

    mergeItem(
        uri,
        "callForm",
        into.callForm,
        from.callForm,
        std::optional<CallForm>{},
        [](const std::optional<CallForm> &a, const std::optional<CallForm> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() ||
                   (a->name == b->name && a->minArgs == b->minArgs &&
                    a->withCount == b->withCount && a->declaredByModule == b->declaredByModule &&
                    a->kernelDeclaration == b->kernelDeclaration && a->exported == b->exported);
        },
        [](const std::optional<CallForm> &v) {
            return v.has_value() ? v->name : std::string("none");
        });

    // ---- what it computes -------------------------------------------------------------------

    // An `InstantiateFn` is a `std::function`: the first statement stands (file header, paragraph
    // 3).
    if (from.indexing && !into.indexing) {
        into.indexing = from.indexing;
    }

    mergeItem(
        uri,
        "laws",
        into.laws,
        from.laws,
        std::vector<sem::ReductionLaw>{},
        [](const std::vector<sem::ReductionLaw> &a, const std::vector<sem::ReductionLaw> &b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (!sameLaw(a[i], b[i])) {
                    return false;
                }
            }
            return true;
        },
        [](const std::vector<sem::ReductionLaw> &v) { return std::format("{} law(s)", v.size()); });

    mergeItem(
        uri,
        "numeric",
        into.numeric,
        from.numeric,
        sem::NumericSemantics{},
        sameNumeric,
        [](const sem::NumericSemantics &v) {
            return std::format("class {}", sem::numericClassName(v.cls));
        });

    mergeItem(
        uri,
        "binary",
        into.binary,
        from.binary,
        std::optional<BinaryLaws>{},
        [](const std::optional<BinaryLaws> &a, const std::optional<BinaryLaws> &b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a.has_value() || a->laws == b->laws; // `elementOf` cannot be compared
        },
        [](const std::optional<BinaryLaws> &v) {
            return v.has_value() ? std::format("{} law(s)", v->laws.size()) : std::string("none");
        });

    mergeItem(
        uri,
        "elementCalls",
        into.elementCalls,
        from.elementCalls,
        std::optional<sem::ElementCalls>{},
        [](const std::optional<sem::ElementCalls> &a, const std::optional<sem::ElementCalls> &b) {
            return a.has_value() == b.has_value() && (!a.has_value() || a.value() == b.value());
        },
        [](const std::optional<sem::ElementCalls> &v) {
            return v.has_value() ? std::string("a call shape") : std::string("none");
        });

    mergeItem(
        uri,
        "definition",
        into.definition,
        from.definition,
        std::optional<sem::Definition>{},
        [](const std::optional<sem::Definition> &a, const std::optional<sem::Definition> &b) {
            return a.has_value() == b.has_value() && (!a.has_value() || a.value() == b.value());
        },
        [](const std::optional<sem::Definition> &v) {
            return v.has_value() ? std::format("{}:{}", v->module, v->function)
                                 : std::string("none");
        });

    // ---- the single statements of what was stated twice -------------------------------------

    mergeItem(
        uri,
        "elementwise",
        into.elementwise,
        from.elementwise,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeItem(
        uri,
        "callsGraphs",
        into.callsGraphs,
        from.callsGraphs,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeItem(
        uri,
        "readsGraphs",
        into.readsGraphs,
        from.readsGraphs,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeItem(
        uri,
        "macro",
        into.macro,
        from.macro,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    // `foldable` is true by default, so "the statement states true" and "the statement says
    // nothing" cannot be told apart -- the same reading the C++ entrance's setter takes.
    mergeItem(
        uri,
        "foldable",
        into.foldable,
        from.foldable,
        true,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });

    mergeItem(
        uri,
        "withAfterNorm",
        into.withAfterNorm,
        from.withAfterNorm,
        false,
        [](bool a, bool b) { return a == b; },
        [](bool v) { return v ? std::string_view("true") : std::string_view("false"); });
}

/// `intern`'s body; the caller holds the lock.
OperId internLocked(Store &s, const sem::OperatorKey &key) {
    const std::string uri = key.uri();
    if (const auto existing = s.byName.find(uri); existing != s.byName.end()) {
        return existing->second;
    }

    const OperId id = s.count;
    if (id == kNoOper) {
        throw std::length_error("the operator registry has no id left");
    }
    Slot *chunk = chunkFor(s, id);

    auto record             = std::make_unique<Record>();
    record->id              = id;
    record->key             = key;
    auto text               = std::make_unique<std::string>(uri);
    const Record *published = record.get();

    s.records.push_back(std::move(record));
    s.texts.push_back(std::move(text));
    s.completed.push_back(false);
    s.byName.emplace(uri, id);
    s.count = id + 1;
    chunk[id & kChunkMask].record.store(published, std::memory_order_release);
    return id;
}

} // namespace

// ---- The registry ---------------------------------------------------------------------------

Registry &Registry::instance() {
    static Registry registry;
    return registry;
}

Registry::Registry() = default;

OperId Registry::intern(const sem::OperatorKey &key) {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    return internLocked(s, key);
}

OperId Registry::intern(std::string_view uri) { return intern(sem::OperatorKey::of(uri)); }

std::optional<OperId> Registry::lookup(const sem::OperatorKey &key) const {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    const auto found = s.byName.find(key.uri());
    if (found == s.byName.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<OperId> Registry::lookup(std::string_view uri) const {
    return lookup(sem::OperatorKey::of(uri));
}

const Record *Registry::find(OperId id) const {
    if (id == kNoOper) {
        return nullptr;
    }
    const Slot *chunk = chunkOf(id);
    if (chunk == nullptr) {
        return nullptr;
    }
    return chunk[id & kChunkMask].record.load(std::memory_order_acquire);
}

const Record *Registry::find(std::string_view uri) const {
    const std::optional<OperId> id = lookup(uri);
    return id.has_value() ? find(*id) : nullptr;
}

std::optional<OperId> Registry::bind(const sem::OperatorKey &key) const {
    const std::optional<OperId> id = lookup(key);
    if (!id.has_value()) {
        return std::nullopt;
    }
    const Record *record = find(*id);
    if (record == nullptr) {
        return std::nullopt;
    }
    // A key read back from outside the process is satisfied while the major versions are equal and
    // this process's minor version is not lower (operator_semantics.h, `OperatorKey`).
    if (record->key.major != key.major || record->key.minor < key.minor) {
        return std::nullopt;
    }
    return id;
}

std::uint32_t Registry::size() const {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    return s.count;
}

void *Registry::kernel(OperId id) const {
    if (id == kNoOper) {
        return nullptr;
    }
    Slot *chunk = chunkOf(id);
    if (chunk == nullptr) {
        return nullptr;
    }
    return chunk[id & kChunkMask].kernel.load(std::memory_order_acquire);
}

void Registry::declare(OperId id, const Record &statement) {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    Record *record = mutableRecord(s, id);
    if (record == nullptr) {
        throw std::invalid_argument(
            std::format("operator {}: the registry has no such record", id));
    }
    if (s.completed[id - 1]) {
        throw std::invalid_argument(
            std::format(
                "{}: the record is completed, and a completed record is never changed again",
                record->uri()));
    }
    mergeRecord(*record, statement);
}

void Registry::stateShape(OperId id, OperShape shape) {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    Record *record = mutableRecord(s, id);
    if (record == nullptr) {
        throw std::invalid_argument(
            std::format("operator {}: the registry has no such record", id));
    }
    if (record->shape == shape) {
        return; // a statement of the answer the record already holds is a repetition
    }
    if (s.completed[id - 1]) {
        // A completed record is never changed again, and the shape is read by walks that run while
        // graphs are already being analysed: a second answer is refused, not taken.
        throw std::invalid_argument(
            std::format(
                "{}: \"shape\" is completed as \"{}\" and this statement says \"{}\"",
                record->uri(),
                camel::ops::shapeName(record->shape),
                camel::ops::shapeName(shape)));
    }
    record->shape = shape;
}

OperId Registry::registerOperator(CppRegistration &registration) {
    const Record &statement = registration.stated();
    if (statement.key.name == 0) {
        throw std::invalid_argument("a C++ registration must name its operator");
    }

    const OperId id = intern(statement.key);

    Store &s = store();
    std::lock_guard lock(s.mutex);
    Record *record = mutableRecord(s, id);
    if (record == nullptr) {
        throw std::invalid_argument(
            std::format("operator {}: the registry has no such record", id));
    }
    if (s.completed[id - 1]) {
        throw std::invalid_argument(
            std::format(
                "{}: the record is completed, and a completed record is never changed again",
                record->uri()));
    }
    mergeRecord(*record, statement);

    // The implementation the registration handed over. Not a record item: `find` never reports it,
    // and `Registry::kernel` is its one reader.
    Slot *chunk = chunkOf(id);
    if (chunk == nullptr) {
        throw std::logic_error(std::format("{}: no record slot for its id", record->uri()));
    }
    chunk[id & kChunkMask].kernel.store(registration.kernel_, std::memory_order_release);
    return id;
}

void Registry::complete(OperId id) {
    Store &s = store();
    std::lock_guard lock(s.mutex);
    Record *record = mutableRecord(s, id);
    if (record == nullptr) {
        throw std::invalid_argument(
            std::format("operator {}: the registry has no such record", id));
    }
    if (s.completed[id - 1]) {
        return; // published already: a completion states nothing new, so it moves no epoch
    }

    const std::string uri = record->uri();

    // The elementwise bit against an algebra whose laws hold element by element: a binary
    // operator that answered with an element type for a type says so, and one place says it.
    if (record->binary.has_value() && record->binary->elementOf && !record->elementwise) {
        refuseStatement(
            id,
            "elementwise",
            "is false, and the binary algebra states laws that hold element by element");
    }

    // The in-place form against its plain operator: the form is an operator of its own and its own
    // entry names the operator it computes the result of.
    const std::string_view form =
        record->inPlace.has_value() ? record->inPlace->form : std::string_view{};
    if (!form.empty()) {
        if (form == uri) {
            refuseStatement(id, "inPlace", "names the operator itself as its form");
        }
        if (!record->inPlace->plain.empty() && record->inPlace->plain != uri) {
            refuseStatement(
                id,
                "inPlace",
                std::format("is the form of {}, not of this operator", record->inPlace->plain));
        }

        const OperId formId = internLocked(s, sem::OperatorKey::of(form));
        Record *formRecord  = mutableRecord(s, formId);
        if (formRecord != nullptr) {
            const std::string &formText  = uriText(s, formId);
            const std::string &plainText = uriText(s, id);
            if (!formRecord->inPlace.has_value()) {
                if (s.completed[formId - 1]) {
                    refuseStatement(
                        id,
                        "inPlace",
                        std::format(
                            "names {} as its form, and that record is completed and names no "
                            "operator",
                            form));
                }
                // The form's own entry: its own URI, and the operator it computes the result of.
                formRecord->inPlace = InPlaceForm{formText, plainText};
            } else if (formRecord->inPlace->plain.empty()) {
                formRecord->inPlace->plain = plainText;
            } else if (formRecord->inPlace->plain != uri) {
                refuseStatement(
                    formId,
                    "inPlace",
                    std::format("is the form of {} and of {}", formRecord->inPlace->plain, uri));
            }
            record->inPlace->plain = formRecord->inPlace->plain;
        }
    }

    s.completed[id - 1] = true;
    s.epoch.fetch_add(1, std::memory_order_acq_rel);
}

std::uint64_t Registry::epoch() const { return store().epoch.load(std::memory_order_acquire); }

} // namespace camel::ops