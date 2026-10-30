// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Values with several writings (docs/design/graph-store 3.1, 3.4): the equivalences asserted
 * between the nodes of one graph, and the classes they make.
 *
 * A node already is a value; several writings are several nodes declared equivalent. What is
 * stored of a graph is the log of the equivalences (WritingUnion), nothing else; the classes
 * are derived from it, by WritingStore, whenever a draft needs them.
 *
 * An equivalence holds under the permissions it names (`perms`, 3.4): a rule that reassociates a
 * sum needs the numeric permission of that, an exact one needs none. The classes are therefore
 * kept per context, a set of permissions: the layer of a context S joins the nodes of every
 * equivalence whose permissions are within S. A node is looked at in the context of its own
 * permissions. A graph has few contexts (the default one, a block that grants more), at most
 * kMaxLayers.
 *
 * Order is part of the result: the root of a class is its smallest node, the members are listed
 * ascending, and nothing here depends on the order of a hash table.
 */

#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::core::context {
class Context;
}

namespace camel::runtime {

class GraphTransaction;

/// Told which nodes change class when two classes join (the nodes of the smaller class).
class WritingObserver {
  public:
    virtual ~WritingObserver()                                                    = default;
    virtual void joined(camel::core::sem::PermissionMask context, std::span<const gc_node_ref_t> moved) = 0;
};

class WritingStore {
  public:
    static constexpr size_t kMaxLayers = 4;

    WritingStore() = default;
    /// The classes of a log.
    explicit WritingStore(std::span<const WritingUnion> log);

    /// Asserts that `a` and `b` are the same value in every context that has `perms`. Returns
    /// true when that changed the classes of the context `perms`.
    bool merge(
        gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms = 0, uint16_t rule = 0, uint8_t flags = 0,
        WritingObserver *observer = nullptr);

    /// The smallest node of the class of `node` in the context `context`.
    gc_node_ref_t find(camel::core::sem::PermissionMask context, gc_node_ref_t node);
    /// A node of the class that stays the same until the class joins another: for keys that
    /// name a class (find gives the smallest node, which changes less often but costs a lookup).
    gc_node_ref_t canon(camel::core::sem::PermissionMask context, gc_node_ref_t node);
    /// The nodes of the class ascending; empty for a node alone in its class.
    std::span<const gc_node_ref_t> members(camel::core::sem::PermissionMask context, gc_node_ref_t node);
    bool same(camel::core::sem::PermissionMask context, gc_node_ref_t a, gc_node_ref_t b) {
        return find(context, a) == find(context, b);
    }
    /// True when no equivalence was asserted.
    bool empty() const { return log_.empty(); }
    std::span<const WritingUnion> log() const { return log_; }
    /// The contexts that have a layer, ascending.
    std::vector<camel::core::sem::PermissionMask> contexts() const;
    /// The roots of the classes of more than one node in the context, ascending.
    std::vector<gc_node_ref_t> classRoots(camel::core::sem::PermissionMask context);

  private:
    struct Layer {
        camel::core::sem::PermissionMask context = 0;
        // Union by size; `smallest` is read at the root of a class.
        std::vector<gc_node_ref_t> parent;
        std::vector<gc_node_ref_t> size;
        std::vector<gc_node_ref_t> smallest;
        // root -> members; a node alone in its class has no entry. The smaller list is appended
        // to the larger when classes join, so a list is sorted when it is asked for.
        struct Members {
            std::vector<gc_node_ref_t> nodes;
            bool sorted = true;
        };
        std::unordered_map<gc_node_ref_t, Members> members;
    };
    Layer &layer(camel::core::sem::PermissionMask context);
    static gc_node_ref_t findIn(Layer &layer, gc_node_ref_t node);
    static void ensure(Layer &layer, gc_node_ref_t node);
    static bool unionIn(Layer &layer, gc_node_ref_t a, gc_node_ref_t b, WritingObserver *observer);

