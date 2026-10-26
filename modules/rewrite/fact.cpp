// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `fact` (rewrite design 7.5): the evaluation of a fact over a value and its data dependencies,
 * and the fact kind the result is stored under.
 *
 * A value's fact is read from the cases of the declaration in the order they are written: the
 * first case pattern that matches the node gives the fact from the matched nodes (or the value of
 * a `const` parameter). A value no case matches takes the fact of its operands, merged by `join`
 * in order; a value with no operands takes `initial`. A value that is its own dependency (a loop)
 * takes the join of what has been computed so far and `initial` — the first version has no
 * widening and says so by returning `initial`.
 *
 * A declaration with parameters (`fact pair(a: Node, b: Node): bool`) is queried with the
 * parts of a value instead of the value itself: a rule's condition `pair(x, y)` hands over the
 * nodes its pattern variables name, and the fact is the one of the value those parts belong to
 * — the value a user of the first part with exactly these parts as its operands, one of the
 * cases matching it. This is how a condition reads the fact of the compound it is rewriting
 * without a syntax naming the whole match (`node_of` is how it names the node of a `const`
 * parameter).
 *
 * A declaration's `with` arguments are part of what a fact is (rewrite design 7.5): the argument written at the query is read here, and the description, the fact kind
 * and the stored value are all keyed by "the name and the arguments" (`user.fact.range<5>`), so
 * `range<3>(x)` and `range<5>(x)` are two facts, each computed and kept on its own.
 *
 * The operands of a commutative operator are not an order: a rule's match may have paired them the
 * other way (`const c` pins which operand it is, so the matcher tries the exchange), and the parts
 * then arrive in the other order from the one the product holds them in. A query matches such an
 * operator with either order of its two operands; an operator that does not declare commutativity
 * is read in the order the program wrote, as the matcher reads it.
 *
 * A query that finds no value with these parts throws `FactQueryNoValue`, which is not a fault of
 * the program: a condition is the test of whether a rule applies here, so the rule makes it false ("条件里的事实查询找不到时，条件为假（这条规则在此不适用），
 * 而不是遍失败——条件本来就是"适用与否"的判断"), and whoever evaluates the condition catches it.
 */

#include "fact.h"

#include "../graph/handle.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "src/passes/sched/nodevm/nodevm.h"

#include <algorithm>
#include <format>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::rewritemod {

namespace {

namespace core     = camel::core;
namespace rtdata   = camel::core::rtdata;
namespace type     = camel::core::type;
namespace mm       = camel::core::mm;
namespace err      = camel::core::error;

using camel::runtime::DraftNode;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphDraft;
using camel::runtime::Program;

/// One node of an `on` pattern, read from the graph of the pattern function.
struct NodePattern {
    enum class Kind : uint8_t { Op, Var, Constant };

    Kind kind = Kind::Var;
    uint32_t var = 0;
    bool constOnly = false;
    type::Type *type = nullptr;
    slot_t value = 0;
    core::OperId oper = core::kNoOper;
    std::vector<NodePattern> args;
};

/// One argument the fact was queried with (`range<limit>`), kept where the description lives: a
/// value of a scalar type is the slot itself, a value the collector manages is held by a root
/// handle, which follows it when the collector moves it. The fact's functions are called with
/// these long after the query's own frame is gone, so the value has to be kept here.
struct WithValue {
    slot_t scalar = 0;
    std::shared_ptr<mm::RootHandle> object;

    slot_t slot() const {
        return object != nullptr ? reinterpret_cast<slot_t>(object->get()) : scalar;
    }
};

/// The fact of one declaration, with the values it needs to compute it: the functions are rooted
/// for as long as the description is used (a description is kept per key, name and arguments).
struct Definition {
    /// The name and the arguments the fact was queried with (`range<5>`): what the description,
    /// the fact kind and the stored value are all keyed by.
    std::string key;
    /// Which declaration this is: the context and the function the declaration generates for its
    /// initial value. Two modules may each declare a fact of one name, and one module compiled in
    /// two contexts declares two; a description or a fact kind is shared only by the same
    /// declaration.
    std::string identity;
    type::Type *factType = nullptr;
    /// What the query wrote out, in the order the declaration declares them: every function the
    /// declaration generates is called with them, so a case reads them by name.
    std::vector<WithValue> withValues;
    /// initial, join, meet, widen, narrow, then (pattern, fact)* per case.
    std::vector<std::shared_ptr<mm::RootHandle>> roots;
    size_t initial = 0;
    size_t join = 1;
    size_t meet = 2;
    size_t widen = 3;
    size_t narrow = 4;
    struct Clause {
        NodePattern pattern;
        size_t factFunction = 0;
        uint64_t constParams = 0;
    };
    std::vector<Clause> clauses;
};

/// The descriptions by key (and declaration), and the keys whose fact kind is registered.
std::mutex descriptionsMutex;
std::unordered_map<std::string, std::shared_ptr<Definition>> descriptions;
std::unordered_set<std::string> registeredKinds;
std::unordered_map<std::string, camel::runtime::fact::KindId> factKindIds;

/// The value of a fact, stored in the context's fact store (design graph-store: a node-level fact).
struct FactValue {
    uint64_t value = 0;
    uint8_t code   = 0;

    bool operator==(const FactValue &) const = default;
};

std::string factText(const FactValue &fact) {
    switch (static_cast<type::TypeCode>(fact.code)) {
    case type::TypeCode::Int64:
        return std::to_string(rtdata::fromSlot<rtdata::Int64>(fact.value));
    case type::TypeCode::Int32:
        return std::to_string(rtdata::fromSlot<rtdata::Int32>(fact.value));
    case type::TypeCode::Bool:
        return rtdata::fromSlot<rtdata::Bool>(fact.value) ? "true" : "false";
    case type::TypeCode::Float32:
        return std::format("{}", static_cast<double>(rtdata::fromSlot<rtdata::Float32>(fact.value)));
    default:
        return std::format("{}", rtdata::fromSlot<rtdata::Float64>(fact.value));
    }
}

} // namespace

namespace {

/// Where the values of a fact live: the draft being rewritten, or a frozen graph.
struct Source {
    const GraphDraft *draft = nullptr;
    const Program *program  = nullptr;
    const GCGraph *graph    = nullptr;
    camel::runtime::GraphId graphId = camel::runtime::GraphId::Invalid;

    GCNodeKind kind(gc_node_ref_t ref) const {
        return draft ? draft->node(ref)->header.kind : graph->node(ref)->kind;
    }
    type::Type *typeOf(gc_node_ref_t ref) const {
        return draft ? draft->node(ref)->header.dataType : graph->node(ref)->dataType();
    }
    core::OperId operOf(gc_node_ref_t ref) const {
        if (draft) {
            const auto bytes = draft->payloadOf(ref);
            if (bytes.size() < sizeof(camel::runtime::GCOperBody)) {
                return core::kNoOper;
            }
            return reinterpret_cast<const camel::runtime::GCOperBody *>(bytes.data())->oper;
        }
        return graph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper;
    }
    std::vector<gc_node_ref_t> normInputs(gc_node_ref_t ref) const {
        const auto inputs = draft ? draft->normInputsOf(ref) : graph->normInputsOf(ref);
        return std::vector<gc_node_ref_t>(inputs.begin(), inputs.end());
    }
    /// The values that read `ref` as a normal input: the candidates for the root of a
    /// multi-parameter query, asked of the draft or of the frozen graph's use index.
    std::span<const gc_node_ref_t> normUsers(gc_node_ref_t ref) const {
        return draft ? draft->normUsersOf(ref) : graph->uses().norm(ref);
    }
    bool alive(gc_node_ref_t ref) const { return draft ? draft->alive(ref) : true; }
    bool constant(gc_node_ref_t ref, slot_t &value, type::Type *&type) const {
        if (kind(ref) != GCNodeKind::Data) {
            return false;
        }
        const int64_t dataIndex = draft ? draft->node(ref)->header.dataIndex
                                        : graph->node(ref)->dataIndex;
        if (dataIndex >= 0) {
            return false;
        }
        const auto slots = draft ? draft->staticSlots() : graph->staticSlots();
        const size_t index = static_cast<size_t>(-dataIndex);
        if (index >= slots.size()) {
            return false;
        }
        value = slots[index];
        type  = typeOf(ref);
        return true;
    }
};

/// One evaluation of a fact over one source, with the values already computed.
struct Frame {
    const Definition *definition = nullptr;
    Source source;
    camel::core::context::Context *context = nullptr;
    std::unique_ptr<NodeVMSchedPass> vm;
    std::unordered_map<uint32_t, slot_t> memo;
    std::unordered_set<uint32_t> active;
    /// The arguments of the query, as the call needs them: rebuilt before every call, because a
    /// value the collector manages is followed by its root handle and the slot it had has to be
    /// read again; the capacity is kept, so a call of a fact allocates nothing after the first.
    std::vector<slot_t> withScratch;

    slot_t call(size_t functionIndex, std::span<const slot_t> args) {
        ::Function *fn = definition->roots[functionIndex]->getAs<::Function>();
        // A clause is a function value of the module the fact is declared in: what its body
        // captured where it was written is in the closure the value was made with, and calling it
        // by its graph alone would leave those captures empty.
        //
        // The arguments the query was written with go in as the function's `with` arguments: the
        // generated functions declare the declaration's `with` parameters, so a case names one and
        // reads the value the query wrote out.
        withScratch.clear();
        for (const WithValue &one : definition->withValues) {
            withScratch.push_back(one.slot());
        }
        return vm->callFunction(fn, args, withScratch);
    }
    slot_t initialValue() { return call(definition->initial, {}); }
    slot_t joinValues(slot_t a, slot_t b) {
        const slot_t args[2] = {a, b};
        return call(definition->join, args);
    }
};

thread_local Frame *currentFrame = nullptr;

bool sameType(type::Type *left, type::Type *right) {
    return left == right || (left != nullptr && right != nullptr && left->equals(right));
}

/// Whether two constants are the same: the same type and the same bits of the value. The bytes of a
/// slot beyond the type's width are no part of the value (a Debug and a Release build leave
/// different ones in an f32 or an i32, or a bool), so they do not decide.
bool sameConstant(slot_t a, type::Type *ta, slot_t b, type::Type *tb) {
    if (!sameType(ta, tb)) {
        return false;
    }
    if (ta != nullptr) {
        switch (ta->code()) {
        case type::TypeCode::Int32:
        case type::TypeCode::Float32:
            return static_cast<uint32_t>(a) == static_cast<uint32_t>(b);
        case type::TypeCode::Bool:
            return static_cast<uint8_t>(a) == static_cast<uint8_t>(b);
        default:
            break;
        }
    }
    return a == b;
}

bool sameFamily(core::OperId pattern, core::OperId actual) {
    const auto &table = core::OperatorTable::instance();
    if (pattern == actual) {
        return true;
    }
    if (!table.isBuiltin(pattern) || !table.isBuiltin(actual)) {
        return false;
    }
    // `:op/add_l` and `:op/add_d` are the same law over two element types: the pattern is
    // compiled with a placeholder type, so the type suffix is not part of what it matches.
    const auto family = [&](core::OperId id) {
        std::string_view uri = table.uri(id);
        const size_t cut     = uri.rfind('_');
        return cut == std::string_view::npos ? uri : uri.substr(0, cut);
    };
    return family(pattern) == family(actual);
}

size_t patternVariables(const NodePattern &pattern) {
    switch (pattern.kind) {
    case NodePattern::Kind::Var:
        return static_cast<size_t>(pattern.var) + 1;
    case NodePattern::Kind::Constant:
        return 0;
    case NodePattern::Kind::Op: {
        size_t n = 0;
        for (const auto &arg : pattern.args) {
            n = std::max(n, patternVariables(arg));
        }
        return n;
    }
    }
    return 0;
}

bool matchNode(
    Frame &frame, gc_node_ref_t ref, const NodePattern &pattern,
    std::vector<gc_node_ref_t> &refs, std::vector<slot_t> &values, uint64_t constParams) {
    switch (pattern.kind) {
    case NodePattern::Kind::Var: {
        const bool isConst = (constParams >> pattern.var) & 1;
        if (isConst) {
            slot_t value = 0;
            type::Type *type = nullptr;
            if (!frame.source.constant(ref, value, type) || !sameType(type, pattern.type)) {
                return false;
            }
            refs[pattern.var]   = ref;
            values[pattern.var] = value;
            return true;
        }
        if (refs[pattern.var] != camel::runtime::kInvalidNodeRef) {
            return refs[pattern.var] == ref;
        }
        refs[pattern.var] = ref;
        return true;
    }
    case NodePattern::Kind::Constant: {
        slot_t value = 0;
        type::Type *type = nullptr;
        return frame.source.constant(ref, value, type) &&
               sameConstant(value, type, pattern.value, pattern.type);
    }
    case NodePattern::Kind::Op: {
        if (frame.source.kind(ref) != GCNodeKind::Oper) {
            return false;
        }
        if (!sameFamily(pattern.oper, frame.source.operOf(ref))) {
            return false;
        }
        const auto inputs = frame.source.normInputs(ref);
        if (inputs.size() != pattern.args.size()) {
            return false;
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!matchNode(frame, inputs[i], pattern.args[i], refs, values, constParams)) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

slot_t evaluate(Frame &frame, gc_node_ref_t ref);

slot_t evaluateDerived(Frame &frame, gc_node_ref_t ref) {
    // No pattern matched: the value of an operator is a function of its operands, joined in
    // order; a leaf has the `initial` fact.
    const auto inputs = frame.source.normInputs(ref);
    if (inputs.empty()) {
        return frame.initialValue();
    }
    slot_t result = evaluate(frame, inputs[0]);
    for (size_t i = 1; i < inputs.size(); ++i) {
        result = frame.joinValues(result, evaluate(frame, inputs[i]));
    }
    return result;
}

slot_t evaluate(Frame &frame, gc_node_ref_t ref) {
    if (ref == camel::runtime::kInvalidNodeRef) {
        return frame.initialValue();
    }
    if (const auto found = frame.memo.find(ref); found != frame.memo.end()) {
        return found->second;
    }
    if (frame.active.contains(ref)) {
        // A value that is its own dependency: the first version has no widening, so the fact
        // of the value is the initial one (the description says so in fact.cpp's header).
        return frame.initialValue();
    }
    frame.active.insert(ref);

    const Definition &definition = *frame.definition;
    for (const auto &clause : definition.clauses) {
        const size_t variables = patternVariables(clause.pattern);
        std::vector<gc_node_ref_t> refs(variables, camel::runtime::kInvalidNodeRef);
        std::vector<slot_t> values(variables, 0);
        if (!matchNode(frame, ref, clause.pattern, refs, values, clause.constParams)) {
            continue;
        }
        // The fact function takes the matched nodes as handles (the values of `const`
        // parameters). Making a handle can collect: the slots are read after the last one.
        std::vector<std::unique_ptr<mm::RootHandle>> handles(variables);
        for (size_t i = 0; i < variables; ++i) {
            if ((clause.constParams >> i) & 1) {
                continue;
            }
            camel::graphmod::Handle *handle =
                frame.source.draft != nullptr
                    ? camel::graphmod::Handle::createInDraft(
                          *frame.source.draft, refs[i], mm::autoSpace())
                    : camel::graphmod::Handle::create(
                          camel::graphmod::HandleKind::Node, frame.source.program,
                          frame.source.graphId, refs[i], nullptr, mm::autoSpace());
            handles[i] = std::make_unique<mm::RootHandle>(
                mm::autoSpace(),
                handle,
                camel::graphmod::HandleType::get(camel::graphmod::HandleKind::Node),
                "rewrite.factArg");
        }
        std::vector<slot_t> args;
        args.reserve(variables);
        for (size_t i = 0; i < variables; ++i) {
            if ((clause.constParams >> i) & 1) {
                args.push_back(values[i]);
            } else {
                args.push_back(rtdata::toSlot(
                    static_cast<camel::graphmod::Handle *>(handles[i]->get())));
            }
        }
        const slot_t fact = frame.call(clause.factFunction, args);
        frame.memo[ref]      = fact;
        frame.active.erase(ref);
        return fact;
    }

    const slot_t fact = evaluateDerived(frame, ref);
    frame.memo[ref]    = fact;
    frame.active.erase(ref);
    return fact;
}

/// Whether the operator of the node `ref` is declared commutative for its own type (op-semantics
/// 7.4: the law's `commutative` flag per element type). This is the declaration the matcher reads
/// to try the exchange (runtime/writings.cpp, `declaredCommutative`), and the one a parts query
/// reads here: only an operator whose declaration says the exchange gives the same value may be
/// matched with its operands exchanged.
bool declaredCommutative(const Source &source, gc_node_ref_t ref) {
    if (source.kind(ref) != GCNodeKind::Oper) {
        return false;
    }
    const core::OperId oper = source.operOf(ref);
    const core::sem::OperatorSemantics *declared =
        core::OperatorTable::instance().semantics(oper);
    if (declared == nullptr || !declared->binary) {
        return false;
    }
    type::Type *type = source.typeOf(ref);
    if (type == nullptr) {
        return false;
    }
    const core::sem::BinaryAlgebra &algebra = *declared->binary;
    const std::optional<type::TypeCode> element =
        algebra.elementOf ? algebra.elementOf(type)
                          : std::optional<type::TypeCode>(type->code());
    if (!element) {
        return false;
    }
    for (const core::sem::BinaryLaw &law : algebra.laws) {
        if (law.element == *element) {
            return law.commutative;
        }
    }
    return false;
}

/// Whether the value `ref` is the compound the parts belong to: `clause` matches it with the
/// parts bound to its variables in the order the pattern binds them. The candidate must have
/// exactly these operands, which is what makes the parts a way to name the value.
///
/// The parts come from the rule's own match, whose pairing of a commutative operator's operands
/// may be the other one from the order the program wrote (`1 * a` matched by `x * c`: the
/// constant can only be the second operand, so the matcher exchanges them). A query asks about the
/// *value*, and the declaration of such an operator says the exchange gives the same value
/// (op-semantics 7.4), so the query answers for either order of the two operands. An operator whose
/// declaration does not say it is read in the order the program wrote, exactly as the matcher
/// reads it.
bool matchesParts(
    Frame &frame, gc_node_ref_t ref, const Definition::Clause &clause,
    std::span<const gc_node_ref_t> parts) {
    if (ref == camel::runtime::kInvalidNodeRef || !frame.source.alive(ref)) {
        return false;
    }
    if (patternVariables(clause.pattern) != parts.size()) {
        return false;
    }
    const auto inOrder = [&](std::span<const gc_node_ref_t> ordered) {
        std::vector<gc_node_ref_t> refs(ordered.begin(), ordered.end());
        std::vector<slot_t> values(ordered.size(), 0);
        return matchNode(frame, ref, clause.pattern, refs, values, clause.constParams);
    };
    if (inOrder(parts)) {
        return true;
    }
    if (parts.size() == 2 && declaredCommutative(frame.source, ref)) {
        const gc_node_ref_t exchanged[2] = {parts[1], parts[0]};
        return inOrder(exchanged);
    }
    return false;
}

/// The fact a query answers, and the value it is the fact of: the queried value itself, or the
/// value the parts belong to. A multi-parameter query looks at the values that read the first
/// part, takes the clauses in the order they are written and the values in node order, so the
/// answer does not depend on the order of a user list.
///
/// Naming parts that no value has is not an error of the program: a condition is the test of
/// whether a rule applies here, so the query says so by throwing `FactQueryNoValue`, which whoever evaluates
/// the condition reads as `false`. A pass that let it through failed the whole rewrite over one
/// rule that simply does not apply.
slot_t evaluateQuery(
    Frame &frame, std::span<const gc_node_ref_t> parts, gc_node_ref_t &anchor) {
    if (parts.size() == 1) {
        anchor = parts[0];
        return evaluate(frame, anchor);
    }
    for (gc_node_ref_t part : parts) {
        if (part == camel::runtime::kInvalidNodeRef) {
            throw err::FactQueryNoValue(std::format(
                "the fact '{}' is asked about a pattern variable the left side does not bind "
                "to a node",
                frame.definition->key));
        }
    }
    std::vector<gc_node_ref_t> candidates(
        frame.source.normUsers(parts[0]).begin(), frame.source.normUsers(parts[0]).end());
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    for (const auto &clause : frame.definition->clauses) {
        for (gc_node_ref_t candidate : candidates) {
            if (matchesParts(frame, candidate, clause, parts)) {
                anchor = candidate;
                return evaluate(frame, candidate);
            }
        }
    }
    throw err::FactQueryNoValue(std::format(
        "the fact '{}' has no value whose operands are these {} nodes",
        frame.definition->key,
        parts.size()));
}

NodePattern compilePattern(
    const GCGraph &graph, gc_node_ref_t ref, uint64_t constParams,
    const std::vector<type::Type *> &portTypes, std::vector<gc_node_ref_t> &ports) {
    const camel::runtime::GCNode *node = graph.node(ref);
    NodePattern pattern;
    switch (node->kind) {
    case GCNodeKind::Port: {
        const auto found = std::find(ports.begin(), ports.end(), ref);
        if (found == ports.end()) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact.pattern",
                "the pattern reads a value that is not a parameter");
        }
        pattern.kind = NodePattern::Kind::Var;
        pattern.var  = static_cast<uint32_t>(found - ports.begin());
        pattern.constOnly = (constParams >> pattern.var) & 1;
        pattern.type = pattern.var < portTypes.size() ? portTypes[pattern.var] : node->dataType();
        return pattern;
    }
    case GCNodeKind::Data: {
        if (node->dataIndex >= 0) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact.pattern",
                "the pattern reads a value that is not a constant");
        }
        const auto index = static_cast<size_t>(-node->dataIndex);
        if (index >= graph.staticSlots().size()) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact.pattern",
                "the pattern names a constant the graph does not have");
        }
        pattern.kind  = NodePattern::Kind::Constant;
        pattern.value = graph.staticSlots()[index];
        pattern.type  = node->dataType();
        return pattern;
    }
    case GCNodeKind::Oper: {
        if (!graph.withInputsOf(ref).empty() || !graph.ctrlInputsOf(ref).empty()) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact.pattern",
                "the pattern has an operator with a context argument or an order dependency");
        }
        pattern.kind = NodePattern::Kind::Op;
        pattern.oper = graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper;
        for (gc_node_ref_t input : graph.normInputsOf(ref)) {
            pattern.args.push_back(
                compilePattern(graph, input, constParams, portTypes, ports));
        }
        return pattern;
    }
    default:
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "fact.pattern",
            "the pattern uses a value that is not an operator, a constant or a parameter");
    }
}

