// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file translate.cpp
 * @brief The integer scalar translation of a rule's two sides, and the declaration of what it
 * covers (translate.h holds the interface and why the layer refuses rather than guesses).
 */

#include "translate.h"

#include "camel/core/rtdata/base.h"
#include "camel/runtime/core_facts.h"

#include <format>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::verify {
namespace {

using camel::core::rtdata::Int32;
using camel::core::rtdata::Int64;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCOperBody;

/// The reasons of the declared set. Each is one fixed sentence: the same rule outside the fragment
/// says the same thing wherever its row is read, and a test can pin it.
inline constexpr const char *kUnreadable = "the sides of the rule could not be read";
inline constexpr const char *kNotBuilt   = "the check did not build the rule's two sides";

std::string kindName(GCNodeKind kind) {
    switch (kind) {
    case GCNodeKind::Data:
        return "DATA";
    case GCNodeKind::Port:
        return "PORT";
    case GCNodeKind::Cast:
        return "CAST";
    case GCNodeKind::Copy:
        return "COPY";
    case GCNodeKind::Fill:
        return "FILL";
    case GCNodeKind::Accs:
        return "ACCS";
    case GCNodeKind::Brch:
        return "BRCH";
    case GCNodeKind::Join:
        return "JOIN";
    case GCNodeKind::Call:
        return "CALL";
    case GCNodeKind::Bind:
        return "BIND";
    case GCNodeKind::Func:
        return "FUNC";
    case GCNodeKind::Oper:
        return "OPER";
    case GCNodeKind::Gate:
        return "GATE";
    case GCNodeKind::Dref:
        return "DREF";
    case GCNodeKind::Loop:
        return "LOOP";
    case GCNodeKind::Dead:
        return "DEAD";
    }
    return "an unknown kind";
}

/// A whole tensor is one of the three classes design 8.1 lists as beyond the coverage: the property
/// would be over every element at once, which is not a scalar term.
bool isWholeTensor(const Type *type) {
    return type != nullptr && camel::runtime::fact::shapeOfType(type).has_value();
}

/// The integer width of a type, or nothing: the fragment covers exactly these two.
std::optional<Width> widthOf(const Type *type) {
    if (type == nullptr) {
        return std::nullopt;
    }
    switch (type->code()) {
    case TypeCode::Int32:
        return Width::Int32;
    case TypeCode::Int64:
        return Width::Int64;
    default:
        return std::nullopt;
    }
}

/// Why a value of this type is not translated: the reason names the class it belongs to. A whole
/// tensor is one class (design 8.1: the property would be over every element at once), a floating
/// width another (S3's work -- and the question "is this floating point" is asked of the family, so
/// `bf16` and `f16` are in it and not left out), and anything else is refused by its own name.
std::string uncoveredType(const Type *type) {
    if (type == nullptr) {
        return "the type of the value is not known";
    }
    if (isWholeTensor(type)) {
        return "the property is over a whole tensor";
    }
    if (camel::core::type::isFloatingScalar(type->code())) {
        return std::format(
            "the element type '{}' is not translated yet (the floating theory is S3)",
            type->toString());
    }
    return std::format("the element type '{}' is not translated", type->toString());
}

/// Why the rule's own parameters are outside the fragment, empty when they are all integer scalars.
/// This is asked before the sides are walked, so that a rule refused by the class design 8.1 lists
/// (a whole tensor, a floating width) says so, whatever operators stand above its parameters.
std::string parameterRefusal(const GCGraph &graph) {
    const auto *type = graph.funcType();
    if (type == nullptr) {
        return kUnreadable;
    }
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        const Type *parameter = type->normTypeAt(i);
        if (!widthOf(parameter)) {
            return uncoveredType(parameter);
        }
    }
    return {};
}

/// Why a node the layer does not read is not translated.
std::string uncoveredNode(const GCNode &node) {
    if (isWholeTensor(node.dataType())) {
        return "the property is over a whole tensor";
    }
    return std::format("the node kind {} is not translated", kindName(node.kind));
}

struct IntegerOperator {
    Term::Kind kind = Term::Kind::Add;
    size_t arity    = 2;
};

