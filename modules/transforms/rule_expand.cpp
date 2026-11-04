// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "rule_expand.h"

#include "camel/core/rtdata/func.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/parse/ast/rule_head.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::transforms {

namespace rt = camel::runtime;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

std::vector<camel::core::type::Type *>
rulePortsOf(const camel::compile::gir::GenericRuleInstance &instance) {
    using camel::core::type::FunctionType;
    std::vector<camel::core::type::Type *> ports;
    const FunctionType *func = instance.lhs != nullptr ? instance.lhs->funcType() : nullptr;
    if (func == nullptr) {
        return ports;
    }
    ports.reserve(func->normTypesCount());
    for (size_t i = 0; i < func->normTypesCount(); ++i) {
        ports.push_back(func->normTypeAt(i));
    }
    return ports;
}

bool narrowerPortsThan(
    const std::vector<camel::core::type::Type *> &narrow,
    const std::vector<camel::core::type::Type *> &wide) {
    if (narrow.size() != wide.size() || narrow.empty()) {
        return false;
    }
    bool strict = false;
    for (size_t i = 0; i < narrow.size(); ++i) {
        if (narrow[i] == nullptr || wide[i] == nullptr) {
            return false;
        }
        if (!wide[i]->assignableFrom(narrow[i])) {
            return false;
        }
        if (!narrow[i]->assignableFrom(wide[i])) {
            strict = true;
        }
    }
    return strict;
}

std::optional<std::vector<int64_t>> statedShapeOf(const camel::core::type::Type *type) {
    if (type == nullptr || type->code() != camel::core::type::TypeCode::Array) {
        return std::nullopt;
    }
    const auto *array = static_cast<const camel::core::type::ArrayType *>(type);
    if (array == nullptr || array->elemType() == nullptr ||
        !camel::core::type::isFloatingScalar(array->elemType()->code())) {
        return std::nullopt;
    }
    const std::span<const camel::core::type::ExtentForm> stated = array->extents();
    if (stated.empty()) {
        // `T[..]`: the rank is the value's, so the type states no shape at all.
        return std::nullopt;
    }
    std::vector<int64_t> extents;
    extents.reserve(stated.size());
    for (const camel::core::type::ExtentForm &extent : stated) {
        const std::optional<int64_t> size = extent.evaluate({});
        if (!size.has_value() || *size < 0) {
            return std::nullopt;
        }
        extents.push_back(*size);
    }
    return extents;
}

namespace {

/// A type list as the reports write it: `(float[..], float)`, `()` when empty.
std::string typeTextOf(const std::vector<camel::core::type::Type *> &types) {
    std::string text;
    for (const camel::core::type::Type *type : types) {
        text += (text.empty() ? "(" : ", ");
        text += type != nullptr ? camel::core::type::typeToString(type) : std::string("?");
    }
    return text.empty() ? std::string("()") : text + ")";
}

} // namespace