    std::vector<WritingUnion> log_;
    // Ascending by context.
    std::vector<Layer> layers_;
};


/// What a cost model may look at: the draft, and the context for the facts about the program
/// (a back end's model reads the shapes there).
struct CostContext {
    const GraphDraft &draft;
    camel::core::context::Context *context = nullptr;
};

/// What it costs to compute a value one way (graph store design 3.6). The cost of a writing is
/// its own cost plus the costs of the values it reads, so the writing chosen for a value never
/// reads that value itself. Replaceable: the target decides (Tiles registers one for the GPU).
class CostModel {
  public:
    virtual ~CostModel() = default;
    /// The cost of computing `node` from the values it reads; positive.
    virtual double nodeCost(const CostContext &context, gc_node_ref_t node) const = 0;
    /// The cost of the cheapest writing of the value `node` computes, given `own` (its
    /// `nodeCost`) and the costs of the cheapest writings of the values it reads, in the order it
    /// reads them. The default is the sum, which makes a writing cost its own plus its inputs';
    /// a model written as a function of the operator and the costs of its inputs (egg's
    /// `CostFunction`) overrides it.
    virtual double totalCost(
        const CostContext &context, gc_node_ref_t node, double own,
        std::span<const double> inputs) const {
        (void)context;
        (void)node;
        double total = own;
        for (double cost : inputs) {
            total += cost;
        }
        return total;
    }
};

/// The models by name: "nodes" (every node costs one), "weighted" (a division costs more than a
/// product, which costs more than a sum; constants and parameters are nearly free), and what a
/// module registers.
void registerCostModel(std::string name, std::shared_ptr<const CostModel> model);
std::shared_ptr<const CostModel> findCostModel(const std::string &name);

/// The cost model the pipeline extracts by when a pass that consumes a program does not accept
/// the state with several writings (`src/execute/pass.cpp`), and the one the dumps mark the
/// chosen writing by: one name, so a picture shows the choice the pipeline would itself make.
inline constexpr const char *kWritingsCostModel = "weighted";

/// A term of a rule (rewrite design 2.2, 2.3). On the left it is matched against the writings
/// of the values; on the right it is built. A variable stands for a value (the same variable
/// twice: the same value); `constOnly` ones only match a value that has a constant writing, and
/// `type` is the type the parameter was declared with, so a `const c: T` reads only a constant
/// the type of the pattern accepts (rewrite design 2 (second version) 1: the matcher checks the
/// declared type, and no rule guards `isI32`-style).
struct Pattern {
    enum class Kind : uint8_t { Var, Constant, Op, Compute };
    Kind kind        = Kind::Var;
    uint32_t var     = 0;
    bool constOnly   = false;
    camel::core::OperId oper = camel::core::kNoOper;
    // Constant: the bits and the type; Var, Op, Compute: the type of the value (null: any type).
    // `value` is mutable because the patterns of a rule are compiled once and a collection during
    // a rule condition (a safepoint) can move the constant it holds.
    mutable slot_t value               = 0;
    camel::core::type::Type *type      = nullptr;
    std::vector<Pattern> args;
    // Compute (right side only): the constant it is, from the constants of its arguments.
    std::function<slot_t(std::span<const slot_t>)> compute;
};
Pattern patVar(uint32_t var, camel::core::type::Type *type = nullptr);
Pattern patConst(uint32_t var, camel::core::type::Type *type = nullptr);
Pattern patLiteral(slot_t value, camel::core::type::Type *type);
Pattern patOp(camel::core::OperId oper, std::vector<Pattern> args, camel::core::type::Type *type = nullptr);
Pattern patCompute(
    camel::core::type::Type *type, std::function<slot_t(std::span<const slot_t>)> compute,
    std::vector<Pattern> args);

class DraftWritings;

/// The classes of a graph in the state with several writings, and the writing extraction would
/// take for each of them: what a dump draws. Everything here is read, never written: the
/// classes come from a decoded copy of the graph, which is dropped again, and the choice comes
/// from the one cost fixed point `extract` applies, so a dump never guesses what extraction does.
struct GraphWritings {
    /// One entry per value that has more than one writing, by ascending smallest writing.
    struct Value {
        /// The smallest writing of the value (the class's root).
        gc_node_ref_t root = kInvalidNodeRef;
        /// Every writing of the value, ascending.
        std::vector<gc_node_ref_t> writings;
        /// The writing extraction would take, and what computing the value that way costs.
        gc_node_ref_t chosen = kInvalidNodeRef;
        double cost          = 0.0;
    };
    std::vector<Value> values;
    /// For a node that is a writing of one of `values`, its index in `values`.
    std::unordered_map<gc_node_ref_t, size_t> valueOf;
    /// Every node of the graph (the values with one writing too): the smallest writing of the
    /// value it is a writing of, the model's cost of that writing itself, and the cost of
    /// computing the value the cheapest way.
    struct Node {
        gc_node_ref_t root = kInvalidNodeRef;
        double own         = 0.0;
        double value       = 0.0;
    };
    std::unordered_map<gc_node_ref_t, Node> nodes;
    bool empty() const { return values.empty(); }
};

/// The classes of `graph` (a graph of `program` in the state with several writings) under the
/// cost model `model`, read without editing the program: an executable graph, or a graph whose
/// draft cannot be decoded, has no classes and gives an empty result.
GraphWritings graphWritings(
    const Program &program, const GCGraph *graph, const CostModel &model,
    camel::core::context::Context *context = nullptr);

/// What a cost model makes of the writings of a draft: which value every living node is a writing
/// of, and which writing the extraction would take for it (graph store design 3.6). This is the
/// fixed point `extract` applies — the cost of a writing is its own plus the cost of the values it
/// reads, repeated until nothing gets cheaper, the smallest node winning a tie — so what
/// extraction would choose is read from one place, never guessed a second time (a dump that marks
/// the chosen writing).
struct WritingChoice {
    /// Per node slot, the root of the class the node is a writing of (kInvalidNodeRef: no such
    /// node alive).
    std::vector<gc_node_ref_t> classOf;
    /// Per node slot, 1 when the value of the node has more than one writing.
    std::vector<uint8_t> multi;
    /// Per node slot, the writing chosen for the class of the node (kInvalidNodeRef: no writing
    /// of that value is known to finish, e.g. every one of them reads the value itself).
    std::vector<gc_node_ref_t> chosen;
    /// Per node slot, the cost of computing the value that way (infinity when none is known).
    std::vector<double> cost;
    /// Per node slot, the model's cost of the writing itself, apart from what it reads.
    std::vector<double> own;
};

/// Thrown by a computed constant of a rule that cannot be computed (the operator faults): the
/// rule is not applied at that match.
class RuleSkipped : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// The terms an associative-commutative left side read but did not name, and the one variable of
/// the left side that stands for them (rewrite design 6): the matcher reads `a + b + c` as the
/// multiset of its terms, so a pattern that names fewer terms than the program wrote leaves the
/// rest to a variable instead of forcing the program into a shape it does not have.
struct MatchRemainder {
    /// The variable that takes the terms no pattern term matched; `set` is false when the left
    /// side named every term and `terms` is empty.
    bool set      = false;
    uint32_t var  = 0;
    /// The operator the terms are terms of: the right side builds their chain with it.
    uint32_t oper = 0;
    /// The terms, ascending by identity.
    std::vector<gc_node_ref_t> terms;
};

/// What a match of a rule's left side found: the value each variable stands for.
struct Match {
    DraftWritings &writings;
    gc_node_ref_t root;
    std::span<const gc_node_ref_t> vars;
    /// What the variable `remainder.var` stands for: the chain of the terms the left side did not
    /// name, built with `remainder.oper`. Reading it in the condition or the right side is what
    /// keeps a law from dropping a term (the engine refuses a match that would drop an effect,
    /// design principles 9.7).
    MatchRemainder remainder;
    /// The constant a variable's value has, if it has one.
    std::optional<std::pair<slot_t, camel::core::type::Type *>> constant(uint32_t var) const;
};

/// `lhs => rhs`: whenever the left side matches, the right side is another writing of the value
/// of the node it matched. `needs` is the numeric permissions the value must have (the
/// policy's and the node's). A rule that holds both ways is two rules.
struct Rule {
    std::string name;
    uint16_t id                            = 0;
    camel::core::sem::PermissionMask needs = camel::core::sem::kNoPermissions;
    Pattern lhs;
    Pattern rhs;
    std::function<bool(const Match &)> when;
    /// The rule comes from a `<=>`: it holds both ways, which only a saturation can use.
    bool twoWay = false;
};

/// Why a rule does or does not fire at one node, for `why_not` (rewrite design 8.7). `detail` is
/// one sentence: the first term of the left side that does not match and what the program has
/// there, the permission the value lacks, the condition that is false with the values it read, or
/// the rewrite that did fire.
struct RuleMiss {
    enum class Kind : uint8_t { Fired, NotPart, Needs, Match, Condition, Blocked };
    Kind kind = Kind::Fired;
    std::string detail;
};

/// Relocates the constant values the patterns of `rules` hold. The objects themselves are kept
/// alive by the rule's Program; a collection during a rule condition can move them, and the
/// patterns hold copies of their slots.
void traceRuleConstants(
    std::span<const Rule> rules, const camel::core::rtdata::Object::RefRelocator &relocate);

/// What a saturation may spend; the result does not depend on time. Rounds are passes over all
/// the rules. The run stops, without an error, when a budget is used up.
struct Budget {
    size_t rounds       = 8;
    size_t newWritings  = 1u << 20;
    size_t unions       = 1u << 22;
};

struct RewriteStats {
    size_t rounds  = 0;
    /// The nodes the matcher looked at, over all rounds: the alive nodes that take part in a
    /// rewrite.
    size_t visited = 0;
    size_t applied = 0;
    size_t erased  = 0;
    bool fixpoint  = false; // a round applied nothing
};

/// One rewrite the engine made, attributed to the rule that made it (rewrite design 8.7:
/// `explain`). The nodes are named by their id in the draft and by the text they had when the
/// rewrite was made: a node the rewrite erased has no later text to read, and a node the draft
/// goes on to renumber would lose the id the step was about.
///
/// A log is kept only when the caller asks for it (`run`'s `explain` parameter). `rewrite`
/// records nothing when it is given no log, so the plain engines pay nothing for attribution
/// nobody reads.
struct RewriteStep {
    /// The source name of the rule that fired, and the id its compiled rules carry.
    std::string rule;
    uint16_t ruleId = 0;
    /// The round of the graph's rewrite the step belongs to, counting from 1.
    size_t round = 0;
    /// The graph the rewrite was made in; the application that drove the graph fills these in.
    std::string graph;
    GraphId graphId = kInvalidGraphId;
    /// The node the left side matched, then the nodes its variables were bound to, in variable
    /// order and without repeats.
    std::vector<gc_node_ref_t> matched;
    std::vector<std::string> matchedText;
    /// The node the right side was built as (an existing one or a new one).
    gc_node_ref_t produced = kInvalidNodeRef;
    std::string producedText;
    /// The nodes the rewrite left unread and the engine erased, the replaced one first.
    std::vector<gc_node_ref_t> erased;
    std::vector<std::string> erasedText;
};

/// The attributed rewrites of one application, in the order the engine made them.
using RewriteLog = std::vector<RewriteStep>;

struct SaturateStats {
    size_t rounds       = 0;
    size_t matches      = 0;
    size_t newWritings  = 0;
    size_t unions       = 0;
    bool saturated      = false; // a round found nothing new
    // Where the time went, for measuring (nothing in the engine reads them).
    double findSeconds  = 0;
    double applySeconds = 0;
};

struct ExtractStats {
    size_t classes  = 0; // values with more than one writing
    size_t rewired  = 0; // nodes whose inputs were changed to other writings
    size_t erased   = 0; // writings not chosen
};

/// True when some graph of the program has values with several writings.
bool hasWritings(const Program &program);

/// The program with one writing per value in every graph that has several, chosen by `model`
/// (the program itself when it has none). `detached` for a caller that holds the result as a
/// value (the Camel `extract`): the commit then leaves the context's program as it is (design
/// principles 9.1); the pipeline's own extraction installs it.
/// `root`, when given, limits the extraction to the graphs reachable from it (the function a
/// Camel `extract` is applied to), and the committed program is the one of that function.
const Program *extractWritings(
    const std::shared_ptr<camel::core::context::Context> &context, const Program *program,
    const CostModel &model, bool detached = false, GCGraph *root = nullptr);

/// The writings of the values of a draft: the classes (WritingStore) and the congruence closure
/// over them. Nodes that compute the same thing from the same classes are one value, so
/// merging two values merges whatever is built from them; nothing is rewritten, no input of a
/// node ever changes, and the graph stays acyclic node by node.
///
/// Only a node that is a pure function of its inputs takes part: a constant, a conversion, an
/// access, a pure operator, not written in place and not reading a variable. Every other node is
/// alone in its class. A control input does not keep a node out: it orders the computation, it
/// does not make the value different (design principles 3.3, 3.4), and extraction gives the
/// writing that stays the control inputs of every writing of its value. Two nodes are congruent
/// only in the same region and with the same numeric permissions (design principles 7.5,
/// op-semantics 6.4).
class DraftWritings : private WritingObserver {
  public:
    explicit DraftWritings(
        GraphDraft &draft,
        camel::core::sem::NumericPolicy policy = camel::core::sem::NumericPolicy::relaxed());
    /// The same, for a draft a transaction owns. The writings record the transaction, so the
    /// draft has an owner that is not a handle: this is what a handle can name once it stops
    /// naming a bare draft. Nothing reads `transaction()` yet; the two forms behave the same.
    explicit DraftWritings(
        GraphTransaction &transaction,
        camel::core::sem::NumericPolicy policy = camel::core::sem::NumericPolicy::relaxed());
    ~DraftWritings();
    DraftWritings(const DraftWritings &)            = delete;
    DraftWritings &operator=(const DraftWritings &) = delete;

