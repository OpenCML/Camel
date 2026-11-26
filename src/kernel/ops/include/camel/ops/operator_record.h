// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator record: one record per operator, found by OperId, holding everything an operator
 * is (core abstractions section 3; docs/design/op-semantics).
 *
 * One record, two equal entrances. A record is filled either by a Camel declaration, which the
 * compiler compiles in when the module loads, or through the C++ registration API
 * (CppRegistration), which needs no declaration at all: a library author writes a Camel operator
 * entirely in C++, registers it, and a program calls it like any other. An item is stated by one
 * entrance; where both state the same item they must be equal, and the load that finds them
 * unequal fails naming the operator, the item and the two values. `Stated` is what remembers who
 * said what, so "nobody said" and "somebody said the conservative thing" stay different answers.
 *
 * The basic properties are written with keywords and types, not with labels. `uses` states what
 * running the operator reads and writes besides the values of its operands; `pure` is that
 * clause empty (a word for the empty list, not a second field). `panic` states whether a call of
 * the operator can fail (design principles 3.9): the Camel entrance states it by writing the
 * keyword, and a declaration that does not write it promises that no call fails; the C++ entrance
 * takes it as a required parameter, so a C++ operator always states one of the two answers. It
 * carries no list of kinds: a fault's kind is reported where the fault happens, by the operator
 * that raises it. `T!` is not that fact and says nothing about it: an error is a value (3.9, and
 * `T!` is a built-in `enum`), so an operator can return `T!` and never fail, and can fail while its
 * result is a plain `int` (a zero divisor). The signature records the `T!` as its own fact
 * (`Signature::fallible`); neither is derived from the other, and completion does not compare them.
 *
 * A label is graph information and is no member here. `#jvp` and `#transpose`, the numeric
 * permissions a program grants, the hints a target reads, the concepts a library gives its own
 * operators: the compiler puts them on the nodes of a program and a pass reads them there. A
 * label says what a pass may do with a node; it never says what the operator is.
 *
 * Each property appears once. Four that could be stated in more than one place are read from this
 * record, from nowhere else:
 *
 *   - the effect: `uses` and its one reading (`effect()`);
 *   - the macro fact: the node flag and the graph flag ask this record, and the standard tag
 *     `#macro` on a *declaration* says the same thing about a function, which is a different
 *     question and is not an operator trait;
 *   - the type rule: a group of overloads (`OperatorGroup`) is a name and the URIs of its
 *     overloads and keeps no second copy of the rules; the rule is stated here, once;
 *   - being elementwise: one bit, `elementwise` below, and nothing else states it. A binary
 *     operator's algebra states its laws and the element type they are about, not a second copy
 *     of the bit; and how an operator that calls a function value calls it -- a map, a filter, a
 *     fold -- is `ElementCalls::shape`, which is a different question: a map may pass another
 *     operand whole, so the shape says nothing about how an output element depends on an input
 *     element.
 *
 * Two more were stated in more than one place and are settled without becoming members here. The
 * numeric permissions are a label: the one statement of a mask is the tag a node carries, and the
 * declaration's mask and the node's mask are read through that tag -- whether the node's mask can
 * be derived from the tags it carries rather than stored beside them is what the review of this
 * interface has to settle, and the record takes no side.
 *
 * The rules of a transform are not here. A ruleset is a compile-time value handed to the
 * transform explicitly (core abstractions section 6), so importing a module changes no
 * transform's result. What the kernel does file of a ruleset is filed for the operator a rule is
 * written for, and that key is an OperId like every other key in this file -- never the URI text
 * a rule happens to be named after.
 *
 * The value types a record is made of (`OperatorKey`, `ResourceSpec`, `ResourceAccess`,
 * `NumericSemantics`, `ElementCalls`, `Definition`) are declared in
 * camel/core/operator_semantics.h and are not restated here. The types an operator table keeps
 * beside its entries today (the folder, the in-place form, the conversion, the call shape, the
 * call-site facts, the decorator grant) are declared below, because they are properties of the
 * operator and move with the record; until the table is gone a translation unit includes one of
 * the two headers, never both.
 *
 * Ownership and lifetime: a record is created on first mention of its key (a declaration, a C++
 * registration, or a graph being built) and is never removed or renumbered, so an OperId is
 * valid until the process exits. Ids are process-local: anything that leaves the process stores
 * the key and binds it again (`Registry::bind`).
 *
 * Thread-safety: reading a record is lock-free and a reader may keep the pointer it read. A
 * record is completed while modules load; a second statement of an existing item is compared
 * with the first before it is merged, and a completed record is never changed again -- a
 * replacement publishes a new revision (`Registry::complete`), and a reader still holding the
 * old one keeps a consistent answer.
 */