RuleChoice chooseRuleAmong(
    camel::core::context::Context &context,
    const std::vector<camel::parse::ast::RuleMatch> &candidates,
    const std::vector<camel::core::type::Type *> &instanceTypes,
    const std::vector<camel::core::type::Type *> &operandTypes, size_t dataOperandCount,
    const RuleChoiceWords &words, const std::vector<bool> *answered) {
    using camel::core::type::Type;
    RuleChoice choice;
    const auto &table = camel::compile::gir::genericRuleTable(context);
    // The most specific refusal wins: a candidate that fits the type but not the shape says more
    // than a later candidate whose condition simply does not hold here.
    std::string shapeWhy;
    // The same candidate's two type lists, kept beside the sentence so a family that writes its own
    // wording for a shape refusal (`transpose`'s `rule-shape`) does not have to re-instantiate the
    // law to read them again.
    std::vector<Type *> shapePorts;
    std::string lastWhy;
    // Every candidate that instantiates and whose ports take the operands. A key can hold more than
    // one such law (`T[..]` and `T[]` both take a rank-one array), so the fit is collected first and
    // the choice made after it.
    struct Fit {
        const camel::compile::gir::GenericRuleInstance *instance = nullptr;
        std::string rule;
        std::vector<Type *> ports;
    };
    std::vector<Fit> fitting;
    for (const camel::parse::ast::RuleMatch &candidate : candidates) {
        if (candidate.status != camel::parse::ast::RuleLookup::Found) {
            continue;
        }
        uint64_t ruleId = 0;
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i] != nullptr && table[i]->name == candidate.rule) {
                ruleId = static_cast<uint64_t>(i) + 1;
                break;
            }
        }
        if (ruleId == 0) {
            lastWhy = "the rule is named in the index but its templates are not registered";
            continue;
        }
        const size_t at =
            camel::compile::gir::genericRuleInstance(context, ruleId, instanceTypes, {});
        const auto *entry = camel::compile::gir::genericRuleEntry(context, ruleId);
        if (entry == nullptr || at >= entry->instances.size()) {
            lastWhy = "no instance of the rule at " + typeTextOf(instanceTypes);
            continue;
        }
        const auto &instance = entry->instances[at];
        if (!instance.made || instance.program == nullptr || instance.rhs == nullptr) {
            lastWhy = instance.why.empty()
                          ? "no instance of the rule at " + typeTextOf(instanceTypes)
                          : instance.why;
            continue;
        }
        // The shape first: a candidate whose ports are not the operands' own types cannot be built,
        // however well its condition holds at the element. Refusing here is what keeps a `float[]`
        // out of a `float` port -- the run that did it was killed by a signal, and a named refusal is
        // worth more than a silent death.
        //
        // A rule's parameters are the operator's **ports, in port order** -- the normal ones, then
        // the `<>` ones (`stdlib/rulesets/jvp.cml`, the paragraph above `jvp_sum`;
        // `stdlib/rulesets/vmap.cml` states the same protocol where it builds a call). So the rule's
        // `count` parameters line up with the node's first `count` ports, and a *rule* that names
        // fewer of them than the node has ports is not refusing anything: `jvp_reshape` is
        // `rule jvp_reshape::<T>(x: T[..])` over the node `reshape<[2, 2]>(x)`, whose second port is
        // the shape its own left side writes as a constant. That port is a static slot of the
        // operator, not a value the node passes as data -- `normInputsOf` does not answer it,
        // `withInputsOf` does -- and it is not a parameter of the rule, so counting it refused a law
        // for the shape of its own left side. What the count still has to say is that no *data* input
        // is left without a parameter to bind it: a rule that names fewer parameters than the node has
        // data inputs cannot describe the node, and one that names more parameters than the node has
        // ports cannot be filled.
        //
        // "Takes" is the relation a call site uses, not equality: an instance's ports are written in
        // the spelling the rule's left side uses, and that spelling does not shrink to the shape of
        // the operand it meets. `jvp_mul_tensor` is `mul_tensor(x: T[..], w: T[..])`, and a
        // `float[2, 2]` operand is a value that port takes -- an open rank is every rank, and the
        // operator walks whatever rank it is given (`stdlib/rulesets/vmap.cml` says the same about the
        // batch it hands a rule). Asking for equality refused it: `float[..]` and `float[2, 2]` are
        // not the same type, and the law was refused for the shape of the value it was written for.
        // The direction is the one a call checks -- the port takes the operand -- and it is the
        // direction that keeps the case this exists for refused: a `float` port still takes no
        // `float[]`, because a primitive takes its own type and nothing else (`Type::assignableFrom`),
        // so `:op/at od` is still stopped before a `float[]` reaches the scalar law's port.
        std::vector<Type *> ports = rulePortsOf(instance);
        const bool namesTheNode =
            ports.size() <= operandTypes.size() && ports.size() >= dataOperandCount;
        bool takes = namesTheNode;
        for (size_t i = 0; takes && i < ports.size(); ++i) {
            // A port the walk answers no cotangent for is not a port the law is asked about, so its
            // shape cannot make the law wrong (`RuleChoice::answered`, and the doc in the header).
            // A family that passes no list asks about every port.
            if (answered != nullptr && i < answered->size() && !(*answered)[i]) {
                continue;
            }
            takes = ports[i] != nullptr && operandTypes[i] != nullptr &&
                    ports[i]->assignableFrom(operandTypes[i]);
        }
        if (!takes) {
            shapeWhy = std::string("the ") + words.subject + " takes " + typeTextOf(ports) +
                       ", and the operator has " + typeTextOf(operandTypes);
            if (dataOperandCount > 0) {
                shapeWhy += " (of which " + std::to_string(dataOperandCount) + " is a data operand)";
            }
            shapePorts = ports;
            continue;
        }
        fitting.push_back(Fit{&instance, candidate.rule, std::move(ports)});
    }
    if (fitting.empty()) {
        // Every candidate refused: the shape that nearly fitted says the most.
        choice.why = !shapeWhy.empty() ? shapeWhy : lastWhy;
        if (!shapeWhy.empty()) {
            // A law was found and instantiated and its shape refused: that is `Shape`, not `NoLaw`,
            // and the two type lists travel with it.
            choice.refusal     = RuleRefusal::Shape;
            choice.lawPorts    = std::move(shapePorts);
            choice.operandTypes = operandTypes;
        }
        return choice;
    }
    // The narrowest fit, and only when it is one law. Two laws neither of which is narrower than the
    // other are two laws the declaration does not choose between -- the author's `two laws both
    // accept these arguments` -- and it is reported rather than answered by filing order.
    std::vector<const Fit *> kept;
    for (const Fit &candidate : fitting) {
        bool outranked = false;
        for (const Fit &other : fitting) {
            if (&other == &candidate) {
                continue;
            }
            if (narrowerPortsThan(other.ports, candidate.ports)) {
                outranked = true;
                break;
            }
        }
        if (!outranked) {
            kept.push_back(&candidate);
        }
    }
    if (kept.size() > 1) {
        std::string names;
        for (size_t i = 0; i < kept.size(); ++i) {
            names += (i == 0 ? "'" : " and '");
            names += kept[i]->rule;
            names += "'";
        }
        choice.why = std::string("two laws both accept these ") + words.operands + ": " + names +
                     " (the operator has " + typeTextOf(operandTypes) + ")";
        return choice;
    }
    choice.instance = kept.front()->instance;
    choice.rule     = kept.front()->rule;
    choice.refusal  = RuleRefusal::None;
    return choice;
}