std::shared_ptr<Definition> buildDefinition(
    camel::core::context::Context &ctx, ArgsView &norm, const std::string &key,
    size_t nodeBegin, size_t nodeCount) {
    auto definition      = std::make_shared<Definition>();
    definition->key      = key;
    const size_t count   = norm.size();
    const size_t onBegin = nodeBegin + nodeCount;
    auto root = [&](size_t index) {
        auto *fn      = norm.get<::Function *>(index);
        auto *handle  = new mm::RootHandle(mm::autoSpace(), fn, nullptr, "rewrite.fact");
        return std::shared_ptr<mm::RootHandle>(handle);
    };
    definition->roots.push_back(root(2)); // initial
    definition->roots.push_back(root(3)); // join
    definition->roots.push_back(root(4)); // meet
    definition->roots.push_back(root(5)); // widen
    definition->roots.push_back(root(6)); // narrow
    ::Function *initial = norm.get<::Function *>(2);
    definition->factType = initial->graph()->funcType()->exitType();
    if (definition->factType == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "fact.initial",
            "the initial clause has no result type");
    }
    const type::TypeCode code = definition->factType->code();
    if (code != type::TypeCode::Int64 && code != type::TypeCode::Int32 &&
        code != type::TypeCode::Float64 && code != type::TypeCode::Float32 &&
        code != type::TypeCode::Bool) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "fact",
            "the fact type of a fact must be a scalar (int, float or bool)");
    }
    // What the query wrote out (the fact's type arguments and its `with` arguments): they sit
    // between the functions and the node parameters, and every function of the declaration is
    // called with them.
    const size_t withCount = nodeBegin > 8 ? nodeBegin - 8 : 0;
    for (size_t i = 0; i < withCount; ++i) {
        const size_t index = 8 + i;
        type::Type *argType = norm.type(index);
        WithValue value;
        if (argType != nullptr && type::isGCTraced(argType->code())) {
            value.object = std::shared_ptr<mm::RootHandle>(new mm::RootHandle(
                mm::autoSpace(), norm.get<rtdata::Object *>(index), argType,
                "rewrite.fact.with"));
        } else {
            value.scalar = norm.slot(index);
        }
        definition->withValues.push_back(std::move(value));
    }
    for (size_t index = onBegin; index + 2 < count; index += 3) {
        ::Function *patternFn = norm.get<::Function *>(index);
        definition->roots.push_back(root(index));     // pattern
        definition->roots.push_back(root(index + 1)); // fact
        const uint64_t constParams =
            static_cast<uint64_t>(norm.get<rtdata::Int64>(index + 2));
        const GCGraph &graph = *patternFn->graph();
        std::vector<gc_node_ref_t> ports(graph.normPorts().begin(), graph.normPorts().end());
        std::vector<type::Type *> portTypes;
        for (size_t i = 0; i < ports.size(); ++i) {
            portTypes.push_back(patternFn->graph()->funcType()->normTypeAt(i));
        }
        const gc_node_ref_t result =
            camel::execute::resolveRuntimeTailValueRef(const_cast<GCGraph *>(&graph));
        if (result == camel::runtime::kInvalidNodeRef) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact.pattern",
                "the pattern function has no result");
        }
        Definition::Clause clause;
        clause.pattern = compilePattern(graph, result, constParams, portTypes, ports);
        clause.factFunction = definition->roots.size() - 1;
        clause.constParams  = constParams;
        definition->clauses.push_back(std::move(clause));
    }
    (void)ctx;
    return definition;
}

