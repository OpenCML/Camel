// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::defun_value: a function value that captures nothing and names one function of the program
 * is a tag.
 *
 * The value a program builds for such a function is a `DATA` node whose static slot holds the
 * `::Function` itself; a call through it is a `CALL` that reads the value and dispatches. This
 * pass gives the value the form the design asks for -- an enum value with one variant and no
 * data, so the tag is in the value's type -- and turns every call through it into a direct call
 * of that function. Because the type has one variant, the dispatch has one arm and is folded
 * away: no branch is built.
 *
 * It is the first slice of defunctionalization (the design note committed as df3796086, which the lane
 * it is deliberately narrow. It rewrites a function type family only when:
 *
 *  - every value of the family is a plain reference to one function (a static `DATA` node holding
 *    a `::Function`): no closure is built anywhere for it, and no other function is written;
 *  - that function's graph captures nothing (no closure nodes);
 *  - everything that reads a value is either the callee of a `CALL` or an argument of a `FUNC`
 *    call -- nothing else observes the value, so replacing its representation changes nothing
 *    observable;
 *  - every `CALL` through the family can become a direct call: the callee is the value or a
 *    parameter and it waits on nothing;
 *  - the family is not an argument of the entry function, and it does not sit inside a named
 *    aggregate (a struct field): the substitution reaches node types, ports and signatures, and
 *    the pass refuses what it cannot see rather than leaving half the program on the old type.
 *
 * A *family* is the set of signatures one value is, not one signature: a function type is
 * structural (design principles 5.2), and the same function value is written `(x: int) => int` in
 * one place and `(x: int) => int pure` in the one that takes it. The two are one value at run
 * time, so one enum is made per family -- and the members are the signatures with the same
 * parameters and the same result, compared with the effect qualifiers left out (5.2: an effect
 * qualifier is a subtype relation, not another type). Not the ones that happen to be assignable to
 * each other: assignability is neither symmetric nor transitive, a `() => void` takes a
 * `() => int` without being the same family, and a family is one type, so its grouping is an
 * equality.
 *
 * The type substitution and the node rewrite are one commit. `retype.h` says why: after the
 * rebuild the graph says the value is the enum while the static area still holds the function
 * object, so the graph would not run until the nodes that build and call the value are rewritten.
 * The commit's own validator says it for the other order: between the two halves the graph hands
 * an enum value to a parameter that still says function, which is not a program.
 */

#include "defun_value.h"

#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/other.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "camel/runtime/retype.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace camel::runtime;
namespace ty = camel::core::type;

using ty::TypeCode;
using ty::TypeId;

/// One function type the program gives a value, and what the program does with it.
struct TypePlan {
    ty::Type *type = nullptr;
    GraphId target = GraphId::Invalid;
    std::string targetName;
    std::string refuse;
    size_t values = 0;
    size_t calls  = 0;
};

/// The signatures one value is: the members have the same parameters and result, so one function
/// value is written as any of them and the run time holds one thing. Effects are left out of that
/// comparison (design principles 5.2), so a member may promise `pure` where another promises
/// nothing -- and, being the same type, they are one family whichever direction a value flows.
struct Family {
    std::vector<TypePlan *> members;
    GraphId target = GraphId::Invalid;
    std::string targetName;
    std::string refuse;
    size_t values = 0;
    size_t calls  = 0;
};

::Function *staticFunctionOf(const GCGraph &graph, const GCNode &node) {
    if (node.dataIndex >= 0 || !graph.staticArea()) {
        return nullptr;
    }
    const size_t index = size_t(-node.dataIndex);
    if (index >= graph.staticSlots().size()) {
        return nullptr;
    }
    const slot_t slot = graph.staticArea()->get<slot_t>(index);
    if (slot == NullSlot) {
        return nullptr;
    }
    auto *function = camel::core::rtdata::fromSlot<::Function *>(slot);
    return function != nullptr && function->graph() != nullptr ? function : nullptr;
}

/// Whether `type` holds `target` anywhere a substitution has to reach: as itself, in the signature
/// of a function type, or in the components of an aggregate the type keeps as a component (a tuple,
/// an array, an enum). A struct keeps its fields by name; that is what the pass refuses instead of
/// seeing.
bool mentions(const ty::Type *type, const ty::Type *target, int depth) {
    if (type == nullptr || target == nullptr || depth > 8) {
        return false;
    }
    if (type == target || type->typeId() == target->typeId() ||
        type->equals(const_cast<ty::Type *>(target))) {
        return true;
    }
    if (type->code() == TypeCode::Function) {
        const auto *function = static_cast<const ty::FunctionType *>(type);
        for (const ty::Type *param : function->withTypesSpan()) {
            if (mentions(param, target, depth + 1)) {
                return true;
            }
        }
        for (const ty::Type *param : function->normTypesSpan()) {
            if (mentions(param, target, depth + 1)) {
                return true;
            }
        }
        return mentions(function->exitType(), target, depth + 1);
    }
    const auto *other = dynamic_cast<const ty::OtherType *>(type);
    if (other == nullptr) {
        return false;
    }
    for (const ty::Type *component : other->paramsSpan()) {
        if (mentions(component, target, depth + 1)) {
            return true;
        }
    }
    return false;
}

std::string identifierOf(std::string_view name, std::string_view fallback) {
    std::string out;
    for (char c : name) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
    }
    if (out.empty()) {
        out = std::string(fallback);
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::span<const std::byte> payloadOf(const void *body, size_t size) {
    return std::span<const std::byte>(static_cast<const std::byte *>(body), size);
}

/// Whether two signatures are one value's type: the same parameters and the same result, in
/// structure, with the effect qualifiers left out. `pure` and a resource clause say which
/// executions a value may take part in, and a `pure` value goes where the plain signature is
/// expected without a conversion, so they do not make another type (design principles 5.2).
bool sameFamily(ty::Type *left, ty::Type *right) {
    return left != nullptr && right != nullptr && left->code() == TypeCode::Function &&
           right->code() == TypeCode::Function && ty::typeEqualsIgnoringResources(left, right);
}

} // namespace

const camel::runtime::Program *
DefunValueRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;

    if (program == nullptr || program->root() == nullptr) {
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (1) What the program holds: values of a function type, and calls through one.
    // ---------------------------------------------------------------------------------------
    std::map<TypeId, TypePlan> plans;
    std::map<uint64_t, std::set<gc_node_ref_t>> valueRefs; // graph id -> the nodes that hold one

    const auto planFor = [&](ty::Type *type) -> TypePlan & {
        TypePlan &plan = plans[type->typeId()];
        plan.type      = type;
        return plan;
    };
    const auto refuseIfUnsaid = [](TypePlan &plan, std::string why) {
        if (plan.refuse.empty()) {
            plan.refuse = std::move(why);
        }
    };

    for (const GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode &node = **it;
            ty::Type *type     = node.dataType();
            if (type == nullptr || type->code() != TypeCode::Function) {
                continue;
            }
            // A parameter of the type is where a value arrives; it is not a value the program
            // builds, and it is rewritten with the signature.
            if (node.kind == GCNodeKind::Port) {
                planFor(type);
                continue;
            }
            TypePlan &plan = planFor(type);
            if (node.kind == GCNodeKind::Fill) {
                refuseIfUnsaid(plan, "a closure is built somewhere for the type");
                continue;
            }
            if (node.kind != GCNodeKind::Data || node.dataIndex >= 0) {
                refuseIfUnsaid(plan, "a value is not a plain reference to a function");
                continue;
            }
            ::Function *function = staticFunctionOf(*graph, node);
            if (function == nullptr) {
                refuseIfUnsaid(plan, "a value holds something that is not a function of the program");
                continue;
            }
            const GCGraph *target = function->graph();
            if (plan.target == GraphId::Invalid) {
                plan.target     = target->id();
                plan.targetName = std::string(target->name());
            } else if (plan.target != target->id()) {
                refuseIfUnsaid(plan, "the type stands for more than one function");
            }
            if (!target->closureNodes().empty()) {
                refuseIfUnsaid(plan, "the function captures values");
            }
            valueRefs[toValue(graph->id())].insert(it.ref());
            ++plan.values;
        }
    }

    if (plans.empty()) {
        os << "DEFUN_VALUE types=0 values=0 calls=0 refused=0\n";
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (2) What else reads a value, and every call through the type.
    // ---------------------------------------------------------------------------------------
    for (const GCGraph *graph : program->graphs()) {
        const auto &values = valueRefs[toValue(graph->id())];
        std::map<gc_node_ref_t, const GCNode *> nodes;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            nodes.emplace(it.ref(), *it);
        }
        const auto typeOf = [&](gc_node_ref_t ref) -> ty::Type * {
            const auto found = nodes.find(ref);
            return found == nodes.end() ? nullptr : found->second->dataType();
        };
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const GCNode &node     = **it;
            const gc_node_ref_t ref = it.ref();
            if (node.kind == GCNodeKind::Call) {
                const auto withs = graph->withInputsOf(ref);
                if (withs.empty()) {
                    continue;
                }
                ty::Type *calleeType = typeOf(withs.front());
                if (calleeType == nullptr || calleeType->code() != TypeCode::Function) {
                    continue;
                }
                TypePlan &plan = planFor(calleeType);
                ++plan.calls;
                const GCNode *callee = nodes.at(withs.front());
                if (callee->kind != GCNodeKind::Data && callee->kind != GCNodeKind::Port) {
                    refuseIfUnsaid(
                        plan, "a call reads a callee that is neither a value nor a parameter");
                }
                if (!graph->waitInputsOf(withs.front()).empty()) {
                    refuseIfUnsaid(plan, "a call's callee waits on something");
                }
            }
            // Nothing else may read a value: the rewrite changes what the value is.
            const auto checkReader = [&](std::span<const gc_node_ref_t> inputs) {
                for (gc_node_ref_t input : inputs) {
                    if (values.find(input) == values.end()) {
                        continue;
                    }
                    ty::Type *inputType = typeOf(input);
                    if (inputType == nullptr) {
                        continue;
                    }
                    const auto withs = graph->withInputsOf(ref);
                    const bool readsAsCallee =
                        node.kind == GCNodeKind::Call && !withs.empty() && withs.front() == input;
                    const auto norms = graph->normInputsOf(ref);
                    const bool readsAsArgument =
                        node.kind == GCNodeKind::Func &&
                        std::find(norms.begin(), norms.end(), input) != norms.end();
                    if (!readsAsCallee && !readsAsArgument) {
                        refuseIfUnsaid(
                            plans[inputType->typeId()], "the value is read by a node that is not a call");
                    }
                }
            };
            checkReader(graph->normInputsOf(ref));
            checkReader(graph->withInputsOf(ref));
            checkReader(graph->ctrlInputsOf(ref));
        }
    }

    // ---------------------------------------------------------------------------------------
    // (3) One family per type: the signatures that are one value's type, effect qualifiers aside.
    // ---------------------------------------------------------------------------------------
    std::vector<Family> families;
    std::set<TypeId> assigned;
    for (auto &[id, plan] : plans) {
        if (assigned.count(id) != 0) {
            continue;
        }
        families.emplace_back();
        Family &family = families.back();
        for (auto &[otherId, other] : plans) {
            if (assigned.count(otherId) != 0 || !sameFamily(plan.type, other.type)) {
                continue;
            }
            assigned.insert(otherId);
            family.members.push_back(&other);
        }
    }
    for (Family &family : families) {
        for (TypePlan *member : family.members) {
            family.values += member->values;
            family.calls += member->calls;
            if (family.refuse.empty()) {
                family.refuse = member->refuse;
            }
            if (member->target != GraphId::Invalid) {
                if (family.target == GraphId::Invalid) {
                    family.target     = member->target;
                    family.targetName = member->targetName;
                } else if (family.target != member->target) {
                    family.refuse = "the type stands for more than one function";
                }
            }
        }
        if (family.values == 0 && family.refuse.empty()) {
            family.refuse = "the type has no value the program builds";
        }
    }

    // ---------------------------------------------------------------------------------------
    // (4) The entry function.
    // ---------------------------------------------------------------------------------------
    if (const auto *rootFunction = program->root()->funcType()) {
        for (const ty::Type *param : rootFunction->normTypesSpan()) {
            for (Family &family : families) {
                for (TypePlan *member : family.members) {
                    if (family.refuse.empty() && mentions(param, member->type, 0)) {
                        family.refuse = "the type is an argument of the entry function";
                    }
                }
            }
        }
    }

    std::vector<Family *> accepted;
    for (Family &family : families) {
        if (family.refuse.empty() && family.values != 0 && family.target != GraphId::Invalid) {
            accepted.push_back(&family);
        }
    }
    if (accepted.empty()) {
        os << "DEFUN_VALUE types=0 values=0 calls=0 refused=" << families.size() << "\n";
        return program;
    }

    // ---------------------------------------------------------------------------------------
    // (5) The enum per family, and the table that retypes every object equal to a member.
    //
    // A function type is structural: the same signature may be a different object with a different
    // id, one per place it is written. The table names ids, so every object equal to a member goes
    // in it -- from node types and from the signature of every graph. Missing one leaves half the
    // program on the old type, which is the state the rebuild cannot run in.
    // ---------------------------------------------------------------------------------------
    std::map<TypeId, std::set<ty::Type *>> equivalents; // member type id -> equal objects
    const auto noteType = [&](ty::Type *type) {
        if (type == nullptr) {
            return;
        }
        for (Family *family : accepted) {
            for (TypePlan *member : family->members) {
                if (type->equals(member->type)) {
                    equivalents[member->type->typeId()].insert(type);
                }
            }
        }
    };
    for (const GCGraph *graph : program->graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            noteType((**it).dataType());
        }
        const auto *signature = graph->funcType();
        if (signature != nullptr) {
            for (ty::Type *type : signature->withTypesSpan()) {
                noteType(type);
            }
            for (ty::Type *type : signature->normTypesSpan()) {
                noteType(type);
            }
            noteType(signature->exitType());
        }
    }

    GraphTypeSubstitution types;
    std::map<TypeId, GraphId> enumTargets; // enum type id -> the function it names
    size_t index = 0;
    for (Family *family : accepted) {
        const std::string name =
            "FnValue_" + identifierOf(family->targetName, "f") + "_" + std::to_string(index++);
        ty::EnumDecl *decl = ty::EnumDecl::create(name, {}, std::string());
        ty::EnumVariant variant;
        variant.name  = identifierOf(family->targetName, "f");
        variant.shape = ty::VariantShape::Unit;
        decl->define({variant});
        ty::EnumType *enumType = decl->prototype();
        for (TypePlan *member : family->members) {
            for (ty::Type *type : equivalents[member->type->typeId()]) {
                types.replace(type, enumType);
            }
            types.replace(member->type, enumType);
        }
        enumTargets.emplace(enumType->typeId(), family->target);
    }

    GraphRetypeOptions options;
    options.leaveStaticObjects = true; // the value's object is replaced by the node rewrite below

    // ---------------------------------------------------------------------------------------
    // (6) One commit: the type of every value, its constructor and its call change together.
    // ---------------------------------------------------------------------------------------
    size_t rewroteValues = 0, rewroteCalls = 0;
    {
        RuntimeGraphDraftSession session(context_, program);
        for (GCGraph *graph : session.program()->graphs()) {
            auto &draft = session.edit(graph).edit();
            retypeDraft(draft, types, options);

            std::vector<gc_node_ref_t> tagNodes, callNodes;
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                const gc_node_ref_t id = draft.draftIdOfSourceRef(it.ref());
                const DraftNodeHeader *header = draft.header(id);
                if (header == nullptr || header->dataType == nullptr) {
                    continue;
                }
                // The value a type names is its own type; the call's own type is the result, so
                // the callee is what says whether the call goes through a value of the family.
                if (header->kind == GCNodeKind::Data && header->dataIndex < 0 &&
                    enumTargets.find(header->dataType->typeId()) != enumTargets.end()) {
                    tagNodes.push_back(id);
                    continue;
                }
                if (header->kind == GCNodeKind::Call) {
                    const auto withs = draft.withInputsOf(id);
                    if (withs.empty()) {
                        continue;
                    }
                    const DraftNodeHeader *calleeHeader = draft.header(withs.front());
                    if (calleeHeader != nullptr && calleeHeader->dataType != nullptr &&
                        enumTargets.find(calleeHeader->dataType->typeId()) != enumTargets.end()) {
                        callNodes.push_back(id);
                    }
                }
            }
            for (gc_node_ref_t id : tagNodes) {
                ty::Type *enumType      = draft.header(id)->dataType;
                const gc_node_ref_t tag = draft.materializeStaticValue(
                    camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(0),
                    ty::Type::Int64());
                GCOperBody body;
                body.op   = nullptr;
                body.oper = camel::core::OperatorTable::instance().intern(":enum/make");
                DraftNodeInit init;
                init.kind       = GCNodeKind::Oper;
                init.dataType   = enumType;
                init.dataIndex  = draft.allocateRuntimeSlot(enumType);
                init.payload    = payloadOf(&body, sizeof(body));
                init.normInputs = std::span<const gc_node_ref_t>(&tag, 1);
                draft.rewriteNode(id, init);
                ++rewroteValues;
            }
            for (gc_node_ref_t id : callNodes) {
                const DraftNodeHeader *header = draft.header(id);
                const auto withs              = draft.withInputsOf(id);
                if (withs.empty()) {
                    continue;
                }
                const DraftNodeHeader *calleeHeader = draft.header(withs.front());
                if (calleeHeader == nullptr || calleeHeader->dataType == nullptr) {
                    continue;
                }
                const auto found = enumTargets.find(calleeHeader->dataType->typeId());
                if (found == enumTargets.end()) {
                    continue;
                }
                const std::vector<gc_node_ref_t> normInputs(
                    draft.normInputsOf(id).begin(), draft.normInputsOf(id).end());
                const std::vector<gc_node_ref_t> directWith(withs.begin() + 1, withs.end());
                GCFuncRef callee{.ref = draft.payloadGraphIndexOf(found->second)};
                DraftNodeInit init;
                init.kind       = GCNodeKind::Func;
                init.dataType   = header->dataType;
                init.dataIndex  = header->dataIndex;
                init.payload    = payloadOf(&callee, sizeof(callee));
                init.normInputs = normInputs;
                init.withInputs = directWith;
                draft.rewriteNode(id, init);
                ++rewroteCalls;
            }
        }
        const Program *done = session.commit();
        if (done == nullptr) {
            os << "DEFUN_VALUE commit_failed=1\n";
            return program;
        }
        os << "DEFUN_VALUE types=" << accepted.size() << " values=" << rewroteValues
           << " calls=" << rewroteCalls << " refused=" << (families.size() - accepted.size())
           << "\n";
        for (Family *family : accepted) {
            os << "DEFUN_VALUE type='" << family->members.front()->type->toString() << "' variant='"
               << identifierOf(family->targetName, "f") << "' values=" << family->values
               << " calls=" << family->calls << "\n";
        }
        return done;
    }
}