namespace {

/// The graph a rule's right side answers with, read from the right side itself.
///
/// The right side of `x * w => (dx, dw) => mul_tangent(x, w, dx, dw)` is a graph that builds the
/// closure `(dx, dw) => mul_tangent(x, w, dx, dw)` over the values the left side bound: its exit is
/// a FILL of the `FunctionClosure` kind, whose static input names the graph the closure runs, and
/// whose `with` inputs are the captured values, in slot order. That inner graph is the body a call
/// of the closure would run, and its two port lists say what the call would pass:
///
///   `normPorts`    the parameters -- the tangents, `dx, dw`;
///   `closureNodes` the captures -- the values the left side bound, `x, w`.
///
/// Reading the body from here is what keeps the argument order out of C++: `mul_tangent(x, w, dx,
/// dw)` is the order the body's own call node already has, not a rule about how a right side is
/// written.
struct RuleRightSide {
    /// The body, a graph of the instance's own program.
    const camel::runtime::GCGraph *body = nullptr;
    /// The values the closure captures, as nodes of the right side's graph, in slot order.
    std::vector<gc_node_ref_t> captures;
};

/// Fills `found` from the instance's right side, or says in `why` what about it is not a function of
/// the tangents.
bool findRuleRightSide(
    const camel::compile::gir::GenericRuleInstance &instance, RuleRightSide &found,
    std::string &why) {
    const rt::GCGraph *rhs = instance.rhs;
    // The exit node is the graph's value-producing node; a gate may stand in front of it when the
    // value is carried out of a block.
    gc_node_ref_t exitRef = rhs->exitNodeRef();
    for (size_t guard = 0; guard < 8 && exitRef != rt::kInvalidNodeRef; ++guard) {
        const rt::GCNode *node = rhs->node(exitRef);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const auto carried = rhs->normInputsOf(exitRef);
        exitRef = carried.empty() ? rt::kInvalidNodeRef : carried.front();
    }
    const rt::GCNode *exit = exitRef == rt::kInvalidNodeRef ? nullptr : rhs->node(exitRef);
    if (exit == nullptr) {
        why = "the rule's right side has no value to answer with";
        return false;
    }
    // A right side answers a *function of the tangents*, and there are two shapes of that answer.
    // One captures the operands: a FILL of the FunctionClosure kind, whose static input names the
    // graph the closure runs and whose with inputs are the captured values. One captures nothing --
    // a rule whose answer is a function of the tangent alone -- and then there is nothing to carry
    // with the value, so the builder hands over the function value itself (`builder_calls.cpp`:
    // "if (hasClosure)"), a Data node over a static function slot. Same graph, no wrapper.
    gc_node_ref_t functionNode = rt::kInvalidNodeRef;
    if (exit->kind == rt::GCNodeKind::Fill) {
        const auto *fillBody = rhs->nodeBodyAs<rt::GCFillBody>(exitRef);
        if (fillBody == nullptr || fillBody->fillKind != rt::GCFillKind::FunctionClosure) {
            why = std::string("the rule's right side ends in a fill of kind ") +
                  (fillBody == nullptr ? std::string("?")
                                       : std::to_string(static_cast<int>(fillBody->fillKind))) +
                  ", not a function of the tangents";
            return false;
        }
        for (gc_node_ref_t input : rhs->normInputsOf(exitRef)) {
            const rt::GCNode *node = rhs->node(input);
            if (node != nullptr && node->kind == rt::GCNodeKind::Data && node->dataIndex < 0) {
                functionNode = input;
                break;
            }
        }
        for (gc_node_ref_t capture : rhs->withInputsOf(exitRef)) {
            found.captures.push_back(capture);
        }
    } else if (exit->kind == rt::GCNodeKind::Data && exit->dataIndex < 0) {
        functionNode = exitRef;
    } else {
        why = std::string("the rule's right side ends in a node of kind ") +
              std::to_string(static_cast<int>(exit->kind)) + ", not a function of the tangents";
        return false;
    }
    if (functionNode == rt::kInvalidNodeRef) {
        why = "the rule's right side names no graph for the function it answers with";
        return false;
    }
    // The graph the answer runs is the static function value the Data node holds.
    const std::span<const slot_t> slots = rhs->staticSlots();
    const size_t at = static_cast<size_t>(
        -static_cast<std::ptrdiff_t>(rhs->node(functionNode)->dataIndex));
    if (at < slots.size()) {
        const auto *function = camel::core::rtdata::fromSlot<const ::Function *>(slots[at]);
        if (function != nullptr) {
            found.body = instance.program->graph(function->graphId());
            if (found.body != nullptr) {
                return true;
            }
        }
    }
    why = "the rule's right side names no graph for the function it answers with";
    return false;
}

/// Which of the body's ports some node of the body reads. A port nothing reads is a value the
/// expansion must not ask for: the right side names it nowhere, and a caller that has no value for
/// it is a caller the expansion can still serve.
///
/// `jvp` never needed this (it always has the operands' values); `transpose` does. The two operands
/// of `x * c` are not both available where a loop body's own state stands in one of them, while the
/// answer that position needs reads only the other -- the constant -- and the rule says so by
/// naming only that one.
std::vector<bool> portsReadByBody(const rt::GCGraph &body, const std::vector<bool> &keep) {
    std::vector<bool> read(body.nodeCount(), false);
    for (gc_node_ref_t ref = 0; ref < body.nodeCount(); ++ref) {
        if (body.node(ref) == nullptr || (!keep.empty() && !keep[ref])) {
            continue;
        }
        for (gc_node_ref_t input : body.normInputsOf(ref)) {
            if (input < read.size()) {
                read[input] = true;
            }
        }
    }
    return read;
}

/// The operator a law names when it wants an operand's **shape** and not its value.
///
/// `tensor:shape` is the library's own name for the extents of an array (`stdlib/tensor.cml#shape`),
/// and it is the name a law has always had for this: `jvp_sum` states a sum's extents by writing
/// `sum_stated(dxs, shape(xs))`. What changes here is where the numbers come from when the caller
/// carries no value for `xs` -- a transposition has none for the operand it differentiates -- so the
/// rule layer answers the call from the type instead (`expandRuleRightSide`).
constexpr std::string_view kShapeUri = "tensor:shape";

/// The extents as the value a law reads them as: a rank-one array of the numbers, of the type the
/// law's own `tensor:shape` node answers with (`int[]`).
///
/// This is the whole of the pathway the author asked for on 2026-11-03 (17:35 and 17:55): a shape
/// arrives at a law as a **known operand** -- a constant of the graph, built where the graph is
/// built -- instead of being read off the value of an operand the caller does not have. It is not a
/// zero standing in for a value (that is what `transpose.cpp#shapeCarrierOf` was, and a law that
/// multiplied by it answered zeros); it is the extents themselves, and the only thing that reads
/// them is the shape (5.4: a shape is a fact about a type, and this is that fact written down).
gc_node_ref_t statedExtentsValue(
    camel::runtime::GraphDraft &draft, camel::core::type::Type *extentsType,
    const std::vector<int64_t> &extents) {
    if (extentsType == nullptr || extentsType->code() != camel::core::type::TypeCode::Array) {
        return rt::kInvalidNodeRef;
    }
    auto *arrayType = static_cast<camel::core::type::ArrayType *>(extentsType);
    const int64_t length = static_cast<int64_t>(extents.size());
    ::Array *value       = ::Array::create(
        camel::core::mm::autoSpace(), arrayType, std::span<const int64_t>(&length, 1),
        /*zeroInit=*/true);
    if (value == nullptr) {
        return rt::kInvalidNodeRef;
    }
    // The storage follows the element type (`type::arrayStorageFor`): an `int[]` holds one `int64_t`
    // per element, and a shape that did not fit the array it was written into would be a shape the
    // graph reads half of. Refused rather than written short.
    if (value->byteSize() != static_cast<size_t>(length) * sizeof(int64_t)) {
        return rt::kInvalidNodeRef;
    }
    std::memcpy(value->rawData(), extents.data(), static_cast<size_t>(length) * sizeof(int64_t));
    return draft.materializeStaticValue(
        camel::core::rtdata::toSlot<::Array *>(value), extentsType, rt::kGCNodeFlagConstant);
}

} // namespace