/// The operators of the fragment: the integer `+ - * / %` and unary `-`, in both widths. The URI
/// carries the width (`_l` is `int`, `_i` is `i32`); the node's own type is what the term records.
std::optional<IntegerOperator> integerOperator(std::string_view uri) {
    using Kind = Term::Kind;
    static const std::unordered_map<std::string_view, IntegerOperator> table{
        {":op/add_l", {Kind::Add, 2}},      {":op/add_i", {Kind::Add, 2}},
        {":op/sub_l", {Kind::Sub, 2}},      {":op/sub_i", {Kind::Sub, 2}},
        {":op/mul_l", {Kind::Multiply, 2}}, {":op/mul_i", {Kind::Multiply, 2}},
        {":op/div_l", {Kind::Divide, 2}},   {":op/div_i", {Kind::Divide, 2}},
        {":op/mod_l", {Kind::Remainder, 2}}, {":op/mod_i", {Kind::Remainder, 2}},
        {":op/neg_l", {Kind::Negate, 1}},   {":op/neg_i", {Kind::Negate, 1}},
    };
    const auto found = table.find(uri);
    if (found == table.end()) {
        return std::nullopt;
    }
    return found->second;
}

/// The signed value of a constant, read at its own width: the two's complement bits an `int` or an
/// `i32` holds are the value the rule compares.
int64_t signedValue(const Term &term) {
    if (term.width == Width::Int32) {
        return static_cast<int32_t>(static_cast<uint32_t>(term.value));
    }
    return static_cast<int64_t>(term.value);
}

/// The parameter names of a rule's side: the declaration's own names, empty when the signature
/// records none (the renderer then names the position).
std::vector<std::string> parameterNames(const GCGraph &graph) {
    std::vector<std::string> names;
    const auto *type = graph.funcType();
    if (type == nullptr) {
        return names;
    }
    names.reserve(type->normTypesCount());
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        std::string name;
        if (type->hasArgNames()) {
            const size_t at = type->withTypesCount() + i;
            if (at < type->argNamesCount()) {
                name = std::string(type->argNameAt(at));
            }
        }
        names.push_back(std::move(name));
    }
    return names;
}

/// One side of a rule, walked from its result node back through the inputs. The first refusal is
/// kept: the walk never continues past it, so the reason is the first thing that is not the
/// fragment, in the order the side is written.
class SideWalker {
  public:
    SideWalker(const GCGraph &graph, std::vector<std::string> names)
        : graph_(graph), names_(std::move(names)),
          ports_(graph.normPorts().begin(), graph.normPorts().end()) {}

    TermPtr walk(gc_node_ref_t root) {
        if (root == camel::runtime::kInvalidNodeRef) {
            why_ = kUnreadable;
            return nullptr;
        }
        return term(root);
    }

    /// Why the walk stopped; empty when it did not.
    const std::string &why() const { return why_; }

  private:
    void refuse(const std::string &why) {
        if (why_.empty()) {
            why_ = why;
        }
    }

    TermPtr term(gc_node_ref_t ref) {
        if (!why_.empty()) {
            return nullptr;
        }
        const auto found = memo_.find(ref);
        if (found != memo_.end()) {
            return found->second;
        }
        const GCNode *node = graph_.node(ref);
        if (node == nullptr || node->kind == GCNodeKind::Dead) {
            refuse(kUnreadable);
            return nullptr;
        }
        TermPtr made;
        switch (node->kind) {
        case GCNodeKind::Data:
            made = constant(*node);
            break;
        case GCNodeKind::Port:
            made = parameter(ref, *node);
            break;
        case GCNodeKind::Cast:
        case GCNodeKind::Copy:
            made = forwarded(ref, *node);
            break;
        case GCNodeKind::Oper:
            made = operate(ref, *node);
            break;
        default:
            refuse(uncoveredNode(*node));
            break;
        }
        if (made != nullptr) {
            memo_.emplace(ref, made);
        }
        return made;
    }