#pragma once

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/core/type/resolver.h"
#include "camel/ops/oper_shape.h"
#include "camel/ops/subscript_form.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::data {
class Data;
using data_ptr_t = std::shared_ptr<Data>;
} // namespace camel::core::data

namespace camel::core::type {
class FunctionType;
} // namespace camel::core::type

namespace camel::ops {

// The blocks the entries below are made of, named once so that the text of the record does not
// open a second spelling of any of them.
namespace attr = ::camel::core::attr;
namespace data = ::camel::core::data;
namespace mm   = ::camel::core::mm;
namespace sem  = ::camel::core::sem;
namespace type = ::camel::core::type;

/// The index of an operator's record. Process-local; see the note on lifetime above.
using OperId = std::uint32_t;

/// No operator. A zero-initialized operand or node field holds it, and every query about it is
/// the conservative one: an operator nobody stated anything about.
inline constexpr OperId kNoOper = 0;

/// Which entrance stated an item.
enum class Origin : std::uint8_t { None, Camel, Cpp };

/**
 * An item of the record together with the entrances that stated it.
 *
 * `stated()` is not `value == T{}`: `panic == false` is a statement ("no call of this operator
 * can fail") and is not the absence of one ("nobody said, so a call may fail"). The two flags are
 * what keeps that difference, and `complete` is what refuses a record whose two entrances
 * disagree (`byCamel && byCpp && !equal`).
 */
template <typename T> struct Stated {
    T value{};
    bool byCamel = false;
    bool byCpp   = false;

    bool stated() const { return byCamel || byCpp; }
    Origin origin() const {
        if (byCamel && byCpp) {
            return Origin::Camel;
        }
        return byCamel ? Origin::Camel : (byCpp ? Origin::Cpp : Origin::None);
    }
};

/// The type of a call of the operator: what a declaration states, and what the type rule
/// answers for a call whose argument types are known.
struct Signature {
    /// The function type of one overload: its with-parameters, then its ordinary ones, then the
    /// result. Null when the operator has no stated signature (a dynamic rule, a kernel a module
    /// declares without a body).
    const type::FunctionType *type = nullptr;
    /// The result is written `T!`: a call gives a value of the payload type, or hands on the error
    /// it came back with (3.9; an error is a value, not a fault). This is the signature's own fact
    /// and **not** `panic`: it does not imply that a call can fail, and `panic` does not imply it
    /// (`div` fails and returns `int`).
    bool fallible = false;
};

/**
 * The value an operator's result has whenever its arguments' types fix it: the shape of a tensor
 * whose type carries its shape, its element count, one of its dimensions. `statics` holds the
 * arguments that are constants (aligned with `types`); `allocator` holds any object the value
 * needs. Nullopt when the types do not fix the result. A folder is how a pass turns such a
 * result into a constant once specialization has put the facts into the types.
 */
using TypeFolder = std::function<std::optional<slot_t>(
    std::span<type::Type *const> types, type::static_args_t statics, mm::IAllocator &allocator)>;

/**
 * The operator's in-place form: an operator with the same parameters and the same result type
 * that may change its first ordinary argument (an array) and returns it, called only on an array
 * no one else holds. It is an implementation-level form and not a name a program can write; the
 * form must change nothing before it can no longer fail.
 */
struct InPlaceForm {
    /// The URI of the form.
    std::string_view form;
    /// The operator the form computes the result of, for the form's own entry; empty for an
    /// operator that is not a form.
    std::string_view plain;
};

/**
 * The operator's result is a value one of its operands already holds instead of a value it makes:
 * the element read answers the slot the array holds, the field read answers the slot the variant
 * holds. Such an operator carries the operand's value (as a JOIN, a GATE and a port carry
 * theirs) and makes no reference of its own.
 */
struct HandsOnValue {
    /// The statement holds only for results that are not arrays: a row of a higher-rank value is
    /// a value of its own.
    bool onlyOutsideArrays = false;
};

/**
 * The types a conversion of the language reads and writes: `:op/itod` is an i32 going into a
 * float. Both are stated where the operator is registered, which is the one place that knows
 * what the operator is; a reader asks the record instead of reading the letters of the URI.
 */
struct ConversionTypes {
    type::TypeCode source = type::TypeCode::Void;
    type::TypeCode target = type::TypeCode::Void;
};

/**
 * What an operator used as a decorator grants (language reference 7.6): an attribute on the
 * nodes written in the function or block it decorates -- the arms, loop bodies and lambdas
 * written there included -- and on no function it calls. Anywhere the decorator cannot be
 * applied while the graph is built it runs like any ordinary operator, and its runtime form may
 * return the function unchanged, because an attribute only allows a library to do something
 * else: a node without it is always correct.
 */
struct AttrGrant {
    /// The attribute key's name; the library registers the key with its kind and its rules.
    std::string key;
    /// The value granted for the with-arguments of one use, given as the constants written
    /// (aligned with the with-parameters); nullopt when the constants do not give one.
    std::function<std::optional<attr::AttrValueId>(std::span<const data::data_ptr_t> with)> value;
};

/**
 * What the compiler does at a call of the operator because of what the operator is, instead of
 * because of a name the compiler knows.
 */
struct CallSpec {
    /// When the call is written with exactly this many ordinary arguments (-1: never), the
    /// compiler appends the position of the call as one more, constant string argument.
    int8_t callSiteAtArity = -1;
    /// The operator calls its function arguments and drops what they return: a function written
    /// in place there whose value nothing takes is refused.
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

/**
 * How an operator is written when it is called by name rather than by a sign: the name, the
 * fewest ordinary arguments a call takes, and how many of its parameters are written in `<...>`.
 * An operator of this kind is a declaration of a function, and the declaration is where its call
 * shape is written.
 */
struct CallForm {
    std::string name;
    /// The fewest ordinary arguments a call takes: a declaration is one overload of several and
    /// may end in a rest parameter, so the count is a lower bound and there is no upper one.
    size_t minArgs = 0;
    /// The parameters a call may state in `<...>`, before the ordinary ones.
    size_t withCount = 0;
    /// Whether the operator's own module stated this shape rather than the shape being read off
    /// a Camel declaration: a module that states its call shape states that a program writes that
    /// call. A derived shape is the *callee's* name, which may be a kernel the module never
    /// exports.
    bool declaredByModule = false;
    /// The declaration this shape was read off is body-less: a kernel the module declares for
    /// its C++ side, whose name is not one a program writes. Such an operator has a name in the
    /// record and no name in the language, which is not the same thing as an operator nobody
    /// declared.
    bool kernelDeclaration = false;
    /// The module that stated this shape exports the name. A body-less declaration is not the
    /// same thing as a private one: the body says where the implementation is, the export says
    /// whether a program may write the name.
    bool exported = false;
};

/**
 * The laws of a binary operator, per element type, and how its operands and its result map to the
 * element type the laws are about. The record's own statement of the algebra: it is
 * `sem::BinaryAlgebra` without the elementwise bit, which the record states once as
 * `Record::elementwise`. (The operator table's `sem::OperatorSemantics` keeps that bit until the
 * table retires; the record is not the table and holds no second copy of it.)
 *
 * A law that holds on a tensor element by element says so by answering with the element type:
 * the laws then hold element by element, and a rewrite may only combine operands of the type of
 * the result (no broadcasting).
 */
struct BinaryLaws {
    /// One law per element type the operator is a law about.
    std::vector<sem::BinaryLaw> laws;
    /// The element type of a value of this type, nullopt when the laws do not speak of it. An
    /// empty function reads a scalar number as its own element type.
    std::function<std::optional<type::TypeCode>(const type::Type *)> elementOf;
};

/**
 * Everything an operator is, stated once.
 *
 * The members are grouped by the question a reader asks, not by the entrance that stated them.
 * A member that is an `Stated<T>` can be stated by either entrance and remembers which; every
 * other member has one default, and that default is the conservative reading (an operator
 * nobody stated anything about is one that may do anything and may fail).
 */
struct Record {
    // ---- Identity -------------------------------------------------------------------------