std::vector<camel::core::type::Type *>
ruleCotangentPortsOf(const camel::compile::gir::GenericRuleInstance &instance) {
    std::vector<camel::core::type::Type *> ports;
    RuleRightSide side;
    std::string why;
    if (!findRuleRightSide(instance, side, why) || side.body == nullptr) {
        return ports;
    }
    for (const gc_node_ref_t port : side.body->normPorts()) {
        const rt::GCNode *node = side.body->node(port);
        ports.push_back(node != nullptr ? const_cast<camel::core::type::Type *>(node->dataType())
                                        : nullptr);
    }
    return ports;
}

/// The rule's right side expanded at the match site: the body of the closure it answers with is
/// copied into this graph, its captures bound to the operands and its parameters to the tangents,
/// and the copy's exit is the operator's derivative.
///
/// This is one lambda layer, not a general inliner. A body that is more than a straight line of
/// computations -- a region, an indirect call, a function value -- is refused by name, the way a
/// primitive with no rule is: the transform has no reading for it, and a copy that guessed would
/// compute something else.
gc_node_ref_t expandRuleRightSide(
    camel::runtime::GraphDraftGroup &group, camel::runtime::GraphTransaction &transaction,
    const camel::compile::gir::GenericRuleInstance &instance,
    const std::function<gc_node_ref_t(size_t)> &valueOf,
    const std::vector<gc_node_ref_t> &tangents, std::string &why, size_t position) {
    RuleRightSide right;
    if (!findRuleRightSide(instance, right, why)) {
        return rt::kInvalidNodeRef;
    }
    // The body's calls name graphs of the instance's own program, and a FUNC node reaches its
    // callee through this program's table: the graphs the rule was instantiated into belong to the
    // program this lift builds on, the way a callee the lifted function calls does.
    group.adopt(*instance.program);
    const rt::GCGraph *body      = right.body;
    // Which node of the body is *this* answer. A rule's left side names N values, and its right side
    // answers N cotangents: a tuple, in the order the left side writes them. Position `i` is that
    // tuple's i-th component, and only the nodes that component needs are copied -- so a position
    // never asks for a value its own answer does not read (a loop body's own state, for one, which
    // has no forward value at all). A right side that answers one value answers position 0 only.
    std::vector<bool> keep;
    gc_node_ref_t answerRef = body->exitNodeRef();
    if (position != kWholeAnswer) {
        for (size_t guard = 0; guard < 8; ++guard) {
            const rt::GCNode *node =
                answerRef == rt::kInvalidNodeRef ? nullptr : body->node(answerRef);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto carried = body->normInputsOf(answerRef);
            answerRef = carried.empty() ? rt::kInvalidNodeRef : carried.front();
        }
        const rt::GCNode *exit = answerRef == rt::kInvalidNodeRef ? nullptr : body->node(answerRef);
        if (exit == nullptr) {
            why = "the rule's right side has no value to answer with";
            return rt::kInvalidNodeRef;
        }
        if (exit->kind == rt::GCNodeKind::Fill) {
            const auto *fillBody = body->nodeBodyAs<rt::GCFillBody>(answerRef);
            if (fillBody != nullptr && fillBody->fillKind == rt::GCFillKind::Tuple) {
                const auto components = body->withInputsOf(answerRef);
                if (position >= components.size()) {
                    why = "the rule's right side answers " + std::to_string(components.size()) +
                          " name(s), and the position asked for is " + std::to_string(position);
                    return rt::kInvalidNodeRef;
                }
                answerRef = components[position];
            } else if (position != 0) {
                why = "the rule's right side answers one name, and the position asked for is " +
                      std::to_string(position);
                return rt::kInvalidNodeRef;
            }
        } else if (position != 0) {
            why = "the rule's right side answers one name, and the position asked for is " +
                  std::to_string(position);
            return rt::kInvalidNodeRef;
        }
        keep.assign(body->nodeCount(), false);
        std::vector<gc_node_ref_t> work{answerRef};
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            if (ref >= keep.size() || keep[ref]) {
                continue;
            }
            keep[ref] = true;
            for (gc_node_ref_t input : body->normInputsOf(ref)) {
                work.push_back(input);
            }
            // An angle argument the answer names is part of what the answer is made of: it is copied
            // with the nodes that read it, not left behind for the copy to trip over.
            for (gc_node_ref_t input : body->withInputsOf(ref)) {
                work.push_back(input);
            }
        }
    }
    const auto capturePorts     = body->closureNodes();
    const auto parameterPorts   = body->normPorts();
    if (capturePorts.size() != right.captures.size() || parameterPorts.size() != tangents.size()) {
        why = "the rule's right side does not take the values and tangents the rule binds";
        return rt::kInvalidNodeRef;
    }
    // A capture is a port of the right side's graph, which is one of the arguments the rule is
    // called with: the operands, in the order the left side writes them. Only the captures the body
    // actually reads are bound: a right side that names one operand asks for that one only, and the
    // caller may have no value for the other (a transposed loop body's own state).
    const auto read            = portsReadByBody(*body, keep);
    const auto rhsPorts        = instance.rhs->normPorts();
    // The law's own ports, in the order its left side writes them: the operand types the instance
    // was made at, which is where a stated shape lives (`statedShapeOf`).
    const std::vector<camel::core::type::Type *> ports = rulePortsOf(instance);
    // The body nodes this expansion answers with a stated shape instead of a call, and the captures
    // whose only reader is one of those calls. Filled below, read by the copy.
    std::unordered_map<gc_node_ref_t, std::vector<int64_t>> statedShapes;
    std::unordered_map<gc_node_ref_t, bool> answeredAsShape;
    // Whether the answer can have `capture`'s **shape** where it cannot have its value.
    //
    // The question is the author's 2026-11-03 17:35/17:55 ruling -- a shape reaches a transpose as a
    // known operand, never read off an operand's value -- and the answer is yes exactly when two
    // facts hold:
    //
    //   * the law's port for that capture states its extents (`float[2, 3]` does, `float[..]` and a
    //     number do not). The instance is made at the node's own operand types
    //     (`expandRuleByName`, `transpose.cpp#ruleCotangent`), so the port is the operand's type;
    //   * every place the answer reads that capture is a `tensor:shape` call. A body that read it
    //     anywhere else needs the value, and the value is the caller's to carry: substituting there
    //     would answer arithmetic with a shape, which is the silent zero this exists to remove.
    //
    // Both readings are refusals of the substitution, not of the expansion: a law that reads the
    // value and a caller that has none still meets the sentence it always met.
    const auto answerTheShapeOf = [&](gc_node_ref_t capture, size_t argument) -> bool {
        if (argument >= ports.size()) {
            return false;
        }
        const std::optional<std::vector<int64_t>> extents = statedShapeOf(ports[argument]);
        if (!extents.has_value()) {
            return false;
        }
        std::vector<gc_node_ref_t> readers;
        for (gc_node_ref_t ref = 0; ref < body->nodeCount(); ++ref) {
            if (body->node(ref) == nullptr || (!keep.empty() && !keep[ref])) {
                continue;
            }
            for (gc_node_ref_t input : body->normInputsOf(ref)) {
                if (input == capture) {
                    readers.push_back(ref);
                    break;
                }
            }
        }
        if (readers.empty()) {
            return false;
        }
        for (const gc_node_ref_t ref : readers) {
            const rt::GCNode *node = body->node(ref);
            if (node == nullptr || node->kind != rt::GCNodeKind::Oper) {
                return false;
            }
            const auto *oper = body->nodeBodyAs<rt::GCOperBody>(ref);
            const auto inputs = body->normInputsOf(ref);
            if (oper == nullptr || oper->uri() != kShapeUri || inputs.size() != 1 ||
                inputs.front() != capture) {
                return false;
            }
        }
        for (const gc_node_ref_t ref : readers) {
            statedShapes.emplace(ref, *extents);
        }
        answeredAsShape.emplace(capture, true);
        if (!keep.empty()) {
            // Not part of the answer any more: the call that read it answers with the shape, so the
            // value is neither asked for nor copied.
            keep[capture] = false;
        }
        return true;
    };
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> bound;
    for (size_t i = 0; i < capturePorts.size(); ++i) {
        const auto at = std::find(rhsPorts.begin(), rhsPorts.end(), right.captures[i]);
        if (at == rhsPorts.end()) {
            why = "a value the rule's right side captures is not one of its parameters";
            return rt::kInvalidNodeRef;
        }
        if (!keep.empty() && !keep[capturePorts[i]]) {
            // Not part of the answer this position asks for: it needs no value and asks for none.
            continue;
        }
        if (!read[capturePorts[i]]) {
            continue;
        }
        const size_t argument = static_cast<size_t>(at - rhsPorts.begin());
        // Asked before the value, not after it: a caller that has no value for this operand may still
        // have a *shape* for it, and a caller that has a value may be holding a stand-in for it
        // (`transpose.cpp#shapeCarrierOf`, a zero array built to be read for its shape). Answering
        // the shape from the type is the same answer in both cases, and it is the one the author
        // asked for: the extents the port states, written into the graph where the call stood.
        if (answerTheShapeOf(capturePorts[i], argument)) {
            continue;
        }
        const gc_node_ref_t value = valueOf == nullptr ? rt::kInvalidNodeRef : valueOf(argument);
        if (value == rt::kInvalidNodeRef) {
            why = "the rule's right side reads a value the caller does not carry";
            return rt::kInvalidNodeRef;
        }
        bound[capturePorts[i]] = value;
    }
    for (size_t i = 0; i < parameterPorts.size(); ++i) {
        bound[parameterPorts[i]] = tangents[i];
    }

    auto &draft                                = transaction.edit();
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> copied;
    const std::span<const slot_t> staticSlots = body->staticSlots();
    // A rule's right side is copied in one pass because a node's inputs are written before it, and
    // that order is what makes the right side a straight line. An instance built at types the law was
    // written for is usually in that order; one built at other types is not always -- a reduction
    // whose answer type follows the *other* operand can end up written before the value it reduces,
    // while the graph is still a straight line. So the pass repeats while it makes progress, and the
    // complaint about order is kept for the pass that copies nothing.
    bool blocked = false;
    bool progress = true;
    while (progress) {
        const size_t before = copied.size();
        blocked = false;
        for (gc_node_ref_t ref = 0; ref < body->nodeCount(); ++ref) {
        if (copied.count(ref) != 0) {
            continue;
        }
        const rt::GCNode *node = body->node(ref);
        if (node == nullptr) {
            continue;
        }
        if (!keep.empty() && !keep[ref]) {
            continue;
        }
        if (const auto stated = statedShapes.find(ref); stated != statedShapes.end()) {
            // The shape the caller could not carry, written down where the call stood: a constant of
            // this graph, built from the extents the law's own port states. Everything the answer
            // does with it is a shape (the call the law wrote is `tensor:shape`), and nothing reads
            // it as an operand's value.
            copied[ref] = statedExtentsValue(draft, node->dataType(), stated->second);
            if (copied[ref] == rt::kInvalidNodeRef) {
                why = "the rule's right side asks for a shape this graph cannot write down";
                return rt::kInvalidNodeRef;
            }
            continue;
        }
        if (answeredAsShape.count(ref) != 0) {
            // A capture answered as a shape: it was removed from `keep` where the position was
            // picked, and a whole-answer expansion reaches it without that pass.
            continue;
        }
        if (node->kind == rt::GCNodeKind::Port) {
            // A port of the body is a value the call passes: bound, never copied, so the copy has
            // no parameters of its own. A port nothing reads is neither bound nor copied.
            const auto at = bound.find(ref);
            if (at == bound.end()) {
                if (!read[ref]) {
                    continue;
                }
                why = "the rule's right side reads a value that is neither a capture nor a tangent";
                return rt::kInvalidNodeRef;
            }
            copied[ref] = at->second;
            continue;
        }
        if (body->nodeRegion(ref) != 0) {
            why = "the rule's right side is not a straight line: it has a region";
            return rt::kInvalidNodeRef;
        }
        std::vector<gc_node_ref_t> inputs;
        bool missing = false;
        for (gc_node_ref_t input : body->normInputsOf(ref)) {
            const auto at = copied.find(input);
            if (at == copied.end()) {
                why   = "the rule's right side uses a node written after it";
                missing = true;
                break;
            }
            inputs.push_back(at->second);
        }
        if (missing) {
            blocked = true;
            continue;
        }
        // What the rule's right side passes in angle brackets: `reshape<shape>(dx)` states, on the
        // right, the same shape the left side was written with, so the value is a constant of the
        // rule's own graph -- a `with` input like any other, copied the way an operand is. A with
        // input that is not a constant of the body would have to be the matched node's own context
        // argument, and the expansion carries the rule's values, not the node's: that one is still
        // refused by name, below.
        std::vector<gc_node_ref_t> withs;
        bool withMissing = false;
        for (gc_node_ref_t input : body->withInputsOf(ref)) {
            const auto at = copied.find(input);
            if (at == copied.end()) {
                why         = "the rule's right side passes a context argument it does not carry";
                withMissing = true;
                break;
            }
            withs.push_back(at->second);
        }
        if (withMissing) {
            blocked = true;
            continue;
        }
        const bool isFunctionValue =
            node->dataType() != nullptr &&
            node->dataType()->code() == camel::core::type::TypeCode::Function;
        switch (node->kind) {
        case rt::GCNodeKind::Data: {
            // A constant of the body: the same value, in this graph. A function value is not a
            // constant to copy -- it is the closure this expansion exists to avoid.
            if (isFunctionValue) {
                why = "the rule's right side answers with a function value";
                return rt::kInvalidNodeRef;
            }
            const std::ptrdiff_t index = -static_cast<std::ptrdiff_t>(node->dataIndex);
            if (node->dataIndex >= 0 || index < 0 ||
                static_cast<size_t>(index) >= staticSlots.size()) {
                why = "the rule's right side reads a value that is not a constant of its graph";
                return rt::kInvalidNodeRef;
            }
            copied[ref] = draft.materializeStaticValue(
                staticSlots[static_cast<size_t>(index)], node->dataType(),
                rt::kGCNodeFlagConstant);
            break;
        }
        case rt::GCNodeKind::Oper: {
            const auto *oper = body->nodeBodyAs<rt::GCOperBody>(ref);
            if (oper == nullptr) {
                why = "the rule's right side has an operator node with no operator";
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t made = draft.addOperNode(node->dataType(), nullptr, oper->uri());
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            if (!withs.empty()) {
                draft.setWithInputs(made, withs);
            }
            copied[ref] = made;
            break;
        }
        case rt::GCNodeKind::Cast:
        case rt::GCNodeKind::Copy: {
            // A cast is how the builder states a value of the same meaning in another written type --
            // the mixed-type instance of a law uses one exactly where 5.4 says a rank-zero array is the
            // element (`float[..]` reduced to `float`). The rule's right side is a straight line, so a
            // cast is one input and one node: copy it with the type the body gave it.
            if (inputs.size() != 1) {
                why = "a cast on the rule's right side does not take one value";
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t made = node->kind == rt::GCNodeKind::Cast
                                           ? draft.addCastNode(node->dataType())
                                           : draft.addCopyNode(node->dataType());
            draft.setNormInputs(made, inputs);
            copied[ref] = made;
            break;
        }
        case rt::GCNodeKind::Func: {
            // A call to a named function: what the rule's right side is for, and what stays a call.
            if (isFunctionValue) {
                why = "the rule's right side answers with a function value";
                return rt::kInvalidNodeRef;
            }
            const GraphId callee = body->calleeIdOf(ref);
            if (!rt::isValid(callee)) {
                why = "the rule's right side calls a function it does not name";
                return rt::kInvalidNodeRef;
            }
            // Which of the call's arguments are tangents -- the rule's own split, recorded on the call
            // the rule produced as one bit per argument position. The right side binds the values as
            // its captures and takes the tangents as its own parameters
            // (`x * w => (dx, dw) => mul_tangent(x, w, dx, dw)`), so an argument that is one of this
            // expansion's tangent ports is a tangent wherever the right side writes it: `mul_tangent`
            // ends with its two, `index_into(dxs, i)` begins with one and puts a value last. The record
            // is therefore the set of positions, not a run of trailing ones. This is the one place the
            // fact exists in the tree: `transpose` reads the node rather than re-deriving the split
            // from a name, and there is no second table to keep in step when a rule is added.
            //
            // The record is `GCNode::tangentArgs`, one bit per position. A tangent past its last bit
            // cannot be written, and a call recorded as one of values only is a wrong split rather
            // than a missing one, so such a right side is refused by name instead.
            uint16_t tangentMask = 0;
            if (!inputs.empty() && !tangents.empty()) {
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (std::find(tangents.begin(), tangents.end(), inputs[i]) == tangents.end()) {
                        continue;
                    }
                    if (i >= rt::kTangentArgumentBits) {
                        why = "the rule's right side passes a tangent as argument " +
                              std::to_string(i + 1) + " of a call, and a call's tangent positions are "
                              "recorded for its first " +
                              std::to_string(rt::kTangentArgumentBits) + " arguments";
                        return rt::kInvalidNodeRef;
                    }
                    tangentMask |= static_cast<uint16_t>(uint16_t{1} << i);
                }
            }
            const gc_node_ref_t made = draft.addFuncNode(
                callee, node->dataType(), 0, draft.allocateRuntimeSlot(node->dataType()), 0,
                tangentMask);
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            if (!withs.empty()) {
                draft.setWithInputs(made, withs);
            }
            // The call reaches its callee through this node, so that graph is a dependency of this
            // one by the reference table's own rule (a FUNC node names its callee).
            draft.addDependency(callee);
            copied[ref] = made;
            break;
        }
        case rt::GCNodeKind::Gate: {
            const gc_node_ref_t made =
                draft.addGateNode(node->dataType(), draft.allocateRuntimeSlot(node->dataType()));
            if (!inputs.empty()) {
                draft.setNormInputs(made, inputs);
            }
            copied[ref] = made;
            break;
        }
        default:
            why = std::string("the rule's right side is not a straight line of computations: node ") +
                  std::to_string(ref) + " kind " + std::to_string(static_cast<int>(node->kind)) +
                  " of " + std::to_string(body->nodeCount()) + ", answer ref " +
                  std::to_string(answerRef);
            return rt::kInvalidNodeRef;
        }
        }  // the pass over the body's nodes
        progress = copied.size() > before;
        if (!progress && blocked) {
            // A whole pass copied nothing and something was waiting for a node it never got: the
            // reason the last one was blocked is why this expansion cannot be built.
            return rt::kInvalidNodeRef;
        }
    }
    // The body's exit is the value the call of the closure would answer with; a gate in front of it
    // carries the same value. A position asks for one component of that value, which the gate walk
    // above has already found.
    gc_node_ref_t out = position == kWholeAnswer ? body->exitNodeRef() : answerRef;
    for (size_t guard = 0; guard < 8; ++guard) {
        const rt::GCNode *node = body->node(out);
        if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
            break;
        }
        const auto carried = body->normInputsOf(out);
        if (carried.empty()) {
            break;
        }
        out = carried.front();
    }
    const auto at = copied.find(out);
    if (at == copied.end()) {
        why = "the rule's right side has no exit the transform can read";
        return rt::kInvalidNodeRef;
    }
    return at->second;
}

