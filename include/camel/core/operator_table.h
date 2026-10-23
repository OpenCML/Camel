// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
 * side effects and may fault (design principles 3.9).
 *
 * Passes and back ends recognise an operator by its OperId, never by comparing URI text
 * (design section 3.8, R10). The operators the core itself knows have compile-time ids
 * (namespace `oper` below): the table interns their URIs first, in a fixed order. An operator
 * of a module that a pass of the same module recognises is interned once by that pass and
 * compared by id. Families are flags of the entry, derived from the URI when it is interned:
 * built-in (`:` prefix: no module protocol) and assignment (`:op/assn...`: `=`
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
#include "camel/core/node_attrs.h"
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

namespace camel::core::data {
class Data;
using data_ptr_t = std::shared_ptr<Data>;
} // namespace camel::core::data

namespace camel::core {

/// Index of an operator in the OperatorTable.
using OperId = uint32_t;

/// "No operator": the value of a zero-initialized OPER payload. Its URI is the empty string and
/// it is never declared, so every query treats it conservatively.
inline constexpr OperId kNoOper = 0;

/// The operators the core recognises by identity, with their ids fixed at compile time. The
/// table interns their URIs (kWellKnownOperatorUris, same order) before anything else, so
/// `body->oper == oper::VarGet` is the test that `body->uri() == ":var/get"` used to be.
///
/// Besides the variables, the list holds the operators the compiler emits itself, as the
/// lowering of a construct (`__enum_make__`, `__throw__`, `__set_index__`): the builder refers
/// to those by one of these ids, obtained from the table at start-up, not by the entity name
/// a module registered them under. An id names one URI; a group of several URIs (`__assn__` and
/// `__iter__`, whose overloads are chosen by the operand types) has no one id and is referred
/// to by the name of its group.
namespace oper {
inline constexpr OperId VarNew          = 1; // ":var/new"  `var v = e`
inline constexpr OperId VarGet          = 2; // ":var/get"  reading a variable
inline constexpr OperId VarPeek         = 3; // ":var/peek" a read whose value cannot be kept
inline constexpr OperId VarTake         = 4; // ":var/take" the value, moved out of the variable
inline constexpr OperId AssnOwned       = 5; // ":op/assn_owned"
inline constexpr OperId SetField        = 6; // ":op/set_field"
inline constexpr OperId SetIndex        = 7; // ":op/set_index"
inline constexpr OperId TagIsNull       = 8; // ":op/tag_is_null"
inline constexpr OperId TagNullValue    = 9; // ":op/tag_null_value"
inline constexpr OperId TagIsError      = 10; // ":op/tag_is_error"
inline constexpr OperId TagErrorValue   = 11; // ":op/tag_error_value"
inline constexpr OperId TagErrorInfo    = 12; // ":op/tag_error_info"
inline constexpr OperId TagThrow        = 13; // ":op/tag_throw"
inline constexpr OperId EnumMake        = 14; // ":enum/make"
inline constexpr OperId EnumTag         = 15; // ":enum/tag"
inline constexpr OperId EnumField       = 16; // ":enum/field"
inline constexpr OperId CopyArray       = 17; // ":struct/copy_arr"
inline constexpr OperId AppendOwned     = 18; // ":struct/append_arr_owned"
inline constexpr OperId MacroCmp        = 19; // ":macro/cmp"
inline constexpr OperId kWellKnownCount = 20; // kNoOper included

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
    ":var/peek",
    ":var/take",
    ":op/assn_owned",
    ":op/set_field",
    ":op/set_index",
    ":op/tag_is_null",
    ":op/tag_null_value",
    ":op/tag_is_error",
    ":op/tag_error_value",
    ":op/tag_error_info",
    ":op/tag_throw",
    ":enum/make",
    ":enum/tag",
    ":enum/field",
    ":struct/copy_arr",
    ":struct/append_arr_owned",
    ":macro/cmp",
};

/**
 * The traits of an operator, as `find` reports them. They are not stored: every one is derived
 * from the operator's semantic record (`sem::OperatorSemantics`), the only place a fact is
 * declared (`traitsOf`, operator_table.cpp). Declaring a trait means declaring the fact the
 * record holds; the two cannot disagree.
 */
struct OperatorTraits {
    /// No side effects and no hidden state: the result depends only on the arguments, so a call
    /// may be evaluated at compile time, merged with an identical call, or removed if unused.
    bool pure = false;
    /// Output element i depends only on input elements i (after broadcasting).
    bool elementwise = false;
    /// Not pure, but without an effect outside the program's own variables: it creates, reads
    /// or changes a variable (`:var/new`, `:var/get`, `:var/take`); which of them is its
    /// resource declaration's to say, the effect summaries read it there. Its result depends on
    /// when it runs, so it is never folded, merged or moved across a write; it is not an effect a
    /// user can observe, so it is not reported as one (effect trace).
    bool stateOnly = false;
    /// The operator can raise a fault (design principles 3.9: division by zero, an index out
    /// of range, a conversion whose value does not fit). True unless the module declares
    /// otherwise. Recorded for consumers of the graph model; no pass in this repository
    /// changes its behaviour on it yet (docs/design/graph-model, question Q1).
    bool mayFault = true;
    /// While it runs, the operator calls function values it received as arguments (the
    /// higher-order operators and `attempt`), so what it does depends on those functions.
    bool callsGraphs = false;
    /// The operator reads the graphs of the function values it receives as arguments (it
    /// compiles, exports or differentiates them) instead of only calling them. Such an operator
    /// sees the program's own graphs, with their structure (a LOOP node and its regions:
    /// docs/design/op-semantics 11.4).
    bool readsGraphs = false;
    /// The operator is a macro operator (design principles 8): a call of it whose arguments are
    /// constants is evaluated ahead of time by `std::macro`, and the result is materialised as a
    /// constant. `grad` and `with_grad` declare it; an ordinary operator does not.
    bool macro = false;
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
 * What an operator used as a decorator grants (language reference 7.6): an attribute
 * (camel/core/node_attrs.h) on the nodes written in the function or block it decorates, the
 * arms, loop bodies and lambdas written there included, and on no function they call.
 *
 * The operator itself takes its with-arguments and one function and returns a function of the
 * same type (OperatorTraits as for any operator). When a decorator names it with with-arguments
 * that are constants, the graph builder applies it while it builds the function: it gives the
 * nodes the attribute and drops the call, so passes see the attribute and the function stays
 * the one written. Anywhere else (a decorator that names it through a variable, arguments known
 * only at run time, a call written as a call) the operator runs like any other; its function is
 * then the decorator's runtime form, which may return the function unchanged
 * (camel::core::module::attrDecoratorIdentity): an attribute only allows a library to do
 * something else, so a node without it is always correct.
 */
struct AttrGrant {
    /// The attribute key's name; the library registers the key with its kind and rules
    /// (attr::registerKey).
    std::string key;
    /// The value granted for the with-arguments of one use, given as the constants written
    /// (aligned with the with-parameters); nullopt when the constants do not give one, and the
    /// decorator is then applied when it runs. Null: attr::unit() whatever the arguments.
    std::function<std::optional<attr::AttrValueId>(std::span<const data::data_ptr_t> with)> value;
};

/**
 * What the compiler does at a call of the operator because of what the operator is (design
 * principles 6.6): facts of the operator, declared with it, instead of names the compiler
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

    /// The operator declares how it calls the function values it receives (the higher-order
    /// array operators and `attempt`): the schedulers may run it themselves, by that declaration,
    /// rather than through its function pointer. Always a `callsGraphs` operator. Lock-free.
    bool isMarked(OperId id) const { return elementCalls(id) != nullptr; }

    /// The operator's element-call declaration, or nullptr.
    const sem::ElementCalls *elementCalls(OperId id) const {
        const sem::OperatorSemantics *declared = semantics(id);
        return declared && declared->elementCalls ? &*declared->elementCalls : nullptr;
    }

    /// An assignment (`:op/assn`, `:op/assn_l`, `:op/assn_add_i`, ...): `=` or a compound
    /// assignment, storing its second operand (or the result of the operation) into the
    /// variable cell that is its first. Lock-free.
    bool isAssignment(OperId id) const { return (entries_[id].family & kFamilyAssignment) != 0; }

    // ---- Declarations (module load time) --------------------------------------------------

    /// Declares (or replaces) the traits of `uri`, e.g. "tensor:matmul" or ":op/add_i".
    void set(std::string_view uri, OperatorTraits traits);

    /// Declares the purity of the operators `protocol:name`, one `{name, pure}` each. Every
    /// operator of a module has to be declared, the ones with side effects too (design
    /// principles 3.3): an operator nobody declared is treated as having side effects, and
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
    bool callsGraphs(OperId id) const {
        const sem::OperatorSemantics *facts = record(id);
        return facts != nullptr && (facts->callsGraphs || facts->elementCalls.has_value());
    }

    /// True when the operator reads the graphs of the function values it receives.
    bool readsGraphs(OperId id) const {
        const sem::OperatorSemantics *facts = record(id);
        return facts != nullptr && facts->readsGraphs;
    }

    /// True when the operator is a macro operator (design principles 8): `std::macro` may
    /// evaluate a call of it ahead of time. Lock-free.
    bool isMacro(OperId id) const {
        const sem::OperatorSemantics *facts = record(id);
        return facts != nullptr && facts->macro;
    }
    bool isMacro(std::string_view uri) const {
        const std::optional<OperatorTraits> traits = find(uri);
        return traits.has_value() && traits->macro;
    }

    /// True when running the operator is an effect visible outside the program's variables:
    /// it is neither pure nor state-only. True for an undeclared operator.
    bool hasExternalEffect(OperId id) const;
    bool hasExternalEffect(std::string_view uri) const;

    // ---- In-place forms (design principles 11.3) --------------------------------------
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

    /// Declares that the operator `uri` may return one of its operands, changed in place, when
    /// the compiler hands it a value no one else holds (design principles 11.3; the block must
    /// also be unshared and Camel-owned at run time, Array::canWriteInPlace). One fact, written
    /// where the operator is registered; the builder's temporary-value analysis reads it.
    void declareReusesOperand(std::string_view uri, bool reuses = true);

    /// Whether the operator `uri` was declared to reuse an operand.
    bool reusesOperand(std::string_view uri) const;

    // ---- Decorators that grant attributes (language reference 7.6) -------------------------

    /// Declares that the operator `uri`, used as a decorator, grants an attribute (AttrGrant).
    /// Module load time; std::invalid_argument when the key is not a valid key name.
    /// One tag declares its decorator and its node-attribute form together with
    /// tag::declareNodeTag (camel/core/tags.h); this is the part that makes the decorator work.
    void declareGrant(std::string_view uri, AttrGrant grant);

    /// What the operator `uri` grants, or nullptr when it is not declared to grant anything.
    std::shared_ptr<const AttrGrant> grantOf(std::string_view uri) const;

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

    // ---- Reduced-precision conversion gate (design principles 7.3, H238) -------------------

    /**
     * One call that would write a value of another element type into a reduced-precision one
     * (`bf16`, `f16`): the type it reads the value from and the type it writes it into, spelled
     * as a program writes them ("Tensor<f32>", "Tensor<bf16>"). nullopt when the call does no
     * such thing.
     */
    using ReducedPrecisionNarrowing = std::optional<std::pair<std::string, std::string>>;

    /**
     * The function a module that owns reduced-precision element types declares so that the
     * compiler can check a conversion written at a call against the numeric permissions in force
     * there. It recognises a narrowing from the operator `uri`, the argument types and the result
     * type (a call to a function is not a conversion: the conversion is written in that
     * function's body); the graph builder asks it after it stamped the node's permissions and
     * refuses the call with a located diagnostic unless the node carries `change_precision`
     * (`#strict` revokes it). Only the owning module knows what a `Tensor<dtype>` is, which is
     * why the table does not implement this itself.
     */
    using PrecisionGate = std::function<ReducedPrecisionNarrowing(
        std::string_view uri, std::span<type::Type *const> normArguments,
        std::span<type::Type *const> withArguments, const type::Type *result)>;

    /// Declares (module load time) the gate. Replacing it is a load-time mistake and throws
    /// std::logic_error, so two modules cannot silently overwrite one another's rule.
    void declarePrecisionGate(PrecisionGate gate);

    /// The gate, or nullptr when no module declared one. The shared pointer keeps it alive.
    std::shared_ptr<const PrecisionGate> precisionGate() const;

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
    /// stays valid until the process exits. A record that only carries traits (set without
    /// declareSemantics) is not semantics: this returns nullptr for it.
    const sem::OperatorSemantics *semantics(OperId id) const {
        return entries_[id].declared.load(std::memory_order_acquire);
    }

    /// Whether running the operator writes the resource the ordinary (or with) operand `index`
    /// names: from its resource declaration (design principles 3.3), the one source of what
    /// an operator writes. An operator without a declaration is read conservatively: every
    /// operand of a non-pure one, except that the operands of one that calls or reads the graphs
    /// of the function values it receives are not written by the operator (what the callee does
    /// to them is the callee's, reached through the function value).
    bool writesOperand(OperId id, bool isWith, size_t index) const;

    /// Whether the operator declares an access to a resource of class `cls` (`writesOnly`: a
    /// write). An undeclared operator declares none.
    bool touchesClass(OperId id, sem::ResourceClass cls, bool writesOnly = false) const;

    /// Whether the operator declares a write to a cell or to an object changed in place: state
    /// of the program itself (not ambient state, not a module's objects).
    bool writesProgramState(OperId id) const {
        return touchesClass(id, sem::ResourceClass::Variable, true) ||
               touchesClass(id, sem::ResourceClass::Object, true);
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

    // Family bits of an entry, fixed when the URI is interned (see isBuiltin, isAssignment).
    enum : uint8_t {
        kFamilyBuiltin    = 1 << 0,
        kFamilyAssignment = 1 << 2,
    };

    struct Entry {
        SymId uri      = kEmptySym;
        uint8_t family = 0;
        // Guarded by mutex_. A folder is heap-allocated once and replaced only at load time, so
        // readers may keep the pointer after releasing the lock.
        type::resolver_ptr_t resolver;
        std::unique_ptr<TypeFolder> folder;
        /// The in-place form (setInPlaceForm); kEmptySym when there is none.
        SymId inPlace = kEmptySym;
        /// The operator may return its operand (declareReusesOperand): an elementwise operator
        /// that writes into an operand it owns, rather than allocating a result.
        bool reusesOperand = false;
        /// What it grants as a decorator (declareGrant), or nullptr.
        std::shared_ptr<const AttrGrant> grant;

        /// The call-site facts (declareCallSpec), or null.
        std::unique_ptr<CallSpec> callSpec;
        /// The facts record (owned by declarations_), or nullptr when nothing is declared. Its
        /// effect/elementwise/callsGraphs/readsGraphs/faults members are what the traits are
        /// derived from; set() may publish one before there is any semantics.
        std::atomic<const sem::OperatorSemantics *> facts{nullptr};
        /// The published semantics record (owned by declarations_), or nullptr while only
        /// traits are declared. It is `facts` whenever that has real semantics.
        std::atomic<const sem::OperatorSemantics *> declared{nullptr};
    };

    /// The traits of `facts`, derived here and nowhere else.
    static OperatorTraits traitsOf(const sem::OperatorSemantics &facts);
    /// The facts record of `id` (traits included), or nullptr. Lock-free, never freed.
    const sem::OperatorSemantics *record(OperId id) const {
        return entries_[id].facts.load(std::memory_order_acquire);
    }

    /// intern() for callers that already hold mutex_ exclusively.
    OperId internLocked(std::string_view uri);

    mutable std::shared_mutex mutex_;
    std::unordered_map<SymId, OperId> ids_; // URI symbol -> entry index
    camel::utils::AppendOnlyTable<Entry> entries_;
    /// Every facts record ever published, replaced ones included (a reader may still hold one).
    /// Guarded by mutex_.
    std::vector<std::unique_ptr<const sem::OperatorSemantics>> declarations_;
    /// The reduced-precision conversion gate (declarePrecisionGate), or nullptr. Guarded by
    /// mutex_; a reader copies the shared pointer.
    std::shared_ptr<const PrecisionGate> precisionGate_;
    std::atomic<uint64_t> epoch_{0};
};

} // namespace camel::core