camel::runtime::fact::KindId factKindOf(const std::string &key, const std::string &identity) {
    std::lock_guard lock(descriptionsMutex);
    const std::string kindKey = key + "@" + identity;
    if (const auto found = factKindIds.find(kindKey); found != factKindIds.end()) {
        return found->second;
    }
    const camel::runtime::fact::KindId kind =
        camel::runtime::fact::registerKind(camel::runtime::fact::KindSpec<FactValue>{
            .key        = "user.fact." + kindKey,
            .owner      = "user",
            .dependency = camel::runtime::fact::Dependency::Node,
            .print      = [](const FactValue &fact) { return factText(fact); },
        });
    factKindIds.emplace(kindKey, kind);
    return kind;
}

/// The text of one `with` argument, as the fact's key spells it (`range<5>`): the value the
/// compiler computed, which is part of what the fact is. A type the engine does not print is
/// named by its type, so two arguments of different types still make two facts.
std::string withArgText(const ArgsView &norm, size_t index) {
    auto *argType = norm.type(index);
    switch (argType->code()) {
    case type::TypeCode::Int64:
        return std::to_string(norm.get<rtdata::Int64>(index));
    case type::TypeCode::Int32:
        return std::to_string(norm.get<rtdata::Int32>(index));
    case type::TypeCode::Bool:
        return norm.get<rtdata::Bool>(index) ? std::string("true") : std::string("false");
    case type::TypeCode::Float32:
        return std::format("{}", static_cast<double>(norm.get<rtdata::Float32>(index)));
    case type::TypeCode::Float64:
        return std::format("{}", norm.get<rtdata::Float64>(index));
    case type::TypeCode::String: {
        // Quoted with its length: `"a, b"` is one argument and `"a", "b"` are two.
        const std::string_view text = norm.get<String *>(index)->view();
        return std::format("\"{}:{}\"", text.size(), text);
    }
    default: {
        // What else is a value of the type: its text, so two values of one type are two facts.
        std::string text = "<" + argType->toString();
        if (argType->isGCTraced()) {
            if (auto *object = norm.get<rtdata::Object *>(index)) {
                std::ostringstream printed;
                object->print(printed, argType);
                text += ":" + printed.str();
            }
        }
        return text + ">";
    }
    }
}

} // namespace