    /// Writes the equivalences back to the draft (GraphDraft::setWritings), which keeps them
    /// until it is encoded.
    void save() { draft_.setWritings(std::vector<WritingUnion>(store_.log().begin(), store_.log().end())); }
    GraphDraft &draft() { return draft_; }
    /// The transaction the draft belongs to, or null when these writings were made from a bare
    /// draft (a test that builds one).
    GraphTransaction *transaction() { return transaction_; }
    WritingStore &store() { return store_; }

    /// Indexes every node of the draft and merges the congruent ones.
    void build();
    /// Asserts that `a` and `b` are the same value under `perms`, then closes over congruence.
    /// Returns true when that changed the classes.
    bool assertEqual(gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms = 0, uint16_t rule = 0);
    /// The same without the closure over congruence, which the caller does with rebuild() once
    /// it has made all its merges (a saturation round).
    bool assertEqualDeferred(
        gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms = 0, uint16_t rule = 0);
    /// The node that computes this, in the class it belongs to: an existing one, or a new one
    /// with the inputs `norm` and `with` (any members of their classes). A node that does not
    /// take part in congruence is always new.
    gc_node_ref_t
    lookupOrAdd(const DraftNodeInit &init, std::span<const gc_node_ref_t> norm,
                std::span<const gc_node_ref_t> with = {});
    /// Applies the rules to the writings, adding writings, until nothing new is found or the
    /// budget is used up. The same rules, graph and budget give the same writings.
    // (a saturation matches the nodes whose permissions are the root's own)
    SaturateStats saturate(std::span<const Rule> rules, const Budget &budget = {});
    /// Applies the rules one way, in place, on a draft with one writing per value (rewrite
    /// design 3, `rewrite`): in a round, the nodes in order, at each the rules in the order of
    /// the list, the first that matches (and whose condition holds) replaces the node's value by
    /// its right side, the nodes nothing reads any more are erased. Rounds are repeated until a
    /// round applies nothing or `maxRounds` is reached. A rule that holds both ways is refused
    /// (std::invalid_argument).
    ///
    /// `log`, when given, collects one `RewriteStep` per rewrite, in the order they were made
    /// (rewrite design 8.7). It is also where the erased nodes are noted, before the draft
    /// forgets them. With no log the recording is skipped and nothing is allocated for it.
    RewriteStats rewrite(
        std::span<const Rule> rules, size_t maxRounds = 16, RewriteLog *log = nullptr);
    /// Why `rule` does or does not fire at `node`, as one sentence for `why_not` (rewrite design
    /// 8.7): it matches the left side the way the engine does — an operator's declared
    /// commutativity is tried when the written order fails — but does not rewrite, and reports the
    /// first term that does not match with the value the program has there, the permission the
    /// value lacks, or the condition's verdict on the values it matched.
    RuleMiss whyNot(const Rule &rule, gc_node_ref_t node);
    /// The constant node `value` of `type` in this region with these permissions: the one there
    /// is, or a new one.
    gc_node_ref_t constantNode(slot_t value, camel::core::type::Type *type, camel::core::sem::PermissionMask perms, uint32_t region);
    /// The constant (bits and type) a writing of the value of `node` is, if any is.
    std::optional<std::pair<slot_t, camel::core::type::Type *>> constantOf(gc_node_ref_t node);
    /// Closes the classes over congruence: nodes whose inputs changed class are looked up again.
    void rebuild();