    TermPtr constant(const GCNode &node) {
        if (node.dataIndex >= 0) {
            refuse(kUnreadable);
            return nullptr;
        }
        const size_t index = static_cast<size_t>(-node.dataIndex);
        const auto slots   = graph_.staticSlots();
        if (index >= slots.size()) {
            refuse(kUnreadable);
            return nullptr;
        }
        const auto width = widthOf(node.dataType());
        if (!width) {
            refuse(uncoveredType(node.dataType()));
            return nullptr;
        }
        auto made    = std::make_shared<Term>();
        made->kind   = Term::Kind::Constant;
        made->width  = *width;
        made->value  = *width == Width::Int32
                           ? static_cast<uint64_t>(
                                 static_cast<uint32_t>(camel::core::rtdata::fromSlot<Int32>(slots[index])))
                           : static_cast<uint64_t>(camel::core::rtdata::fromSlot<Int64>(slots[index]));
        return made;
    }

    TermPtr parameter(gc_node_ref_t ref, const GCNode &node) {
        size_t index = 0;
        for (; index < ports_.size(); ++index) {
            if (ports_[index] == ref) {
                break;
            }
        }
        if (index >= ports_.size()) {
            refuse(kUnreadable);
            return nullptr;
        }
        const auto width = widthOf(node.dataType());
        if (!width) {
            refuse(uncoveredType(node.dataType()));
            return nullptr;
        }
        auto made         = std::make_shared<Term>();
        made->kind        = Term::Kind::Parameter;
        made->width       = *width;
        made->parameter   = static_cast<uint32_t>(index);
        if (index < names_.size()) {
            made->name = names_[index];
        }
        return made;
    }

    /// A CAST or a COPY that hands the same value on is the value itself. A cast that changes the
    /// type is a node the fragment does not carry yet, and it is refused by name.
    TermPtr forwarded(gc_node_ref_t ref, const GCNode &node) {
        const auto inputs = graph_.normInputsOf(ref);
        if (inputs.size() != 1) {
            refuse(uncoveredNode(node));
            return nullptr;
        }
        const GCNode *input = graph_.node(inputs.front());
        if (input == nullptr) {
            refuse(kUnreadable);
            return nullptr;
        }
        if (input->dataType() != node.dataType()) {
            refuse(uncoveredNode(node));
            return nullptr;
        }
        return term(inputs.front());
    }

    TermPtr operate(gc_node_ref_t ref, const GCNode &node) {
        const auto *body      = graph_.nodeBodyAs<GCOperBody>(ref);
        const std::string uri = body == nullptr ? std::string() : std::string(body->uri());
        const auto inputs     = graph_.normInputsOf(ref);
        const auto found      = integerOperator(uri);
        if (!found || !graph_.withInputsOf(ref).empty() || !graph_.waitInputsOf(ref).empty() ||
            inputs.size() != found->arity) {
            refuse(std::format("the operator '{}' is not translated", uri));
            return nullptr;
        }
        const auto width = widthOf(node.dataType());
        if (!width) {
            refuse(uncoveredType(node.dataType()));
            return nullptr;
        }
        std::vector<TermPtr> operands;
        operands.reserve(inputs.size());
        for (gc_node_ref_t input : inputs) {
            TermPtr operand = term(input);
            if (operand == nullptr) {
                return nullptr;
            }
            operands.push_back(std::move(operand));
        }
        for (const TermPtr &operand : operands) {
            if (operand->width != *width) {
                refuse(std::format("the operator '{}' is not translated", uri));
                return nullptr;
            }
        }
        auto made    = std::make_shared<Term>();
        made->kind   = found->kind;
        made->width  = *width;
        made->left   = operands.front();
        made->right  = operands.size() > 1 ? operands[1] : nullptr;
        return made;
    }

    const GCGraph &graph_;
    std::vector<std::string> names_;
    std::vector<gc_node_ref_t> ports_;
    std::unordered_map<gc_node_ref_t, TermPtr> memo_;
    std::string why_;
};

} // namespace

const char *widthName(Width width) {
    return width == Width::Int32 ? "i32" : "int";
}