    OperId id = kNoOper;
    /// `protocol:name@major.minor`: the name the operator keeps when it leaves the process.
    sem::OperatorKey key;

    /// The URI a graph and a program write: `:op/add_l`, `tensor:matmul`.
    std::string uri() const { return key.uri(); }

    // ---- The properties keywords state ------------------------------------------------------

    /**
     * `uses`: what running the operator reads and writes besides the values of its operands, and
     * the only statement the effect is read from. `pure` is this clause empty and is not a
     * second field. Not stated: nobody said, and the operator is read as one that writes the
     * global resource (design principles 3.3).
     */
    Stated<sem::ResourceSpec> uses;

    /**
     * `panic`: whether a call of the operator can fail (design principles 3.9). The Camel
     * entrance states it by writing the keyword, and a declaration that leaves it out states
     * `false`: not writing `panic` promises that no call fails. `true` says a call may fail, and
     * no list of kinds comes with it -- a fault's kind is reported where the fault happens, by
     * the operator that raises it. Not stated: nobody said, and the operator is read as one that
     * may fail. The C++ entrance has no default for it (`CppRegistration`), so a C++ operator
     * always states one of the two answers.
     */
    Stated<bool> panic;

    /**
     * `cost`: how dear one run of the operator is next to the cheapest run of the same target,
     * as the operator's own registration states it (design principles 9.11, "the cost of an
     * extraction"). It is the property a cost function reads instead of the operator's name: the
     * writing of a value costs this number plus the costs of the values it reads, so a target
     * says once, here, that a division is dearer than a multiplication -- and the same number
     * holds on every target that does not state its own. Not stated: nobody said, and one run
     * costs as much as the cheapest one (1.0), which is how a node was counted before this
     * number existed. Read through `relativeCostOf()`.
     */
    Stated<double> relativeCost;

