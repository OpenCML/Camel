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
 * Created: Oct. 13, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operator table: one entry per operator URI, referred to by a 32-bit OperId.
 *
 * Part of the graph data model (docs/design/graph-model, sections 3.2, 3.5 and 3.8). An OPER
 * node holds the OperId of the operator it calls instead of the URI text, and everything a
 * pass may ask about an operator without running it is a field of its entry:
 *
 *   uri            ":op/add_l", "tensor:matmul"
 *   traits         what the operator's module declared about it (OperatorTraits)
 *   resolver       its type rule, re-run by passes that change what is known about arguments
 *   type folder    the value its result has whenever the argument types fix it
 *   semantics      the versioned record of what it computes (camel/core/operator_semantics.h):
 *                  its stable key "protocol:name@major.minor", its numeric semantics and
 *                  faults (docs/design/op-semantics, section 4)
 *
 * Generic graph passes (constant folding, CSE, DCE) must not know about any particular
 * module, yet they need to know whether an operator may be evaluated early, merged or
 * dropped. Modules and the builtin operator set declare those properties here when they are
 * loaded; passes only query. An operator nobody declared is treated conservatively: it has
 * side effects and may fault (design principles 2.9).
 *
 * Passes and back ends recognise an operator by its OperId, never by comparing URI text
 * (design section 3.8, R10). The operators the core itself knows have compile-time ids
 * (namespace `oper` below): the table interns their URIs first, in a fixed order. An operator
 * of a module that a pass of the same module recognises is interned once by that pass and
 * compared by id. Families are flags of the entry, derived from the URI when it is interned:
 * built-in (`:` prefix: no module protocol), marked (`:mark/`: the schedulers run it
 * themselves, it calls the function values it receives) and assignment (`:op/assn...`: `=`
 * and the compound assignments, which store into a variable cell).
 *
 * The implementation (the C++ function pointer) is deliberately not in the table: it is
 * resolved per Context by the executor manager.
 *
 * Ownership and lifetime: entries are created on first mention of a URI (by a declaration, a
 * resolver registration, or a graph being built) and are never removed or renumbered, so an
 * OperId is valid until the process exits. Ids are process-local: anything that leaves the
 * process (a module cache) must store URIs and re-intern them when it is loaded.
 *
 * Thread-safety: every member function may be called from any thread. uri() and the trait
 * queries by OperId take no lock (the parallel scheduler asks them while it runs graphs);
 * traits are one atomic byte per entry, so a declaration racing a query yields either the old
 * or the new traits, never a mixture. The lookups by URI text and the resolver/folder fields
 * are guarded by a reader/writer lock; they are compile-time paths. A semantics record is
 * published through an atomic pointer once it has passed its checks and is never changed or
 * freed afterwards (a replacement publishes a new record), so semantics() is lock-free too.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/symbol.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/append_only_table.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::core {

/// Index of an operator in the OperatorTable.
using OperId = uint32_t;

/// "No operator": the value of a zero-initialized OPER payload. Its URI is the empty string and
/// it is never declared, so every query treats it conservatively.
inline constexpr OperId kNoOper = 0;

/// The operators the core recognises by identity, with their ids fixed at compile time. The
/// table interns their URIs (kWellKnownOperatorUris, same order) before anything else, so
/// `body->oper == oper::VarGet` is the test that `body->uri() == ":var/get"` used to be.
namespace oper {
inline constexpr OperId VarNew              = 1;  // ":var/new"  `var v = e`
inline constexpr OperId VarGet              = 2;  // ":var/get"  reading a variable
inline constexpr OperId MapArr              = 3;  // ":mark/map_arr"
inline constexpr OperId ApplyArr            = 4;  // ":mark/apply_arr"
inline constexpr OperId FilterArr           = 5;  // ":mark/filter_arr"
inline constexpr OperId ReduceArr           = 6;  // ":mark/reduce_arr"
inline constexpr OperId UnorderedReduceArr  = 7;  // ":mark/unordered_reduce_arr"
inline constexpr OperId ForeachArr          = 8;  // ":mark/foreach_arr"
inline constexpr OperId UnorderedForeachArr = 9;  // ":mark/unordered_foreach_arr"
inline constexpr OperId Isolate             = 10; // ":mark/isolate"  `attempt`
inline constexpr OperId VarPeek             = 11; // ":var/peek" a read whose value cannot be kept
inline constexpr OperId kWellKnownCount     = 12; // kNoOper included

/// Reading a variable: `:var/get`, or `:var/peek`, the same read for a value that only goes to
/// operations that cannot keep it (it leaves the variable's sole-reference mark set; see
/// Builder::markNonRetainingReads and __builtin__var_peek__). Whatever looks at reads of
/// variables treats the two alike.
inline constexpr bool isVariableRead(OperId id) { return id == VarGet || id == VarPeek; }
} // namespace oper

/// The URIs of the ids in namespace `oper`, indexed by id (index 0, kNoOper, is empty).
inline constexpr std::string_view kWellKnownOperatorUris[oper::kWellKnownCount] = {
    "",
    ":var/new",
    ":var/get",
    ":mark/map_arr",
    ":mark/apply_arr",
    ":mark/filter_arr",
    ":mark/reduce_arr",
    ":mark/unordered_reduce_arr",
    ":mark/foreach_arr",
    ":mark/unordered_foreach_arr",
    ":mark/isolate",
    ":var/peek",
};

struct OperatorTraits {
    /// No side effects and no hidden state: the result depends only on the arguments, so a call
    /// may be evaluated at compile time, merged with an identical call, or removed if unused.
    bool pure = false;
    /// Output element i depends only on input elements i (after broadcasting).
    bool elementwise = false;
    /// Not pure, but without an effect outside the program's own variables: it creates or
    /// reads a variable (`:var/new`, `:var/get`). Its result depends on when it runs, so it
    /// is never folded, merged or moved across a write; it is not an effect a user can
    /// observe, so it is not reported as one (effect trace).
    bool stateOnly = false;
    /// The operator can raise a fault (design principles 2.9: division by zero, an index out
    /// of range, a conversion whose value does not fit). True unless the module declares
    /// otherwise. Recorded for consumers of the graph model; no pass in this repository
    /// changes its behaviour on it yet (docs/design/graph-model, question Q1).
    bool mayFault = true;
    /// While it runs, the operator calls function values it received as arguments (the
    /// higher-order operators and `attempt`), so what it does depends on those functions.
    bool callsGraphs = false;
    /// The operator reads the graphs of the function values it receives as arguments (it
    /// compiles, exports or differentiates them) instead of only calling them. Such an operator
    /// is given the program's own graphs, with their structure (a LOOP node: docs/design/
    /// op-semantics 11.4), also on a back end that runs a private, lowered copy of the program:
    /// lowerLoopsToTailRecursion (camel/runtime/loop_lowering.h) passes each of its operands of
    /// function type through `:graph/original`, which takes a function value of the copy back
    /// to the program's graph it was copied from (GCGraph::originalGraph).
    bool inspectsGraphs = false;
};

/**
 * The value an operator's result has whenever its arguments have the given types, for operators
 * whose result a type can fix: the shape of a tensor whose type carries its shape, its element
 * count, one of its dimensions. `statics` holds the arguments that are constants (aligned with
 * `types`); `allocator` holds any object the value needs. Returns nullopt when the types do not
 * fix the result. std::opt::fold uses these to turn such results into constants once shape
 * specialization has put the facts into the types.
 */
using TypeFolder = std::function<std::optional<slot_t>(
    std::span<type::Type *const> types, type::static_args_t statics, mm::IAllocator &allocator)>;

/**
 * What the compiler does at a call of the operator because of what the operator is (design
 * principles 2.17): facts of the operator, declared with it, instead of names the compiler
 * knows. Declared at module load time (OperatorTable::declareCallSpec).
 */
struct CallSpec {
    /// When the call is written with exactly this many ordinary arguments (-1: never), the
    /// compiler appends the position of the call as one more, constant string argument: like
    /// Rust's `#[track_caller]`. `error(kind, message)` uses it.
    int8_t callSiteAtArity = -1;
    /// The operator calls its function arguments and drops what they return (`foreach`): a
    /// function written in place there whose value is a value nothing takes is refused.
    bool discardsCallbackResult = false;
    /// The index of the with-argument, or of the ordinary argument, that is a format string
    /// (-1: none): when it is a constant, the compiler checks it against the other arguments.
    int8_t formatWithArg = -1;
    int8_t formatNormArg = -1;
    /// Checks a constant format string against the types of the arguments it formats; returns
    /// what is wrong with it, nullopt when it is fine. Set together with the index above.
    std::function<std::optional<std::string>(
        std::string_view pattern, std::span<type::Type *const> argumentTypes)>
        checkFormat;
};

class OperatorTable {
  public:
    static OperatorTable &instance();

    // ---- Identity -------------------------------------------------------------------------

    /// The id of the operator `uri`, creating an undeclared entry when the URI is new.
    OperId intern(std::string_view uri);

    /// The id of `uri` when the table has an entry for it, nullopt otherwise. Adds nothing.
    std::optional<OperId> lookup(std::string_view uri) const;

    /// The URI of `id`. The view is never invalidated. Lock-free.
    std::string_view uri(OperId id) const { return symbolText(entries_[id].uri); }

    /// Number of entries, kNoOper included.
    uint32_t size() const { return entries_.size(); }

    /// The operator's URI has no module protocol (`:op/add_i`, `:var/get`): it is one of the
    /// interpreter's own. Lock-free.
    bool isBuiltin(OperId id) const { return (entries_[id].family & kFamilyBuiltin) != 0; }

    /// A `:mark/` operator: the schedulers run it themselves rather than through a function
    /// pointer, because it calls the function values it receives (the higher-order array
    /// operators and `attempt`). Lock-free.
    bool isMarked(OperId id) const { return (entries_[id].family & kFamilyMarked) != 0; }

    /// An assignment (`:op/assn`, `:op/assn_l`, `:op/assn_add_i`, ...): `=` or a compound
    /// assignment, storing its second operand (or the result of the operation) into the
    /// variable cell that is its first. Lock-free.
    bool isAssignment(OperId id) const { return (entries_[id].family & kFamilyAssignment) != 0; }

    // ---- Declarations (module load time) --------------------------------------------------

    /// Declares (or replaces) the traits of `uri`, e.g. "tensor:matmul" or ":op/add_i".
    void set(std::string_view uri, OperatorTraits traits);

    /// Declares the purity of the operators `protocol:name`, one `{name, pure}` each. Every
    /// operator of a module has to be declared, the ones with side effects too (design
    /// principles 2.9): an operator nobody declared is treated as having side effects, and
    /// its module is told so when it is loaded. `pure` means the result depends on the
    /// arguments only: not on a clock, a random generator, the environment or a file.
    void declare(
        std::string_view protocol,
        std::initializer_list<std::pair<std::string_view, bool>> operators);

    // ---- Queries --------------------------------------------------------------------------

    /// Declared traits of the operator, or nullopt when nobody declared it.
    std::optional<OperatorTraits> find(OperId id) const;
    std::optional<OperatorTraits> find(std::string_view uri) const;

    /// True when the operator is declared pure.
    bool isPure(OperId id) const;
    bool isPure(std::string_view uri) const;

    /// True when execution can invoke function inputs. Lock-free.
    bool callsGraphs(OperId id) const { return (traitBits(id) & kCallsGraphs) != 0; }

    /// True when the operator reads the graphs of the function values it receives.
    bool inspectsGraphs(OperId id) const { return (traitBits(id) & kInspectsGraphs) != 0; }

    /// True when running the operator is an effect visible outside the program's variables:
    /// it is neither pure nor state-only. True for an undeclared operator.
    bool hasExternalEffect(OperId id) const;
    bool hasExternalEffect(std::string_view uri) const;

    // ---- In-place forms (design principles 2.12, 2.13) --------------------------------------
    //
    // An operator returns a new value and never changes its arguments (`sort` returns a new
    // array). Where a variable is bound to the result of the operator applied to the
    // variable's own array, `xs = xs->sort`, and nothing but the variable refers to that
    // array, changing the array in place gives the same result without a copy: an
    // implementation-level optimisation that does not enter the semantics. An operator that
    // can work so declares an in-place form: an operator with the same parameters and the same
    // result type that may change its first ordinary argument (an array) and returns it, which
    // the compiler calls only on an array no one else holds (Builder::inPlaceRebindingOf).
    // The form must change nothing before it can no longer fail: a fault leaves the array as
    // it was. It is not a name a program can write.

    /// Declares that `inPlaceUri` is the in-place form of the operator `uri` (module load time).
    void setInPlaceForm(std::string_view uri, std::string_view inPlaceUri);

    /// The in-place form of the operator `uri`, or nullopt when it has none.
    std::optional<std::string> inPlaceForm(std::string_view uri) const;

    // ---- Call-site facts (CallSpec) --------------------------------------------------------

    /// Declares the call-site facts of the operator `uri` (module load time).
    void declareCallSpec(std::string_view uri, CallSpec spec);

    /// The call-site facts of the operator, or nullptr when it declares none. The pointer stays
    /// valid until the spec of the same operator is replaced (load time only).
    const CallSpec *callSpec(std::string_view uri) const;

    // ---- Type rules -----------------------------------------------------------------------

    /// Registers (or replaces) the type resolver of an operator overload. An OPER node records
    /// only the operator its call resolved to; passes that change what is known about the
    /// arguments (binding input shapes, folding a shape to a constant) re-run the overload's
    /// resolver through the table. Operator groups register their overloads when created.
    void setResolver(std::string_view uri, type::resolver_ptr_t resolver);

    /// The resolver of the operator, or nullptr.
    type::resolver_ptr_t resolver(OperId id) const;
    type::resolver_ptr_t resolver(std::string_view uri) const;

    /// Registers (or replaces) the type folder of an operator.
    void setTypeFolder(std::string_view uri, TypeFolder folder);

    /// The type folder of the operator, or nullptr. The pointer stays valid until the folder
    /// of the same operator is replaced, which only happens while modules load.
    const TypeFolder *typeFolder(OperId id) const;
    const TypeFolder *typeFolder(std::string_view uri) const;

    // ---- Semantics (docs/design/op-semantics, section 4) ------------------------------------

    /**
     * Declares (or replaces) the semantics of the operator `uri`, at module load time. The
     * record is checked before it is published (design section 4.4); a record that fails is
     * rejected with std::invalid_argument naming the operator and the reason, and the table
     * is left as it was. The checks:
     *
     *   1. `semantics.key` names `uri`; a replacement does not lower the version.
     *   2. It does not contradict the traits: an operator declared never to fault declares no
     *      fault.
     *   3. The numeric part is consistent: Bounded has a positive ulpBound and is sensitive to
     *      approx_math; Exact and CorrectlyRounded have no ulpBound; Exact is sensitive at most
     *      to the finite_math assumptions (it has no rounding a permission could change);
     *      Opaque is sensitive to everything; CorrectlyRounded and Bounded name the type
     *      (Float32 or Float64) their steps are rounded in.
     *   4. Every reduction law is consistent in itself (sem::checkLaw): its constants fit the
     *      accumulator, a float add or multiply does not claim exact associativity, a law that
     *      starts from its first contribution faults on an empty domain.
     *   5. The resources are consistent in themselves (sem::checkResources) and with the
     *      traits: a pure operator touches none; any other touches, allocates or affects
     *      something; an external effect only where the traits allow one (not pure, not
     *      state-only), while an operator the traits count as external may declare none (a
     *      refinement: an assignment writes the program's own cell). Checked against the
     *      traits declared so far: declare them first.
     *   6. A definition names a library module and a function, and binds every parameter to
     *      an operand (the rest is checked when the library is compiled).
     *
     * The instances an instantiation function returns cannot be checked here (it is code);
     * sem::instantiate validates each one structurally, and the property tests compare the
     * declarations with the kernels (docs/design/op-semantics, section 12).
     *
     * Every declaration, new or replacing, increments declarationEpoch().
     */
    void declareSemantics(std::string_view uri, sem::OperatorSemantics semantics);

    /// Declares the resources of the operator `uri` (docs/design/op-semantics, section 8): its
    /// semantics record gets them, a record of its own (`uri@1.0`, nothing else declared) when
    /// it has none, otherwise the same record one minor version up. Checked as
    /// declareSemantics checks (check 5); throws std::invalid_argument when rejected.
    void declareResources(std::string_view uri, sem::ResourceSpec resources);

    /// Declares the definition of the operator `uri` (docs/design/op-semantics, section 9): a
    /// function of a library module written in Camel that is its normative definition. Its
    /// semantics record gets it, as declareResources gives resources (a record of its own, or
    /// the existing one a minor version up: a definition is a fact added). Checked as
    /// declareSemantics checks (check 6); the definition itself is compiled and checked when it
    /// is first asked for (camel/runtime/definitions.h).
    void declareDefinition(std::string_view uri, sem::Definition definition);

    /// declareResources for `protocol:name`, one `{name, resources}` each (a module's load).
    void declareResources(
        std::string_view protocol,
        std::initializer_list<std::pair<std::string_view, sem::ResourceSpec>> operators);

    /// The declared semantics, or nullptr when nobody declared any. Lock-free; the record
    /// stays valid until the process exits.
    const sem::OperatorSemantics *semantics(OperId id) const {
        return entries_[id].semantics.load(std::memory_order_acquire);
    }

    /// The stable key of an operator that has semantics, nullopt otherwise.
    std::optional<sem::OperatorKey> key(OperId id) const;

    /// The process-local id of a stable key read back from outside the process (a cache):
    /// nullopt when the operator is unknown, has no semantics, or its version cannot satisfy
    /// `key` (another major version, or a lower minor version than the one recorded).
    std::optional<OperId> bind(const sem::OperatorKey &key) const;

    /// Incremented whenever a semantics declaration is added or replaced (module load). A fact
    /// derived from declarations records it and is stale when it changed.
    uint64_t declarationEpoch() const { return epoch_.load(std::memory_order_acquire); }

  private:
    OperatorTable();

    // Trait bits of an entry. kDeclared distinguishes "declared effectful" from "nobody
    // declared it". kNoFault is stored inverted so that an all-zero byte is the conservative
    // reading of every trait.
    enum : uint8_t {
        kDeclared       = 1 << 0,
        kPure           = 1 << 1,
        kElementwise    = 1 << 2,
        kStateOnly      = 1 << 3,
        kNoFault        = 1 << 4,
        kCallsGraphs    = 1 << 5,
        kInspectsGraphs = 1 << 6,
    };

    // Family bits of an entry, fixed when the URI is interned (see isBuiltin, isMarked).
    enum : uint8_t {
        kFamilyBuiltin    = 1 << 0,
        kFamilyMarked     = 1 << 1,
        kFamilyAssignment = 1 << 2,
    };

    struct Entry {
        SymId uri      = kEmptySym;
        uint8_t family = 0;
        std::atomic<uint8_t> traits{0};
        // Guarded by mutex_. A folder is heap-allocated once and replaced only at load time, so
        // readers may keep the pointer after releasing the lock.
        type::resolver_ptr_t resolver;
        std::unique_ptr<TypeFolder> folder;
        /// The in-place form (setInPlaceForm); kEmptySym when there is none.
        SymId inPlace = kEmptySym;
        /// The call-site facts (declareCallSpec), or null.
        std::unique_ptr<CallSpec> callSpec;
        /// The published semantics record (owned by declarations_), or nullptr.
        std::atomic<const sem::OperatorSemantics *> semantics{nullptr};
    };

    static uint8_t encode(OperatorTraits traits);
    static OperatorTraits decode(uint8_t bits);

    /// intern() for callers that already hold mutex_ exclusively.
    OperId internLocked(std::string_view uri);
    uint8_t traitBits(OperId id) const {
        return entries_[id].traits.load(std::memory_order_relaxed);
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<SymId, OperId> ids_; // URI symbol -> entry index
    camel::utils::AppendOnlyTable<Entry> entries_;
    /// Every semantics record ever published, replaced ones included (a reader may still hold
    /// one). Guarded by mutex_.
    std::vector<std::unique_ptr<const sem::OperatorSemantics>> declarations_;
    std::atomic<uint64_t> epoch_{0};
};

} // namespace camel::core