std::string render(const Term &term) {
    switch (term.kind) {
    case Term::Kind::Constant:
        return std::to_string(signedValue(term));
    case Term::Kind::Parameter:
        return term.name.empty() ? std::format("x{}", term.parameter) : term.name;
    case Term::Kind::Negate:
        return std::format("(-{})", term.left == nullptr ? std::string("?") : render(*term.left));
    case Term::Kind::Add:
    case Term::Kind::Sub:
    case Term::Kind::Multiply:
    case Term::Kind::Divide:
    case Term::Kind::Remainder:
        break;
    }
    const char *op = term.kind == Term::Kind::Add        ? " + "
                     : term.kind == Term::Kind::Sub      ? " - "
                     : term.kind == Term::Kind::Multiply ? " * "
                     : term.kind == Term::Kind::Divide   ? " / "
                                                         : " % ";
    const std::string left  = term.left == nullptr ? std::string("?") : render(*term.left);
    const std::string right = term.right == nullptr ? std::string("?") : render(*term.right);
    return std::format("({}{}{})", left, op, right);
}

std::string Translation::proposition() const {
    if (lhs == nullptr || rhs == nullptr) {
        return {};
    }
    return render(*lhs) + " = " + render(*rhs);
}

std::string Translation::column() const {
    if (!covered) {
        return uncoveredColumn(reason);
    }
    std::string column =
        std::format("integer scalar {}: {}", widthName(width), proposition());
    if (conditional) {
        column += ", no premise yet";
    }
    return column;
}

Translation translate(const camel::runtime::RuleSource &source) {
    Translation out;
    const camel::runtime::Program *program = source.program;
    if (program == nullptr || source.lhs == camel::runtime::kInvalidGraphId ||
        source.rhs == camel::runtime::kInvalidGraphId) {
        out.reason = kNotBuilt;
        return out;
    }
    const GCGraph *lhs = program->graph(source.lhs);
    const GCGraph *rhs = program->graph(source.rhs);
    if (lhs == nullptr || rhs == nullptr) {
        out.reason = kUnreadable;
        return out;
    }
    // The class the rule's own parameters belong to is asked first: a tensor law is refused as a
    // whole-tensor property and a floating law as an element type, not by whichever operator happens
    // to stand at the top of its side.
    if (const std::string refused = parameterRefusal(*lhs); !refused.empty()) {
        out.reason = refused;
        return out;
    }
    if (const std::string refused = parameterRefusal(*rhs); !refused.empty()) {
        out.reason = refused;
        return out;
    }
    // The names belong to the declaration, and both sides are the same declaration's two functions.
    const std::vector<std::string> names = parameterNames(*lhs);
    SideWalker left(*lhs, names);
    out.lhs = left.walk(lhs->exitNodeRef());
    if (out.lhs == nullptr) {
        out.reason = left.why().empty() ? kUnreadable : left.why();
        return out;
    }
    SideWalker right(*rhs, names);
    out.rhs = right.walk(rhs->exitNodeRef());
    if (out.rhs == nullptr) {
        out.reason = right.why().empty() ? kUnreadable : right.why();
        return out;
    }
    if (out.lhs->width != out.rhs->width) {
        out.reason = "the two sides work in different widths";
        out.lhs    = nullptr;
        out.rhs    = nullptr;
        return out;
    }
    out.covered     = true;
    out.width       = out.lhs->width;
    out.conditional = static_cast<bool>(source.when);
    return out;
}

std::string uncoveredColumn(const std::string &reason) {
    return "beyond translation coverage: " + (reason.empty() ? std::string(kUnreadable) : reason);
}

const std::string &coverageTable() {
    static const std::string table =
        "the coverage of the integer scalar translation\n"
        "widths translated: int i32\n"
        "widths declared and not translated yet: float f32 bf16 f16 (the floating theory is S3)\n"
        "operators translated: :op/add_l :op/add_i :op/sub_l :op/sub_i :op/mul_l :op/mul_i "
        ":op/div_l :op/div_i :op/mod_l :op/mod_i :op/neg_l :op/neg_i\n"
        "division and remainder: translated, and the fault at a zero divisor is part of the term "
        "(the decision carries it)\n"
        "beyond translation coverage: a whole tensor, a width or an operator or a node kind not "
        "listed above\n"
        "a rule's condition: the sides are translated, and the condition is not a premise yet (S4)";
    return table;
}

} // namespace camel::verify