    /**
     * The type of a call. The type rule below answers it for a call whose argument types are
     * known; this is what the declaration stated, which is what a pass reads when it asks about
     * the operator rather than about one call.
     */
    Stated<Signature> signature;

    // ---- What a pass asks about the operator, and nothing else ------------------------------

    /// The type rule: the with- and ordinary argument types in, the type of the result out, or
    /// nullopt when no overload accepts those arguments. Re-run by a pass that changes what is
    /// known about the arguments (binding a shape, folding a shape to a constant).
    Stated<type::resolver_ptr_t> resolver;

    /// The value the result has whenever the argument types fix it.
    Stated<TypeFolder> folder;

    /// The form that computes the same result by changing its first operand.
    std::optional<InPlaceForm> inPlace;

    /// The operator may return one of its operands, changed in place, when the compiler hands it
    /// a value no one else holds.
    bool reusesOperand = false;

    /// The operator's result is a value an operand already holds.
    std::optional<HandsOnValue> handsOn;

    /// The types the operator converts between, when it is a conversion of the language.
    std::optional<ConversionTypes> conversion;

    /**
     * Which of the four shaped operations it is -- a conversion, a copy, a construction, a member
     * read -- or None for an operator that is not one of them. Stated where the operator
     * is registered, and the only place a reader asks: what a node does is the record of the
     * operator it names, and a reader that decoded the letters of a URI instead would hold a second
     * statement of one fact ("never by comparing URI text", operator_table.h).
     *
     * None is the answer for an operator nobody stated a shape for, so no reader has to tell "not
     * one of the four" from "nobody said".
     */
    OperShape shape = OperShape::None;

    /**
     * Which subscript form of the language it is the node of -- an element read at a position, a
     * view of one axis, the integers of a step -- or None (camel/ops/subscript_form.h). Stated
     * where the operator is registered, and the only place a reader asks: a reader that must know
     * how many ordinary operands the form carries, and which one of them is the step, reads this
     * instead of the letters of a URI, and none of the three has an id of its own anywhere.
     *
     * None is the answer for an operator nobody stated a form for, so a reader does not have to
     * tell "not one of the three" from "nobody said".
     */
    SubscriptForm subscript = SubscriptForm::None;