    /// The smallest node of the value `node` is a writing of, in the context of its own
    /// permissions.
    gc_node_ref_t find(gc_node_ref_t node);
    /// The writings of the value `node` is one of, ascending (empty: the only one).
    std::span<const gc_node_ref_t> writingsOf(gc_node_ref_t node);
    /// The text of a node of the draft, as a proof prints it: `%id kind operator: type` (and the
    /// value of a static constant, bounded). The id it names is the draft's.
    std::string textOf(gc_node_ref_t node);
    /// Picks one writing for every value, by the cost model, and leaves the draft in the
    /// executable state: the inputs of the nodes that stay read the chosen writings, the other
    /// writings are erased, the equivalences are dropped. Throws when a value that is read has
    /// no writing that does not read itself.
    ExtractStats extract(const CostModel &model, camel::core::context::Context *context = nullptr);
    /// The classes of the draft and the writing the cost model would choose for every value: the
    /// same fixed point `extract` picks its writings by, read without changing anything (a dump
    /// marks the writing extraction would take). The classes are closed over congruence
    /// first, so this counts on the store being up to date.
    WritingChoice
    choiceByCost(const CostModel &model, camel::core::context::Context *context = nullptr);
    /// How many merges congruence closure made.
    size_t congruentMerges() const { return congruentMerges_; }