gc_node_ref_t expandRuleByName(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction, std::string_view ruleName,
    const std::function<gc_node_ref_t(size_t)> &valueOf,
    const std::vector<gc_node_ref_t> &tangents, size_t position,
    const std::vector<camel::core::type::Type *> &operandTypes, std::string &why,
    bool *noInstanceAtOperands) {
    using camel::core::type::Type;
    if (noInstanceAtOperands != nullptr) {
        *noInstanceAtOperands = false;
    }
    // What the rule is instantiated at, and why it is the operands' own types: a law that covers two
    // shapes of one operator -- `tensor:multiply` on two arrays, and on an array and a number --
    // names its parameters' types (`mul_tensor_t::<X, W>`), so the instance has to be built at the
    // types the operator really has. Instantiating at a single `float` made every `T[..]` read
    // `float[..]`, which is one type for every port: the number's cotangent was a member the
    // signature called `float` and the value called an array.
    //
    // A law whose type parameters are not one per operand (the scalar `mul_t` names `T` once for two
    // operands) still instantiates at the single type it always did, so the fallback keeps every
    // other law exactly where it was.
    std::vector<Type *> types;
    if (!operandTypes.empty()) {
        types = operandTypes;
    }
    const camel::compile::gir::GenericRuleInstance *instance =
        types.empty() ? nullptr
                      : camel::compile::gir::genericRuleInstanceByName(context, ruleName, types,
                                                                       {}, why);
    if (instance == nullptr) {
        // What the mixed instantiation said, kept: a second attempt that overwrote it would report
        // the fallback's complaint instead of the reason the law could not be built at these types.
        std::string firstWhy = why;
        types               = std::vector<Type *>{Type::Float64()};
        instance = camel::compile::gir::genericRuleInstanceByName(context, ruleName, types, {}, why);
        why      = firstWhy;
    }
    if (instance == nullptr) {
        // The law was named and would not be built at these types -- the second of the four ways in
        // the header. Recorded as a class and not as a reading of `why`, because `why` here is the
        // instantiation's own sentence about a call inside the law's body, and a caller that must
        // answer "which kind of refusal is this" would otherwise be parsing it back.
        if (noInstanceAtOperands != nullptr) {
            *noInstanceAtOperands = true;
        }
        return rt::kInvalidNodeRef;
    }
    RuleRightSide right;
    if (!findRuleRightSide(*instance, right, why)) {
        return rt::kInvalidNodeRef;
    }
    // Whether a value is asked for at all is `expandRuleRightSide`'s call, not this function's: it
    // knows which captures the answer reads -- after the position has picked one component -- and a
    // capture the answer does not read is never asked for, so no operand is invented for a slot the
    // answer does not use.
    return expandRuleRightSide(group, transaction, *instance, valueOf, tangents, why, position);
}

} // namespace camel::transforms