    /// What the operator grants when it is used as a decorator.
    std::shared_ptr<const AttrGrant> grant;

    /// What the compiler does at a call of it because of what the operator is.
    std::optional<CallSpec> callSpec;

    /// How a call of it is written when it is called by name.
    std::optional<CallForm> callForm;

    // ---- What it computes, stated by the declaration ---------------------------------------

    /// The index semantics: the instance of one call, or empty when the operator reads its
    /// operands whole and writes its result whole.
    sem::InstantiateFn indexing;

    /// The reduction laws its instances name, one per element type it reduces over.
    std::vector<sem::ReductionLaw> laws;

    /// The numeric semantics: the class of its rounding, the assumptions it is sensitive to,
    /// the special values it departs from IEEE on, and the algorithm its documentation names.
    sem::NumericSemantics numeric;

    /// The laws of a binary operator, per element type. It states no elementwise bit of its own:
    /// `elementwise` below is the one statement of that.
    std::optional<BinaryLaws> binary;

    /// How the operator calls the function values it receives: which shape, and whether the
    /// calls on different elements may run at once. The shape is not a second statement of
    /// `elementwise`: a map may hand another operand whole, so the shape says which elements the
    /// function is called on, not how an output element depends on an input element.
    std::optional<sem::ElementCalls> elementCalls;

    /// The definition: a function of a library module, written in Camel, that is the operator's
    /// normative meaning. Its absence is not a statement that the operator has no meaning; a
    /// kernel's implementation is not a definition.
    std::optional<sem::Definition> definition;

    // ---- The single statements of what was stated twice ------------------------------------

    /// Output element i depends only on input elements i (after broadcasting). The one statement
    /// of it: an operator that is elementwise says so here and nowhere else.
    bool elementwise = false;

    /// While it runs, the operator calls function values it received as arguments. True for
    /// every operator with an `elementCalls` statement.
    bool callsGraphs = false;

    /// It reads the graphs of the function values it receives instead of only calling them: it
    /// compiles, exports or differentiates them, and so sees the program's own graphs with their
    /// structure.
    bool readsGraphs = false;

    /// The operator is a macro operator (design principles 8): `std::macro` may evaluate a call
    /// of it ahead of time when the arguments are constants, and materialise the result. The
    /// node flag and the graph flag that say so are read from this bit.
    bool macro = false;

    /// Calls of it may be evaluated ahead of time and their results kept as values. False for an
    /// operator that must run where the program writes it although running it has no effect: its
    /// result holds functions no collector traces, or installing it is what the declaration
    /// means. A different question from the effect, which is whether running it changes anything
    /// outside the program.
    bool foldable = true;

    /// The kernel takes the with-arguments of a call after the ordinary ones, as one list: a
    /// fact about the kernel's argument order, not about the language, where a with-argument
    /// keeps its own edge.
    bool withAfterNorm = false;

    // ---- The readings every reader shares ---------------------------------------------------

    /// The effect, read from `uses` and from nothing else (docs/design/op-semantics 4.2): the
    /// empty clause is pure, a clause of the program's own variables and objects is state-only,
    /// and anything else -- an access to a resource outside the program, a declared external
    /// effect, an operator nobody stated a clause for -- is effectful. Written once, in the
    /// record's implementation.
    sem::Effect effect() const;

    /// The operator touches no resource at all: its clause is stated and empty.
    bool pure() const { return uses.stated() && uses.value.none(); }

    /// A call of it may fail: `panic` is stated true, or nobody stated it.
    bool mayFault() const { return !panic.stated() || panic.value; }

    /// How dear one run of it is, if the operator says: `std::nullopt` when nobody stated a cost,
    /// which is a different answer from a stated 1.0 and is kept different here. A target that
    /// needs a number for an unstating operator picks its own -- and says that it is picking one,
    /// at the place that picks it (`graph.relative_cost`), rather than reading its policy as the
    /// record's statement. Writing the default here read a silence as a value: `cost` stated 1.0
    /// and `cost` never stated were the same double.
    std::optional<double> relativeCostStated() const {
        return relativeCost.stated() ? std::optional<double>(relativeCost.value) : std::nullopt;
    }