slot_t factKernel(ArgsView &, ArgsView &norm, camel::core::context::Context &ctx) {
    const std::string name(norm.get<String *>(0)->view());
    // The fact's type arguments, as the compiler fixed them for this instance: empty for a fact
    // whose head declares none. Together with the `with` arguments they are what the query was
    // written with, and they are part of what the fact is, so they go into the key the
    // description, the fact kind and the stored value are all keyed by (rewrite design 7.5).
    const std::string types(norm.get<String *>(1)->view());
    // The declaration this query is of: its context and the graph of the function it generates
    // for the initial value, which no other declaration shares.
    std::string identity = std::format("{:p}", static_cast<const void *>(&ctx));
    if (auto *initial = norm.get<::Function *>(2); initial != nullptr && initial->graph() != nullptr) {
        identity += "/" + std::string(initial->graph()->mangledName());
    }
    const size_t count     = norm.size();
    const size_t withCount = static_cast<size_t>(norm.get<rtdata::Int64>(7));
    const size_t nodeBegin = 8 + withCount;
    std::string key        = name;
    std::string written;
    if (!types.empty()) {
        written = types;
    }
    for (size_t i = 0; i < withCount; ++i) {
        if (!written.empty()) {
            written += ", ";
        }
        written += withArgText(norm, 8 + i);
    }
    if (!written.empty()) {
        key += "<" + written + ">";
    }
    // The query's pattern variables: the node handles right after the `with` arguments, one
    // in the one-value form and one per parameter in a multi-parameter declaration. They are the
    // arguments that are not functions, as they are for the resolver above. The case descriptions
    // follow as triples.
    size_t nodeCount = 0;
    while (nodeBegin + nodeCount < count &&
           norm.type(nodeBegin + nodeCount)->code() != type::TypeCode::Function) {
        ++nodeCount;
    }
    if (nodeCount == 0 || nodeBegin + nodeCount >= count ||
        (count - nodeBegin - nodeCount) % 3 != 0) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "fact",
            "a query takes one or more nodes and then the patterns of its cases");
    }
    // The values behind the handles are the parts the fact is asked about; the integers stay
    // valid when the handles move at a safepoint.
    std::vector<gc_node_ref_t> parts;
    parts.reserve(nodeCount);
    for (size_t i = 0; i < nodeCount; ++i) {
        auto *handle = norm.get<camel::graphmod::Handle *>(nodeBegin + i);
        if (handle->kind() != camel::graphmod::HandleKind::Node) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "fact",
                "a query parameter is not a node");
        }
        parts.push_back(handle->index());
    }
    auto *first = norm.get<camel::graphmod::Handle *>(nodeBegin);
    const GraphDraft *draft = first->draft();
    const bool fromDraft    = draft != nullptr;
    const Program *program = fromDraft ? nullptr : first->program();
    const camel::runtime::GraphId graphId =
        fromDraft ? camel::runtime::GraphId::Invalid : first->graph();
    if (currentFrame != nullptr && currentFrame->definition->key == key &&
        currentFrame->definition->identity == identity) {
        const bool sameSource =
            fromDraft ? currentFrame->source.draft == draft
                      : currentFrame->source.program == program &&
                            currentFrame->source.graph == program->graph(graphId);
        if (sameSource) {
            gc_node_ref_t anchor = camel::runtime::kInvalidNodeRef;
            return evaluateQuery(*currentFrame, parts, anchor);
        }
    }
    std::shared_ptr<Definition> definition;
    {
        std::lock_guard lock(descriptionsMutex);
        const auto found = descriptions.find(key + "@" + identity);
        if (found != descriptions.end()) {
            definition = found->second;
        }
    }
    if (!definition) {
        definition           = buildDefinition(ctx, norm, key, nodeBegin, nodeCount);
        definition->identity = identity;
        std::lock_guard lock(descriptionsMutex);
        descriptions.emplace(key + "@" + identity, definition);
    }
    const camel::runtime::fact::KindId kind = factKindOf(key, identity);

    Source source;
    if (fromDraft) {
        source.draft = draft;
    } else {
        source.program = program;
        source.graphId = graphId;
        source.graph   = program->graph(graphId);
    }
    Frame frame;
    frame.definition = definition.get();
    frame.source     = source;
    frame.context    = &ctx;
    frame.vm         = std::make_unique<NodeVMSchedPass>(ctx.shared_from_this());
    gc_node_ref_t anchor = camel::runtime::kInvalidNodeRef;
    // `currentFrame` says which evaluation a nested query may answer from, and it names a frame on
    // this thread's stack: it has to name this frame exactly while this evaluation runs, and the
    // one that was here before, or nothing, the moment this evaluation stops running -- however it
    // stops. `evaluateQuery` throws `FactQueryNoValue` whenever the parts it was given are no
    // value's operands (the condition of a rule that does not apply here, which the engine catches
    // above), so the ordinary way out of this function is an unwind, not a return; assigning
    // `currentFrame` back on the way past the call would leave every frame that unwound past it
    // pointing at a frame that no longer exists, and the next query of the same fact compares its
    // key against a slot of a dead frame -- a read of a stack slot whose contents the next call
    // has already overwritten. The restore is therefore tied to the frame's lifetime by the
    // guard, not to reaching the end of the evaluation.
    struct RestoresCurrent {
        Frame *previous;
        ~RestoresCurrent() { currentFrame = previous; }
    } restore{currentFrame};
    currentFrame = &frame;
    const slot_t fact = evaluateQuery(frame, parts, anchor);

    if (!fromDraft) {
        const camel::runtime::fact::Anchor factAnchor{source.program, source.graph, anchor};
        FactValue stored{
            fact, static_cast<uint8_t>(definition->factType->code())};
        try {
            ctx.facts().put<FactValue>(kind, factAnchor, stored);
        } catch (const std::exception &) {
            // A kind registered by another module under the same name: the value is still
            // answered, it is just not stored.
        }
    }
    return fact;
}

} // namespace camel::rewritemod