  private:
    struct Index;
    /// 1: the node takes part in congruence and in every match — it computes its value from its
    /// data inputs; 2: it does not. A control input takes no part in this question: it orders the
    /// node, it does not make the value different (design principles 3.3, 3.4).
    uint8_t shapeOf(gc_node_ref_t node);
    bool writtenInPlace(gc_node_ref_t node);
    bool takesPart(gc_node_ref_t node);
    /// Whether `node` and everything it is computed from is a pure computation: what a rewrite
    /// drops has to be (design principles 9.7; rewrite design 8).
    bool pureComputation(gc_node_ref_t node);
    /// Whether the right side of `rule` stops reading a value of the left side that is not a pure
    /// computation, which would drop an effect or a computation ordered after one. `remainder` is
    /// the value the variable taking the terms no pattern term named stands for.
    bool dropsOrdered(
        const Rule &rule, std::span<const gc_node_ref_t> bind, const MatchRemainder &remainder);
    camel::core::sem::PermissionMask contextOf(gc_node_ref_t node) const;
    struct Item {
        const Pattern *pattern;
        gc_node_ref_t node;
    };
    /// Matches `work[at..]`; `waits`, when given, collects the control inputs of every operator
    /// term matched (the root included), so a rewrite can give them to the writing it builds.
    /// Returns whether any match was accepted (whether `emit` returned true).
    ///
    /// A binary term whose operator's semantics declare it commutative for the node's element
    /// type is read in the written order first. The other order is tried only when the written
    /// order produced no accepted match, so a law sees its operands where it wrote them, and the
    /// replacement (which is built from the law's right side) keeps the written order when one
    /// exists. The fact is read per operator and per element type from
    /// `sem::OperatorSemantics::binary` (docs/design/op-semantics 7.4); a term whose operator does
    /// not declare it is never commuted, since a law is about values and the matcher may not
    /// guess (design principles 9.5).
    ///
    /// `commuted` says whether that fallback may run for this rule. A `<=>` rule already states both
    /// directions, and a saturation applies its reverse as a rule of its own; the fallback is what
    /// a law that wrote only one order needs, so a two-way rule passes false. (Trying it as well
    /// only doubled the matches of the rearranging laws: `rules::saturate_all` went from about
    /// half a second to past the 20 s timeout.) A one-way rule (`=>`) has no reverse and
    /// passes true.
    ///
    /// An operator whose own declaration says exchanging its operands and regrouping its chain
    /// give the same value (`BinaryLaw.commutative` and `exactAssociative`, per element type) is
    /// read one way further: the chain is flattened into the multiset of its terms and the
    /// pattern's terms are matched against it in any order, the terms no pattern term names going
    /// to the single variable `remainder` reports. Only the bit-exact association is read, so a
    /// float sum is never regrouped here (`reassociate` and its rules are that path), and the
    /// written order is still tried first.
    bool matchFrom(
        camel::core::sem::PermissionMask context, std::vector<Item> &work, size_t at, std::vector<gc_node_ref_t> &bind,
        std::vector<uint8_t> &bound, std::vector<gc_node_ref_t> *waits, bool commuted,
        const std::function<bool()> &emit, MatchRemainder *remainder = nullptr);
    gc_node_ref_t build(
        const Pattern &pattern, const Match &match, const DraftNodeInit &base);
    void enqueue(gc_node_ref_t node);
    void joined(camel::core::sem::PermissionMask context, std::span<const gc_node_ref_t> moved) override;
    void index(gc_node_ref_t node);

    GraphDraft &draft_;
    /// The transaction `draft_` belongs to, when these writings were made from one.
    GraphTransaction *transaction_ = nullptr;
    camel::core::sem::PermissionMask matchPerms_ = 0;
    bool greedy_        = false; // rewrite(): nodes are replaced, so nothing is looked up by key
    camel::core::sem::NumericPolicy policy_;
    WritingStore store_;
    std::unique_ptr<Index> index_;
    std::vector<gc_node_ref_t> pending_;
    std::vector<bool> queued_;
    size_t congruentMerges_ = 0;
};

/// A fact about a value with several writings: the join of what each writing gives (an
/// analysis of the rewrite design, 4.2: more writings only make the fact more exact or leave it
/// as it is). `on` gives the fact of one writing from the facts of the values it reads.
template <typename Fact, typename On, typename Join>
Fact classFact(DraftWritings &writings, gc_node_ref_t node, On &&on, Join &&join) {
    const auto members = writings.writingsOf(node);
    if (members.empty()) {
        return on(node);
    }
    Fact fact = on(members.front());
    for (size_t i = 1; i < members.size(); ++i) {
        fact = join(fact, on(members[i]));
    }
    return fact;
}

} // namespace camel::runtime