    /// Whether running it is visible outside the program, which is what orders two effects of
    /// one resource and keeps them where the program wrote them.
    bool external() const;
};

/**
 * The registry: the records, by OperId, and the two entrances that fill them.
 *
 * The registry never holds a record's properties itself; it holds the records. A statement that
 * names an item already stated by the other entrance is compared item by item and refused when
 * they differ, so the two entrances cannot mean two different operators.
 */
class CppRegistration;

class Registry {
  public:
    static Registry &instance();

    // ---- Identity --------------------------------------------------------------------------

    /// The id of `key`'s operator, creating a record for it when it is new.
    OperId intern(const sem::OperatorKey &key);
    /// The id of the operator `uri` names, creating a record for it when it is new.
    OperId intern(std::string_view uri);

    /// The id of `key` when the registry has a record for it, nullopt otherwise.
    std::optional<OperId> lookup(const sem::OperatorKey &key) const;
    std::optional<OperId> lookup(std::string_view uri) const;

    /// The record of `id`, or nullptr for `kNoOper`. Lock-free; the pointer stays valid until
    /// the process exits.
    const Record *find(OperId id) const;
    const Record *find(std::string_view uri) const;

    /// The id of the operator a key read back from outside the process names (a module cache):
    /// nullopt when the operator is unknown or its version cannot satisfy `key`.
    std::optional<OperId> bind(const sem::OperatorKey &key) const;

    /// The number of records, `kNoOper` included.
    std::uint32_t size() const;

    // ---- Entrance 1: a Camel declaration, compiled in --------------------------------------

    /**
     * States what a Camel declaration compiled to, at module load time. Every item the
     * declaration states is merged into the record; an item the other entrance already stated
     * must be equal, else std::invalid_argument names the operator, the item and both values.
     */
    void declare(OperId id, const Record &statement);

    /**
     * States the shape of the four that have one, at module load time. It is a statement
     * of one item rather than a `Record` merge because it is the one item no Camel declaration can
     * state: the four are written in `prelude.cml` as functions and the front end builds them as
     * operators, so "this one is a construction" is the C++ side's to write, where the operator is
     * registered with the executor that runs it.
     *
     * Before the record is completed a later statement stands (the bare-value convention of
     * `CppRegistration`'s seven setters); a completed record refuses a statement that disagrees
     * with the one it holds, naming the operator and both shapes.
     */
    void stateShape(OperId id, OperShape shape);

    /**
     * States which subscript form the operator is (camel/ops/subscript_form.h), from the module
     * that registers it with its implementation. The same rule as `stateShape`: a later statement
     * stands before the record is completed, and a completed record refuses one that disagrees.
     */
    void stateSubscriptForm(OperId id, SubscriptForm form);

    // ---- Entrance 2: the C++ registration API ----------------------------------------------

    /**
     * Registers the operator `registration` names, at module load time, with the same checks as
     * declare(). Returns the id the operator now has.
     */
    OperId registerOperator(CppRegistration &registration);

    // ---- Completion ------------------------------------------------------------------------

    /**
     * Checks a record whose entrances are all in and publishes it: the two entrances' `panic`
     * against each other, purity against the accesses, an in-place form against its plain operator,
     * and an elementwise algebra against the bit that states it. Throws std::invalid_argument
     * naming the operator and the item that does not hold; a refused record is left as it was.
     *
     * A published record is never changed again: everything a program can ask about an operator
     * is fixed before its first call is compiled, so the graph's own content and the facts
     * computed from it cannot disagree with the record they were computed from.
     */
    void complete(OperId id);

    /// Incremented whenever a record is completed or replaced. A fact computed from the records
    /// stamps this, and is stale when it changed.
    std::uint64_t epoch() const;

    /**
     * A member of its own, with the existing declarations unchanged: the implementation this
     * process runs for `id`, or nullptr when the operator has none or `id` is `kNoOper`. It is what
     * a `CppRegistration` handed over with `kernel(void *)`, kept beside the record and not in it
     * -- the note on `CppRegistration` says a target registers its own implementations against the
     * same OperId and they do not enter the record. Added because `kernel(void *)` had nowhere to
     * put the pointer it was given and no reader to hand it to, and a kernel nothing can read is
     * the same thing as no kernel at all.
     */
    void *kernel(OperId id) const;

