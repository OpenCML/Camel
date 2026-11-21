// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A write: `a op= b` (compoundAssignmentByRebinding), the in-place rebinding of a cell
 * (inPlaceRebindingOf), a write through a path -- `v.f = e`, `v[i] = e` (pathAssignmentOf) --
 * and the ASSIGN node itself, both the rebinding of a `var` built as a value
 * (ssaAssignmentOf) and the store into a cell it lowers to (visitAssignNode,
 * makeStoreOperator, storeOperatorOrigin).
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/primary.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/fault_isolation.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/other.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <set>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {
node_handle_t Builder::compoundAssignmentByRebinding(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
    const type_vec_t &normTypes) {
    // `a op= b` is `a = a op b` (design principles 4.2): one rule for every type, whoever
    // declared it — a number's built-in `+`, a `+` the tensor module declares for a numeric
    // array, an `operator +` of the program. What the compound assignment follows is that
    // operator, so what is asked of it is what design principles 4.2 says: that `a op b` be
    // defined for these operands, and that `a = a op b` be a legal assignment — the value the
    // operator gives fits `a`'s type where the assignment would put it (an addition broadcasts
    // `f32[]` and `f32[2]` to `f32[2]`, which a `var` holding `f32[]` takes). `int[]` has no
    // `+`, and a `+` whose value no assignment to `a` would accept is not a `+=` either; both
    // are refused here, and the call reports the compound assignment the program wrote.
    const auto plainGroup = camel::core::plainGroupOfCompoundAssignment(group);
    if (!plainGroup || operands.size() != 2 || normTypes.size() != operands.size()) {
        return nullptr;
    }
    const std::string plainName(*plainGroup);
    if (!plainOperatorAvailableFor(plainName, operands, normTypes)) {
        return nullptr;
    }
    // `a`'s type as the operator of `a op b` sees it: the two are compared where the operator
    // is resolved, so the comparison asks about one and the same reading of the type.
    Type *target = valueTypeOf(normTypes[0]);
    if (target == nullptr) {
        return nullptr;
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(gct->origin());
        return node;
    };
    // The value keeps the compound operator's own origin: what a fault of the operator is
    // reported at is the `%=` as written (a `%=` by zero is a DivisionByZero at the `%=`,
    // test/plans/feat/linear/operators.plan.toml). The write keeps the statement's, so the
    // statement is still the whole assignment where something asks for it (the LSP's
    // navigation, test/plans/feat/tools/lsp.plan.toml).
    const auto makeOperator = [&](GCT::node_ptr_t node) {
        node->setOrigin(
            gct->size() > 0 && gct->at(0) != nullptr ? gct->at(0)->origin() : gct->origin());
        return node;
    };
    // The right operand is evaluated once: it was built already, and is named here.
    const std::string right = bindTemporary(valueOf(operands[1].node));
    // The place is read as it is written, by the link below: `a op= b` reads `a` with the read
    // `a = a op b` reads it with, built where that reading is built and settled by the same rule
    // (design principles 2.1, 6.11: whether the read keeps the place's array for the store to
    // write in place is the memory plan's question, Builder::markNonRetainingReads). The place is
    // written again rather than named from the operand the compound operator built, so that the
    // two spellings make their nodes in one order and write one graph; a place that is not a
    // variable of the program is built as written too, and its own report is the one given.
    GCT::node_ptr_t value = makeOperator(tree_->makeLink(2));
    *value << makeOperator(tree_->makeDRef(plainName));
    *value << tree_->clone(operands[0].expr);
    *value << make(tree_->makeDRef(right));
    // The plain operator is resolved as one written by itself would be: this is where its
    // availability is settled for the arguments, and where its result type is known. A result
    // the assignment to `a` would not take is not this assignment's meaning, so the rebinding
    // does not apply and the compound assignment is reported as undefined, exactly as for a
    // type that has no plain operator at all.
    // A write of this shape reads the name it writes: what it stores is not a value nobody could
    // read, and the report of a dropped value stays off every link it rebuilds (ssaAssignmentOf).
    struct CompoundGuard {
        bool &flag;
        bool outer;
        explicit CompoundGuard(bool &f) : flag(f), outer(f) { flag = true; }
        ~CompoundGuard() { flag = outer; }
    } compoundGuard(inCompoundAssignment_);
    const node_handle_t plain = visitLinkNode(value);
    Type *result              = valueTypeOf(nodeTypeOf(plain));
    if (result == nullptr || !target->assignableFrom(result)) {
        return nullptr;
    }
    // The value the plain operator gave is named for the assignment, so that it is computed
    // once: the assignment stores what the operator answered.
    const std::string held = bindTemporary(valueOf(plain));
    GCT::node_ptr_t assign = make(tree_->makeAssign(GCT::AssignmentForm::Plain));
    *assign << tree_->clone(operands[0].expr) << make(tree_->makeDRef(held));
    return visitAssignNode(assign);
}

oper_group_ptr_t Builder::importedOperatorGroupNamed(const std::string &name) {
    if (nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
        decoratedGraphAt(name).has_value()) {
        return nullptr;
    }
    if (auto graphs = graphsAt(name); graphs.has_value() && !graphs.value()->empty()) {
        return nullptr;
    }
    const auto entity = importedEntity(name);
    return entity.has_value() && entity->isOperGroup() ? entity->operGroup() : nullptr;
}

node_handle_t Builder::inPlaceRebindingOf(const GCT::node_ptr_t &gct) {
    // The lowered form of `xs = xs->op<w...>` (and of `xs = op<w...>(xs)`, `xs = xs->op`):
    //   ASSIGN(Plain, DREF xs, LINK(WITH(DREF op, w...) | DREF op, DREF xs))
    // In a sync block only: the rewritten statement is three operations on the variable,
    // which must run together and in this order.
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    if (!synced_ || gct->type() != GCT::NodeKind::ASSIGN ||
        gct->assignmentForm() != GCT::AssignmentForm::Plain || !isDref(gct->assignmentTarget())) {
        return nullptr;
    }
    const GCT::node_ptr_t target = gct->assignmentTarget();
    const GCT::node_ptr_t call   = gct->assignmentValue();
    if (call->type() != GCT::NodeKind::LINK || call->size() != 2 ||
        !isDref(call->at(1), target->name())) {
        return nullptr;
    }
    const GCT::node_ptr_t callee = call->at(0);
    const bool withArgs          = callee->type() == GCT::NodeKind::WITH;
    const GCT::node_ptr_t opName = withArgs ? callee->at(0) : callee;
    if (!isDref(opName)) {
        return nullptr;
    }
    // `xs` is a variable (of this function or a captured one) holding an array.
    const std::string name = target->name();
    if (!nodeAt(name).has_value()) {
        return nullptr;
    }
    const node_handle_t variable = resolveNodeByRef(name);
    // The name a closure captured is a PORT and not a variable once the capture is by value
    // (design principles 3.5, 197): the rewrite below writes it, and a value has nowhere to write.
    // The capture's rule answers that write, and it has to answer it HERE, before the rewrite is
    // chosen: a rewrite that declines to run leaves the call as written, and the report becomes
    // `NoMatchingFunction` -- about the operator, naming neither the write nor the way out.
    if (variable != nullptr && (isVariable(variable) || capturePortOfAVariable(variable))) {
        requireCapturedNameWritable(variable, name, false, target->origin());
    }
    Type *arrayType = valueTypeOf(nodeTypeOf(variable));
    if (!isVariable(variable) || arrayType == nullptr || arrayType->code() != TypeCode::Array) {
        return nullptr;
    }
    // `op` is an operator of a module (nothing of the program is named so), and one of its
    // overloads declares an in-place form: anything else is an ordinary call.
    oper_group_ptr_t group = importedOperatorGroupNamed(opName->name());
    if (group == nullptr) {
        // A generic function whose `#impl` kernel has an in-place form (`sort`): the rule of the
        // kernel that is the executor for this array is the operator the rewrite is about
        // (adoptImplKernel asks the same question of an instance).
        std::vector<generic_template_ptr_t> templates;
        if (const auto scoped = templateScope_->get(opName->name()); scoped.has_value()) {
            if (GenericGroup *templated = genericGroupOf(scoped.value()); templated != nullptr) {
                templates.assign(templated->begin(), templated->end());
            }
        } else if (const auto imported = importedEntity(opName->name()); imported.has_value()) {
            templates = genericTemplatesOfEntity(imported.value());
        }
        for (const generic_template_ptr_t &tmpl : templates) {
            const auto declared = implTemplateKernels_.find(tmpl.get());
            if (declared == implTemplateKernels_.end()) {
                continue;
            }
            for (const auto &[ruleUri, resolver] : declared->second.group->resolvers()) {
                if (resolver->resolve({}, {arrayType}).has_value() &&
                    declaredRuleApplies(resolver, declared->second.name, {}, {arrayType})) {
                    group = OperatorGroup::create(
                        declared->second.group->name(),
                        std::vector<std::pair<std::string, camel::core::type::resolver_ptr_t>>{
                            {ruleUri, resolver}});
                    break;
                }
            }
            if (group != nullptr) {
                break;
            }
        }
    }
    auto &table = camel::core::OperatorTable::instance();
    if (group == nullptr || std::ranges::none_of(group->resolvers(), [&](const auto &overload) {
            return table.inPlaceForm(overload.first).has_value();
        })) {
        return nullptr;
    }
    // The with-arguments are plain values, built first as in the call as written and named
    // so that the call below refers to them; a name of a function, a binding or a lambda is
    // settled by the call it is passed to, and such a call is built as written.
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        const GCT::node_ptr_t arg = callee->at(i);
        if ((isDref(arg) && !nodeAt(arg->name()).has_value()) ||
            arg->type() == GCT::NodeKind::WITH || arg->type() == GCT::NodeKind::FUNC) {
            return nullptr;
        }
    }
    std::vector<std::string> temporaries;
    type_vec_t withTypes;
    for (size_t i = 1; withArgs && i < callee->size(); ++i) {
        node_handle_t value = valueOf(visitOperand(callee->at(i), callee));
        if (nodeIsKind(value, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(value)) {
            rejectNameWithoutValue(value, "the argument", callee->at(i));
        }
        withTypes.push_back(valueTypeOf(nodeTypeOf(value)));
        temporaries.push_back(bindTemporary(value));
    }
    const auto make = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    // The callee as written, with the with-arguments replaced by their names.
    GCT::node_ptr_t calleeCopy = tree_->clone(opName);
    if (withArgs) {
        GCT::node_ptr_t with = make(tree_->makeWith(temporaries.size()), callee);
        *with << calleeCopy;
        for (size_t i = 0; i < temporaries.size(); ++i) {
            *with << make(tree_->makeDRef(temporaries[i]), callee->at(i + 1));
        }
        calleeCopy = with;
    }
    // The overload the call resolves to, as visitLinkNode will resolve it: rewritten only when
    // it has an in-place form and gives an array of the variable's own type.
    const std::vector<StaticArg> statics(1);
    const auto resolved       = group->resolve(withTypes, {arrayType}, statics);
    const bool inPlace        = resolved.has_value() &&
                                table.inPlaceForm((*resolved)->uri()).has_value() &&
                                (*resolved)->funcType()->exitType() != nullptr &&
                                (*resolved)->funcType()->exitType()->equals(arrayType);
    GCT::node_ptr_t rebinding = make(
        tree_->makeAssign(inPlace ? GCT::AssignmentForm::Owned : GCT::AssignmentForm::Plain),
        gct);
    GCT::node_ptr_t applied   = make(tree_->makeLink(1), call);
    if (!inPlace) {
        // As written (with the arguments already built): `op` reports what does not fit.
        *applied << calleeCopy << tree_->clone(call->at(1));
        *rebinding << tree_->clone(target) << applied;
        return visitAssignNode(rebinding);
    }
    GCT::node_ptr_t take = make(tree_->makeLink(1), call->at(1));
    *take << make(internalOperatorDref(camel::core::oper::VarTake), call->at(1))
          << tree_->clone(target);
    *applied << calleeCopy << take;
    inPlaceCalls_.insert(applied);
    *rebinding << tree_->clone(target) << applied;
    return visitAssignNode(rebinding);
}

namespace {

/// One place a path assignment writes: a field (`v.f`, `v.0`) or an element (`v[i]`).
struct PathStep {
    GCT::node_ptr_t node = {}; // the ACCS or __idx__ node, as written
    bool isIndex = false; // a field of a struct or tuple, or an element of an array
    size_t field = 0;     // the position of the field
    std::string selector; // the name of the index value of an element
    std::string base;     // the name of the value the step is applied to
    std::string value;    // the name of the value the step reads
};

} // namespace

node_handle_t Builder::pathAssignmentOf(const GCT::node_ptr_t &gct) {
    // `v.f = e`, `v[i] = e`, `v[i].f += e` (design principles 3.5): the whole value of the
    // variable is rebound to a `set` chain, so everything downstream sees an ordinary
    // assignment of `v`:
    //   ASSIGN(<path>, e)  ->
    //   ASSIGN(Rebuilt, v, LINK(__set_index__, v, i, LINK(__set_field__, v[i], f, e)))
    // Each step of the path is read once, and each prefix of it is named, so that the chain
    // refers to the values the reads gave and to the places they came from.
    if (gct->type() != GCT::NodeKind::ASSIGN) {
        return nullptr;
    }
    // `=` or a compound assignment (`+=`), which is the plain operator over the place's value.
    const GCT::AssignmentForm form = gct->assignmentForm();
    std::optional<std::string_view> plainGroup;
    if (form == GCT::AssignmentForm::Compound) {
        plainGroup = camel::core::plainGroupOfCompoundAssignment(gct->assignmentOperator());
        if (!plainGroup.has_value()) {
            return nullptr;
        }
    } else if (form != GCT::AssignmentForm::Plain) {
        return nullptr; // the builder's own writes: this is not the program's path
    }
    // A compound write under a path is written as the plain operator over the place's value, and
    // the path steps are the operand's own (`y.f += e` writes `y.f`).
    const GCT::node_ptr_t &pathTarget = gct->assignmentTarget();
    const GCT::node_ptr_t &written    = gct->assignmentValue();
    const auto isDref = [](const GCT::node_ptr_t &node, std::string_view name = {}) {
        return node != nullptr && node->type() == GCT::NodeKind::DREF &&
               (name.empty() || node->name() == name);
    };
    // The path, read from its leaf back to the variable it starts at, then in the order it is
    // written. A place is a field of a struct or tuple or an element of an array; anything else
    // (`s.0` on a string, `f().x`) is left to the call as written.
    std::vector<GCT::node_ptr_t> chain;
    GCT::node_ptr_t root = pathTarget;
    while (!isDref(root)) {
        const bool element = root->type() == GCT::NodeKind::LINK && root->size() == 3 &&
                             isDref(root->at(0), "__idx__");
        if (root->type() != GCT::NodeKind::ACCS && !element) {
            return nullptr;
        }
        chain.push_back(root);
        root = element ? root->at(1) : root->at(0);
    }
    if (chain.empty()) {
        return nullptr; // `v = e`: the whole variable, which is the assignment as written
    }
    std::reverse(chain.begin(), chain.end());
    // The path starts at a variable: its whole value can be stored back into it.
    const std::string name = moduleLevelName(root->name());
    if (!nodeAt(name).has_value()) {
        return nullptr;
    }
    const node_handle_t variable = resolveNodeByRef(name);
    // The path starts at a `var` built as a value as well: `x.f = e` rebuilds the whole value
    // and rebinds the name (`ssaAssignmentOf`), and the array is copied rather than taken, since
    // there is no cell whose ownership could be kept.
    const SsaEnv::Entry *ssaTarget = isVariable(variable) ? nullptr : ssa_.find(nodeScope_, name);
    // ...and at a `var` a loop head carries as state (loopStateAssignmentOf). The name stands for
    // the round's value and not for a cell of this graph -- the cell is the loop's, and this body
    // cannot reach it -- so the path is rebuilt over the value the round has so far, and the write
    // is the round's next state: the head takes it in `AssignmentForm::Rebuilt`
    // (loopStateAssignmentOf), the same way `ssaAssignmentOf` takes the rebuilt write of a name
    // built as a value. Without this the write had no variable to store into and the program was
    // refused (`ImmutablePassedToVarParam`, `AssignmentTargetNotVariable`).
    const bool carriedState =
        ssaTarget == nullptr && !isVariable(variable) && loopStatePorts_.contains(variable);
    if (!isVariable(variable) && ssaTarget == nullptr && !carriedState) {
        return nullptr;
    }
    Type *rootType = ssaTarget != nullptr ? ssaTarget->type : valueTypeOf(nodeTypeOf(variable));
    if (rootType == nullptr) {
        return nullptr;
    }
    // The types along the path and what each step does.
    std::vector<PathStep> steps(chain.size());
    Type *type = rootType;
    for (size_t i = 0; i < chain.size(); ++i) {
        PathStep &built = steps[i];
        built.node      = chain[i];
        if (chain[i]->type() == GCT::NodeKind::ACCS) {
            if (type->code() == TypeCode::Struct) {
                const auto *structType = tt::as_ptr<StructType>(type);
                const auto index       = structType->findField(chain[i]->accessKey());
                if (!index.has_value()) {
                    return nullptr; // the read reports the field that does not exist
                }
                built.field = *index;
                type        = structType->typeAt(*index);
            } else if (type->code() == TypeCode::Tuple) {
                const auto *tupleType = tt::as_ptr<TupleType>(type);
                if (chain[i]->accessIndex() >= tupleType->size()) {
                    return nullptr; // the read reports the element past the end
                }
                built.field = chain[i]->accessIndex();
                type        = tupleType->typeAt(built.field);
            } else {
                return nullptr;
            }
        } else {
            if (type->code() != TypeCode::Array) {
                return nullptr; // an element of a tuple or struct by a runtime index
            }
            built.isIndex = true;
            type          = tt::as_ptr<ArrayType>(type)->elemType();
        }
    }
    // An array variable is written where it is when the statement is part of a sync block: the
    // outermost element set takes the variable's own array (`:var/take`) and the store keeps it
    // as the variable's, so nothing downstream copies. The read must not take the array away.
    //
    // Whether the value is shared is not asked here, and no count of edges answers it: the copy
    // is decided at run time by the cell's own mark (`:op/assn_owned` sets it, `:var/get` and
    // `:op/assn` clear it, `:var/peek` leaves it). A control edge only orders two nodes and
    // reads no value (design principles 3.3, 3.4), so adding one cannot make this statement
    // copy; see for the measurement that shows both.
    //
    // A state the head carries is no cell of this body (`carriedState`): there is nothing to take
    // the array out of, so the round's value is read as the value it is -- the value the round
    // wrote before this write, or the port. `:var/take` names a cell, and a port is not one.
    const bool inPlace =
        ssaTarget == nullptr && !carriedState && synced_ && rootType->code() == TypeCode::Array;
    node_handle_t rootRead = ssaTarget != nullptr ? ssa_.valueOf(ssaTarget)
                             : carriedState       ? valueOf(variable)
                             : inPlace            ? peekVariable(variable)
                                                  : readVariable(variable);
    const auto make        = [&](GCT::node_ptr_t node, const GCT::node_ptr_t &at) {
        node->setOrigin(at->origin());
        return node;
    };
    steps.front().base = bindTemporary(rootRead);
    for (size_t i = 0; i < steps.size(); ++i) {
        PathStep &step     = steps[i];
        node_handle_t read = nullptr;
        if (step.isIndex) {
            // The index is a value of the program: evaluated once, and named for the set below.
            node_handle_t indexValue = valueOf(visitOperand(step.node->at(2), step.node));
            if (nodeIsKind(indexValue, runtime::GCNodeKind::Dref) ||
                boundWithArgs_.contains(indexValue)) {
                rejectNameWithoutValue(indexValue, "the index", step.node->at(2));
            }
            step.selector        = bindTemporary(indexValue);
            GCT::node_ptr_t call = make(tree_->makeLink(2), step.node);
            *call << make(tree_->makeDRef("__idx__"), step.node->at(0))
                  << make(tree_->makeDRef(step.base), step.node->at(1))
                  << make(tree_->makeDRef(step.selector), step.node->at(2));
            read = visitLinkNode(call);
        } else {
            GCT::node_ptr_t accs = step.node->accessByIndex()
                                       ? make(tree_->makeAccs(step.node->accessIndex()), step.node)
                                       : make(tree_->makeAccs(step.node->accessKey()), step.node);
            *accs << make(tree_->makeDRef(step.base), step.node->at(0));
            read = visitAccsNode(accs);
        }
        step.value = bindTemporary(read);
        if (i + 1 < steps.size()) {
            steps[i + 1].base = step.value;
        }
    }
    // The value written: the right-hand side for `=`, and for a compound assignment the plain
    // operator over the place's value and the right-hand side (which is therefore read once,
    // design principles 6.11). The right-hand side takes the type of the place, as it takes the
    // type of the variable in `v = e`.
    std::string assigned;
    {
        ExpectedValueScope valueScope(expectedValueType_, type);
        node_handle_t right = valueOf(visitOperand(written, gct));
        if (nodeIsKind(right, runtime::GCNodeKind::Dref) || boundWithArgs_.contains(right)) {
            rejectNameWithoutValue(right, "the value", written);
        }
        if (plainGroup.has_value()) {
            const std::string rightName = bindTemporary(right);
            GCT::node_ptr_t plain       = make(tree_->makeLink(2), gct);
            *plain << make(tree_->makeDRef(std::string(*plainGroup)), gct)
                   << make(tree_->makeDRef(steps.back().value), chain.back())
                   << make(tree_->makeDRef(rightName), written);
            right = visitLinkNode(plain);
        }
        // The value enters the place's type as it enters a declaration's and a parameter's
        // (`acc.best = hi` with `best: float?`): one relation, entersImplicitly.
        Type *valueType = valueTypeOf(nodeTypeOf(right));
        if (type != nullptr && valueType != nullptr && !type->equals(valueType) &&
            entersImplicitly(type, valueType)) {
            if (node_handle_t entered = checkValue(right, type, written)) {
                right = entered;
            }
        }
        assigned = bindTemporary(right);
    }
    // The set chain, from the deepest place to the variable.
    GCT::node_ptr_t value = make(tree_->makeDRef(assigned), written);
    for (size_t i = steps.size(); i-- > 0;) {
        const PathStep &step = steps[i];
        const bool outermost = i == 0;
        GCT::node_ptr_t call = make(tree_->makeLink(3), step.node);
        if (step.isIndex) {
            GCT::node_ptr_t applied = nullptr;
            if (outermost && inPlace) {
                applied = make(tree_->makeLink(1), step.node->at(1));
                *applied << make(internalOperatorDref(camel::core::oper::VarTake), step.node->at(1))
                         << tree_->clone(root);
                inPlaceCalls_.insert(call);
            } else {
                applied = make(tree_->makeDRef(step.base), step.node->at(1));
            }
            *call << make(internalOperatorDref(camel::core::oper::SetIndex), step.node->at(0))
                  << applied << make(tree_->makeDRef(step.selector), step.node->at(2)) << value;
        } else {
            GCT::node_ptr_t field = make(
                tree_->makeData(std::make_shared<LongData>(static_cast<int64_t>(step.field))),
                step.node);
            *call << make(internalOperatorDref(camel::core::oper::SetField), step.node->at(0))
                  << make(tree_->makeDRef(step.base), step.node->at(0)) << field << value;
        }
        value = call;
    }
    // The store: the variable keeps the new value, and the array as its own when it was written
    // in place (so that the next statement may write it in place again). The form says which
    // write this is: the builder's own rebuild of the whole variable (`Rebuilt`), or the
    // rebinding to the result of the in-place operator (`Owned`).
    GCT::node_ptr_t rebinding = make(
        tree_->makeAssign(inPlace ? GCT::AssignmentForm::Owned : GCT::AssignmentForm::Rebuilt),
        gct);
    *rebinding << tree_->clone(root) << value;
    return visitAssignNode(rebinding);
}

node_handle_t Builder::ssaAssignmentOf(const GCT::node_ptr_t &gct) {
    // `x = e` where `x` is a `var` built as a value. The name
    // stands for a value, so the write is the value: `e` is built where the store would have put
    // it -- in the type the cell would have held, so a value of another type is refused as it was
    // -- and the name is rebound to it. Nothing is stored, and no cell is read or written, which
    // is the whole of the difference: what the cell did by being overwritten, the binding does by
    // being replaced.
    // The assignment as written, and the one a path rebuilds: that one says so in its form
    // (AssignmentForm::Rebuilt) and is the same write (pathAssignmentOf).
    if (gct->type() != GCT::NodeKind::ASSIGN) {
        return nullptr;
    }
    const GCT::AssignmentForm form = gct->assignmentForm();
    if (form != GCT::AssignmentForm::Plain && form != GCT::AssignmentForm::Rebuilt) {
        return nullptr;
    }
    const GCT::node_ptr_t &target = gct->assignmentTarget();
    if (target == nullptr || target->type() != GCT::NodeKind::DREF) {
        // `x.f = e` and `x[i] = e`: pathAssignmentOf rebuilds the whole value and writes the
        // variable, which comes back here as a plain assignment of the name.
        return nullptr;
    }
    const std::string name       = moduleLevelName(target->name());
    const SsaEnv::Entry *binding = ssa_.find(nodeScope_, name);
    if (binding == nullptr) {
        return nullptr;
    }
    // Design principles 3.5 (198): from the making of a closure to its last use the name may not be
    // assigned. The name is a value here and not a cell, and the rule is about the name, so the
    // check takes the value the binding holds now -- which is the value the closure captured.
    requireCapturedNameWritable(ssa_.valueOf(binding), name, false, gct->origin());
    const GCT::node_ptr_t &written = gct->assignmentValue();
    Type *held                     = binding->type;
    node_handle_t value{};
    {
        ExpectedValueScope writeScope(expectedValueType_, held);
        value = argumentNodeOf(visit(written), written);
    }
    GUARD(value != nullptr, "An assignment always gives a value.");
    Type *have = valueTypeOf(nodeTypeOf(value));
    if (held != nullptr && have != nullptr && !held->equals(have)) {
        node_handle_t entered = checkValue(value, held, written);
        if (entered == nullptr) {
            // What the store refused: a value the variable cannot hold is not a value of the
            // variable, and the assignment is refused where it is written.
            diags_->of(SemanticDiag::NoMatchingFunction)
                .atOrigin(gct->origin())
                .commit(
                    have != nullptr ? have->toString() : std::string("void"),
                    "'=' takes a value of the variable's own type: " + held->toString());
            throw BuildAbortException();
        }
        value = entered;
    }
    // A write that came from `a op= b` is not reported: the compound assignment reads the name it
    // writes, so the report of a dropped value never stood on it (storedValueIsRead answered for
    // the cell), and it does not stand on it here. A write a path rebuilds (`x.f = e`) is skipped
    // for the same reason: that rebuild reads `x`.
    const bool reportable =
        !inCompoundAssignment_ && form == GCT::AssignmentForm::Plain;
    if (value != nullptr && reportable) {
        // The write is reported where the operator stands -- the `=` the program wrote -- which is
        // the place the store's own reference carries (storeOperatorOrigin); the value's own origin
        // is where the value begins.
        const camel::source::origin_id_t at = storeOperatorOrigin(gct);
        ssa_.noteWrite(value, at != camel::source::kInvalidOriginId ? at : gct->origin());
    }
    ssa_.rebind(nodeScope_, name, value, armWritesHere());
    // The write is the round's next state where a loop head carries the name as a value:
    // the head's phi takes it instead of a cell (loopStateAssignmentOf, noteLoopStateWrite).
    noteLoopStateWrite(name, value);
    return value;
}

node_handle_t Builder::loopStateAssignmentOf(const GCT::node_ptr_t &gct) {
    // `x = e` where `x` is a `var` the loop around this body carries as a state (visitLoopNode).
    // The body's `x` is a port of its region, and the value `e` gives is the next state of the
    // `var`: nothing is stored anywhere, because the cell is not a cell the body can reach -- the
    // LOOP hands the value on from one round to the next. This is the third of the three places a
    // write is lowered (design principles 3.5: "a write to an outer `var` in a loop body becomes
    // the loop's carried state"), and it is why the `var` writing and the `let` writing are one
    // graph.
    // The write as written, and the one a path rebuilds: that one says so in its form
    // (AssignmentForm::Rebuilt) and is the same write -- `x.f = e` hands the head the whole value
    // the rebuild made, exactly as `x = e` hands it the value written (pathAssignmentOf builds it
    // over the round's own value, and ssaAssignmentOf takes the rebuilt write of a name built as a
    // value the same way).
    const GCT::AssignmentForm form = gct->assignmentForm();
    if (loopStatePorts_.empty() || gct->type() != GCT::NodeKind::ASSIGN ||
        (form != GCT::AssignmentForm::Plain && form != GCT::AssignmentForm::Rebuilt)) {
        return nullptr;
    }
    const auto &target = gct->assignmentTarget();
    if (target == nullptr || target->type() != GCT::NodeKind::DREF) {
        // A path (`v.f = e`, `v[i] = e`) was rewritten into the whole variable's rebinding by
        // pathAssignmentOf above, or is left to the call as written: either way it is not this.
        return nullptr;
    }
    const auto resolved = nodeAt(moduleLevelName(target->name()));
    if (!resolved.has_value()) {
        return nullptr;
    }
    const node_handle_t port = resolved.value();
    if (!loopStatePorts_.contains(port)) {
        return nullptr; // a `var` of the body itself, or one the loop does not carry
    }
    // The value written stands at the state's own place: what the round goes on with has the type
    // of what it started from, and a value that enters that type implicitly is converted there.
    Type *stateValueType = valueTypeOf(nodeTypeOf(port));
    node_handle_t value{};
    {
        ExpectedValueScope writeScope(expectedValueType_, stateValueType);
        value = valueNodeOf(visit(gct->assignmentValue()));
    }
    if (value == nullptr) {
        return nullptr;
    }
    if (stateValueType != nullptr) {
        Type *written = valueTypeOf(nodeTypeOf(value));
        if (written != nullptr && written != stateValueType &&
            entersImplicitly(stateValueType, written)) {
            if (node_handle_t converted = coerceTo(value, stateValueType, gct->assignmentValue());
                converted != nullptr) {
                value = converted;
            }
        }
    }
    // The round's next state of this `var`, and the value a read written after this write in the
    // same round takes (valueOf). A second write in the round replaces the first: what the round
    // hands on is the last value it wrote, as a cell would hold.
    loopStateWrites_[port] = value;
    return value;
}

void Builder::noteLoopStateWrite(const std::string &name, node_handle_t value) {
    // The same record the cell path makes where the write stands (loopStateAssignmentOf above), for
    // a `var` the loop head carries as a value: the round's next state is the value just
    // written. The write itself can stand anywhere in the body -- a statement of the body's block,
    // an arm of a branch inside it (where the JOIN is the value), or a loop body inside it (where
    // that head's value is) -- and each of those hands the value to this head when it is done, so
    // every path that finishes a write calls this one. A name no head of this build carries is not
    // loop state: the environment holds no port for it and nothing is recorded.
    if (value == nullptr) {
        return;
    }
    if (const node_handle_t port = ssa_.portOf(nodeScope_, name); port != nullptr) {
        loopStateWrites_[port] = value;
    }
}

node_handle_t Builder::visitAssignNode(const GCT::node_ptr_t &gct) {
    // A write (NodeKind::ASSIGN) is not a call. Three of the four lowerings the tier above has
    // for a write are made here, and the fourth -- a store into a cell -- is the call the write
    // makes, built below from the node's two sides:
    //  - a path (`v.f = e`, `v[i] op= e`): the whole variable is rebound (pathAssignmentOf);
    //  - a `var` built as a value (`ssaAssignmentOf`), or one a loop head carries
    //    (`loopStateAssignmentOf`): the name is rebound, and nothing is stored;
    //  - a rebinding to an in-place operator's result (`inPlaceRebindingOf`).
    ENTER("ASSIGN");
    ExpectedValueScope assignScope(expectedValueType_, nullptr);
    const bool unusedStatement = gct == unusedStatement_;
    const UnusedStatementScope unusedScope(unusedStatement_, nullptr);
    if (node_handle_t rebound = pathAssignmentOf(gct)) {
        LEAVE("ASSIGN");
        return rebound;
    }
    // A write to a `var` built as a value: the name is rebound,
    // and nothing is stored. The statement is worth the value the write gave, which is what the
    // read of the name after it takes. A dropped assignment is a dropped value, and it is reported
    // as one (UnusedAssignedValue) rather than as a pure call whose result nobody takes.
    if (node_handle_t written = ssaAssignmentOf(gct)) {
        discardedValues_.insert(written);
        LEAVE("ASSIGN");
        return written;
    }
    // A write to a `var` the loop this body belongs to carries as its own state (visitLoopNode;
    // design principles 3.5): `x = e` in the body is the round's next value of `x`, and there is
    // no cell to store it in -- the cell is the loop's state, and the LOOP hands the value on.
    if (node_handle_t written = loopStateAssignmentOf(gct)) {
        if (unusedStatement) {
            // What this statement is worth is the loop's next state, not a value the position
            // drops: the unused-pure-value rule (rejectUnusedPureResult) is about a value nobody
            // wants, and this one the loop wants.
            discardedValues_.insert(written);
        }
        LEAVE("ASSIGN");
        return written;
    }
    if (unusedStatement) {
        if (node_handle_t rebound = inPlaceRebindingOf(gct)) {
            LEAVE("ASSIGN");
            return rebound;
        }
    }
    // What is left is a store: the cell the target is, and the value written into it. The call is
    // the one the write is spelled as -- a call of the group that writes its first
    // argument -- and its form says which store this is.
    const GCT::AssignmentForm form  = gct->assignmentForm();
    GCT::node_ptr_t store           = tree_->makeLink(2);
    GCT::node_ptr_t storeOperator   = makeStoreOperator(gct);
    // The store is a node of its own, so its two sides are copied into it. The copy of the value
    // takes the in-place mark with it: that mark is the GCT node's identity (inPlaceCalls_,
    // Builder::pathAssignmentOf and Builder::inPlaceRebindingOf), and a copy that lost it was
    // built as the copying form the operator declares next to the in-place one -- `:op/set_index`
    // and `:struct/append_arr` -- so every array the lowering meant to rebind in place was copied.
    GCT::node_ptr_t storeValue = tree_->clone(gct->assignmentValue());
    if (inPlaceCalls_.erase(gct->assignmentValue()) > 0) {
        inPlaceCalls_.insert(storeValue);
    }
    *store << storeOperator << tree_->clone(gct->assignmentTarget()) << storeValue;
    store->setOrigin(gct->origin());
    node_handle_t stored = visitLinkNode(store, form);
    LEAVE("ASSIGN");
    return stored;
}

GCT::node_ptr_t Builder::makeStoreOperator(const GCT::node_ptr_t &gct) {
    // The reference to the operator that writes the cell:
    //  - `=`, and the whole variable's rebuild of a path: the group of one overload per type,
    //    chosen by the arguments' types. It has no single id (an assignment the program may also
    //    write as `=`), so it stays by name -- the one place a name is the reference, and here it
    //    is the store's own name, not the write's. The name itself is `core::kAssignmentGroup`:
    //    the table in `operator_names.cpp` states it once, and this switch asks for it. The rebuild
    //    writes through that same name; a `$builtin::` prefix would only mark a name the compiler
    //    made and cost readers a prefix judgement of their own.
    //  - `op=`: the compound group, whose overload set is what says whether the write is defined
    //    for these types (the node carries it, see AssignmentForm::Compound).
    //  - the rebinding to an in-place operator's result: that operator, by id.
    GCT::node_ptr_t dref = nullptr;
    switch (gct->assignmentForm()) {
    case GCT::AssignmentForm::Compound:
        dref = tree_->makeDRef(gct->assignmentOperator());
        break;
    case GCT::AssignmentForm::Rebuilt:
    case GCT::AssignmentForm::Plain:
        // One group for both: the whole variable's rebuild writes through the same assignment
        // group `=` names, and `core::kAssignmentGroup` is where the name is stated. The bare name
        // resolves to that same group; a `$builtin::` prefix would only mark a name the compiler made.
        dref = tree_->makeDRef(std::string(camel::core::kAssignmentGroup));
        break;
    case GCT::AssignmentForm::Owned:
        dref = internalOperatorDref(camel::core::oper::AssnOwned);
        break;
    }
    dref->setOrigin(storeOperatorOrigin(gct));
    return dref;
}

camel::source::origin_id_t Builder::storeOperatorOrigin(const GCT::node_ptr_t &gct) {
    // The place of the operator, not of the statement: the token the program wrote (`+=`), which
    // is where a fault of the operator is reported and where the compound rewrite reads it from
    // (compoundAssignmentByRebinding sets the plain operator's own origin from the callee's, so
    // that a `%=` by zero is reported at the `%=`). The node's own origin is the statement's.
    if (const auto *bundle = gctSemantic(context_, gct)) {
        for (const auto &part : bundle->parts) {
            if (part.role == camel::source::SemanticRole::Operator &&
                part.origin != camel::source::kInvalidOriginId) {
                return part.origin;
            }
        }
    }
    return gct->origin();
}

} // namespace camel::compile::gir