  private:
    Registry();
};

/**
 * The C++ entrance: one operator, stated entirely in C++ and registered without a Camel
 * declaration.
 *
 * ```cpp
 * // a library author's module, at load time
 * camel::ops::CppRegistration reg("mylib:scale", false);   // panic is not optional
 * reg.pure()
 *    .resolver(myScaleRule)
 *    .signature(myScaleType)
 *    .numeric(myScaleNumeric)
 *    .kernel(myScaleKernel);
 * camel::ops::Registry::instance().registerOperator(reg);
 * ```
 *
 * Every setter states one item and returns the registration, so an operator is one expression.
 * Two setters of the same item are refused (that is one entrance stating one item twice, not two
 * entrances agreeing). The kernel is the operator's implementation for this process; a target
 * registers its own implementations against the same OperId and they do not enter the record.
 */
class CppRegistration {
  public:
    /**
     * `panic` is a parameter, not a field with a default: `true` states that a call of the
     * operator may fail and `false` that none does. There is no third answer for a C++ operator,
     * so a registration that forgot to think about failure does not compile.
     */
    CppRegistration(std::string_view uri, bool mayFault);

    /**
     * A registration that states only the items its setters name: no `panic`, so the operator
     * must already be registered, and the items stated here are merged into its record (a target
     * restating a cost, a pass stating what nobody had stated). `complete` still refuses a
     * record whose items disagree, and stating an item the C++ entrance already stated is
     * refused the same way.
     */
    explicit CppRegistration(std::string_view uri);

    /// `uses` is the empty access list: running the operator touches nothing but its operands.
    CppRegistration &pure();
    /// `uses`: the accesses it lists. `pure()` and this are the same item.
    CppRegistration &uses(std::span<const sem::ResourceAccess> accesses);
    /// The access list is the one of the function value the operand `which` names.
    CppRegistration &usesFunctionIn(bool isWith, size_t which);

    /// The type of a call of it, as the declaration states it; `fallible` is `T!`.
    CppRegistration &signature(const type::FunctionType &type, bool fallible = false);
    /// The type rule: the type of a call whose argument types are known.
    CppRegistration &resolver(type::resolver_ptr_t rule);

    /// `cost`: how dear one run of it is, as this target measures it. A number greater than
    /// zero; 1.0 is the cheapest run and is what an operator whose registration states nothing
    /// is read as (`Record::relativeCostOf`).
    CppRegistration &relativeCost(double cost);

    CppRegistration &folder(TypeFolder folder);
    CppRegistration &inPlaceForm(std::string_view form);
    CppRegistration &reusesOperand(bool reuses = true);
    CppRegistration &handsOnOperandValue(bool onlyOutsideArrays = false);
    CppRegistration &conversion(ConversionTypes types);
    CppRegistration &grant(AttrGrant grant);
    CppRegistration &callSpec(CallSpec spec);
    CppRegistration &callForm(CallForm form);

    /// Output element i depends only on input elements i. The one statement of it, and the way a
    /// binary operator whose laws hold element by element says so.
    CppRegistration &elementwise();
    /// It calls the function values it receives; `calls()` states `callsGraphs` with it.
    CppRegistration &calls(sem::ElementCalls shape);
    CppRegistration &readsGraphs();
    CppRegistration &macro();
    CppRegistration &foldable(bool foldable = true);
    CppRegistration &withAfterNorm();

    CppRegistration &indexing(sem::InstantiateFn instantiate);
    CppRegistration &laws(std::vector<sem::ReductionLaw> laws);
    CppRegistration &numeric(sem::NumericSemantics numeric);
    CppRegistration &algebra(BinaryLaws laws);
    CppRegistration &definition(sem::Definition definition);

    /// The implementation this process runs for the operator. Not part of the record: a target
    /// registers its own against the same id.
    CppRegistration &kernel(void *function);

    /// The items stated so far, which `registerOperator` merges into the record.
    const Record &stated() const;

  private:
    Record record_;

    /**
     * A field of its own, with the existing declarations unchanged: the pointer `kernel(void *)`
     * was given. It is not a record item, so it is not in `record_`; `Registry::registerOperator`
     * is the one reader and keeps it beside the record, per OperId, for the process' life.
     */
    void *kernel_ = nullptr;

    friend class Registry;
};

} // namespace camel::ops
