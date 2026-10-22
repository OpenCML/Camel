// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `match type` (design principles 5.5, handover H51 step 1): the subject is a type known
 * after instantiation, and only the case its shape selects is built. The selected body is
 * straight-line code in the current graph: no branch node, no region, nothing left of the
 * cases that are not selected.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/core/data/base.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/text.h"
#include "camel/core/type/tree.h"
#include "camel/utils/scope.h"

#include <charconv>
#include <numeric>

#define DEBUG_LEVEL -1

namespace camel::compile::gir {

using namespace camel::core::data;
using namespace camel::core::type;

namespace {

std::string typePatternText(const GCT::node_ptr_t &caseNode) {
    const uint16_t patterns = caseNode->typeCasePatterns();
    std::string text;
    const auto add = [&](GCT::TypeCaseKind kind, const char *name) {
        if ((patterns & GCT::typeCaseBit(kind)) == 0) {
            return;
        }
        if (!text.empty()) {
            text += " | ";
        }
        text += name;
    };
    add(GCT::TypeCaseKind::Struct, "struct");
    add(GCT::TypeCaseKind::Tuple, "tuple");
    add(GCT::TypeCaseKind::Func, "func");
    add(GCT::TypeCaseKind::Leaf, "#leaf");
    add(GCT::TypeCaseKind::Wildcard, "_");
    for (size_t i = 0; i < caseNode->typeCaseTypeCount(); ++i) {
        if (!text.empty()) {
            text += " | ";
        }
        text += caseNode->typeCaseTypeAt(i)->toString();
    }
    return text.empty() ? std::string("(none)") : text;
}

} // namespace

void Builder::resolveStaticBodyNames(const GCT::node_ptr_t &body) {
    std::vector<std::unordered_set<std::string>> locals;
    walkStaticBodyNames(body, locals);
}

void Builder::walkStaticBodyNames(
    const GCT::node_ptr_t &node, std::vector<std::unordered_set<std::string>> &locals) {
    if (!node) {
        return;
    }
    switch (node->type()) {
    case GCT::NodeKind::FUNC:
    case GCT::NodeKind::DECL:
        // A function written in a case that is not selected is read when it is built. Its
        // parameters and body are a scope of their own; names here would not resolve against
        // the scopes around it.
        return;
    case GCT::NodeKind::EXEC: {
        locals.emplace_back();
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals);
        }
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::NREF:
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals);
        }
        if (!locals.empty() && !node->name().empty() &&
            node->name() != GCT::kDiscardName) {
            locals.back().insert(node->name());
        }
        return;
    case GCT::NodeKind::VARI:
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals);
        }
        if (!locals.empty() && !node->name().empty()) {
            locals.back().insert(node->name());
        }
        return;
    case GCT::NodeKind::DREF: {
        bool found = false;
        for (auto it = locals.rbegin(); it != locals.rend() && !found; ++it) {
            found = it->contains(node->name());
        }
        // The rest parameter and the `constas` values of the instance being expanded are
        // compile-time names, not nodes of a scope: a case that is not selected must still see
        // them, or the unselected case of a `match type` would resolve differently from the
        // selected one (H51 step 13 writes `tree_map` this way).
        if (!found && restBindingNamed(node->name()) != nullptr) {
            found = true;
        }
        if (!found && staticValueNamed(node->name()).has_value()) {
            found = true;
        }
        if (!found && !valueNameExists(node->name(), node)) {
            diags_->of(SemanticDiag::UnresolvedReference)
                .atOrigin(node->origin())
                .commit(node->name());
            throw BuildAbortException();
        }
        return;
    }
    case GCT::NodeKind::SFOR: {
        // `n` is bound in the body only. The type name `F` is not a value, so a use of it in
        // a value position stays unresolved, as it should. `_` (the wildcard of `match`) binds
        // nothing, so a use of it stays unresolved too.
        locals.emplace_back();
        const std::string &valueName = node->staticForValueName();
        if (!valueName.empty() && valueName != "_") {
            locals.back().insert(valueName);
        }
        walkStaticBodyNames(node->staticForBody(), locals);
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::SLOOP: {
        // The first values are outside the names of the head; `n` and the state names are
        // bound in the body only (as for a run-time loop).
        for (size_t j = 0; j < node->staticLoopStateCount(); ++j) {
            walkStaticBodyNames(node->staticLoopInitial(j), locals);
        }
        locals.emplace_back();
        const std::string &valueName = node->staticLoopValueName();
        if (valueName != "_") {
            locals.back().insert(valueName);
        }
        for (size_t j = 0; j < node->staticLoopStateCount(); ++j) {
            locals.back().insert(node->staticLoopStateName(j));
        }
        walkStaticBodyNames(node->staticLoopBody(), locals);
        locals.pop_back();
        return;
    }
    default:
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals);
        }
        return;
    }
}

bool Builder::typeCaseMatches(Type *subject, const GCT::node_ptr_t &caseNode) const {
    const uint16_t patterns = caseNode->typeCasePatterns();
    const bool isStruct     = subject->code() == TypeCode::Struct;
    const bool isTuple = subject->code() == TypeCode::Tuple && tree::isInternal(subject);
    const bool isFunc  = subject->code() == TypeCode::Function;
    if ((patterns & typeCaseBit(GCT::TypeCaseKind::Wildcard)) != 0) {
        return true;
    }
    if (isStruct && (patterns & typeCaseBit(GCT::TypeCaseKind::Struct)) != 0) {
        return true;
    }
    if (isTuple && (patterns & typeCaseBit(GCT::TypeCaseKind::Tuple)) != 0) {
        return true;
    }
    if (isFunc && (patterns & typeCaseBit(GCT::TypeCaseKind::Func)) != 0) {
        return true;
    }
    if ((patterns & typeCaseBit(GCT::TypeCaseKind::Leaf)) != 0 && !isStruct && !isTuple) {
        return true;
    }
    // An identity pattern is exact: `case float` does not select `int`, `case f32` does not
    // select `f64`, and a named struct is its declaration, not merely a struct of that shape.
    // The pattern's type variables have the bindings of the instance being built, so `case T`
    // selects exactly the type `T` was bound to. A pattern that is still open is reported by
    // visitTypeMatchNode before any case is selected.
    for (size_t i = 0; i < caseNode->typeCaseTypeCount(); ++i) {
        Type *pattern = boundType(caseNode->typeCaseTypeAt(i));
        if (pattern == nullptr || containsTypeVar(pattern)) {
            continue;
        }
        if (typeEquals(subject, pattern)) {
            return true;
        }
    }
    return false;
}

node_handle_t Builder::visitTypeMatchNode(const GCT::node_ptr_t &gct) {
    ENTER("TMTCH");
    ASSERT(gct->size() >= 2, "A match type has a subject type and cases.");
    Type *subject = resolveTypeNode(gct->at(0)->dataType(), gct->at(0));
    if (subject == nullptr || subject->code() == TypeCode::TypeVar || containsTypeVar(subject)) {
        diags_->of(SemanticDiag::StaticTypeNotConcrete)
            .atOrigin(gct->origin())
            .commit(subject == nullptr ? std::string("unknown") : subject->toString());
        throw BuildAbortException();
    }
    if (staticDepth_ >= kMaxStaticMatchDepth) {
        diags_->of(SemanticDiag::StaticMatchTooDeep)
            .atOrigin(gct->origin())
            .commit(std::to_string(staticDepth_));
        throw BuildAbortException();
    }
    ++staticDepth_;

    const auto restoreDepth = [&] { --staticDepth_; };

    try {
        const size_t caseCount = gct->size() - 1;
        for (size_t i = 0; i < caseCount; ++i) {
            const auto &caseNode = gct->at(i + 1);
            ASSERT(caseNode->type() == GCT::NodeKind::TCASE, "A case of match type is a TCASE.");
            const uint16_t patterns = caseNode->typeCasePatterns();
            const bool catchesAll =
                (patterns & (typeCaseBit(GCT::TypeCaseKind::Leaf) |
                             typeCaseBit(GCT::TypeCaseKind::Wildcard))) != 0;
            if (catchesAll && i + 1 < caseCount) {
                diags_->of(SemanticDiag::StaticMatchUnreachableCase)
                    .atOrigin(caseNode->origin())
                    .commit(typePatternText(caseNode));
                throw BuildAbortException();
            }
            // An identity pattern must name a type by the time the case is selected: a type
            // variable of the pattern that the instance does not bind cannot match anything.
            std::vector<Type *> earlier;
            uint16_t earlierShapes = 0;
            for (size_t j = 0; j < i; ++j) {
                const auto &other = gct->at(j + 1);
                for (size_t k = 0; k < other->typeCaseTypeCount(); ++k) {
                    earlier.push_back(other->typeCaseTypeAt(k));
                }
                earlierShapes |= other->typeCasePatterns();
                if ((other->typeCasePatterns() & typeCaseBit(GCT::TypeCaseKind::Wildcard)) != 0) {
                    earlier.clear(); // every type is selected before this case
                }
            }
            for (size_t k = 0; k < caseNode->typeCaseTypeCount(); ++k) {
                Type *written = caseNode->typeCaseTypeAt(k);
                Type *pattern = boundType(written);
                if (pattern == nullptr) {
                    continue;
                }
                if (containsTypeVar(pattern)) {
                    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                        .atOrigin(caseNode->origin())
                        .commit(
                            pattern->toString(),
                            "a type variable of the pattern is not bound here");
                    throw BuildAbortException();
                }
                // A shape an earlier case selects covers whole families: `case struct` before
                // `case Point` makes the latter dead, exactly as `case #leaf` before it does.
                const bool isStruct = pattern->code() == TypeCode::Struct;
                const bool isTuple =
                    pattern->code() == TypeCode::Tuple && tree::isInternal(pattern);
                const bool isFunc = pattern->code() == TypeCode::Function;
                const bool coveredByShape =
                    (isStruct &&
                     (earlierShapes & typeCaseBit(GCT::TypeCaseKind::Struct)) != 0) ||
                    (isTuple && (earlierShapes & typeCaseBit(GCT::TypeCaseKind::Tuple)) != 0) ||
                    (isFunc && (earlierShapes & typeCaseBit(GCT::TypeCaseKind::Func)) != 0);
                if (coveredByShape) {
                    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                        .atOrigin(caseNode->origin())
                        .commit(
                            pattern->toString(),
                            "an earlier case selects every type of this shape");
                    throw BuildAbortException();
                }
                for (Type *before : earlier) {
                    Type *bound = boundType(before);
                    if (bound == nullptr || containsTypeVar(bound)) {
                        continue;
                    }
                    if (typeEquals(pattern, bound)) {
                        // The type is selected by an earlier case, which is written before
                        // this one: no subject can reach this pattern.
                        diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                            .atOrigin(caseNode->origin())
                            .commit(
                                pattern->toString(), "an earlier case already selects this type");
                        throw BuildAbortException();
                    }
                }
            }
        }

        GCT::node_ptr_t selected = nullptr;
        for (size_t i = 0; i < caseCount; ++i) {
            const auto &caseNode = gct->at(i + 1);
            if (typeCaseMatches(subject, caseNode)) {
                selected = caseNode;
                break;
            }
        }
        if (selected == nullptr) {
            diags_->of(SemanticDiag::StaticMatchNotExhaustive)
                .atOrigin(gct->origin())
                .commit(subject->toString());
            throw BuildAbortException();
        }

        // A case that is not selected is not built and its types are not checked, but its
        // names are still resolved (Q10): a misspelling must not wait for the instantiation
        // that happens to select it.
        for (size_t i = 0; i < caseCount; ++i) {
            const auto &caseNode = gct->at(i + 1);
            if (caseNode != selected) {
                resolveStaticBodyNames(caseNode->at(0));
            }
        }

        // A `return` in the selected body gives the value of the match, not of the function
        // around it. The body is otherwise built as any block, in the current graph.
        StaticBodyScope scope;
        StaticBodyScope *outer = std::exchange(activeStaticBody_, &scope);
        node_handle_t result{};
        try {
            result = visitBodyNode(selected->at(0));
        } catch (...) {
            activeStaticBody_ = outer;
            throw;
        }
        activeStaticBody_ = outer;
        if (scope.returned) {
            result = scope.value;
        }
        if (result == nullptr) {
            // An empty case body has no value: it stands for `void`.
            result = currGraph_->addStaticDataNode(Data::null());
        }
        restoreDepth();
        LEAVE("TMTCH");
        return result;
    } catch (...) {
        restoreDepth();
        throw;
    }
}

namespace {

/// Inside the rounds of a `static for` a `t.[n]` may reuse the access an earlier round made
/// (staticAccessCache_). Outside one the cache is not consulted.
struct StaticExpansionScope {
    size_t &depth;
    explicit StaticExpansionScope(size_t &d) : depth(d) { ++depth; }
    ~StaticExpansionScope() { --depth; }
};

} // namespace

node_handle_t Builder::visitStaticForNode(
    const GCT::node_ptr_t &gct, std::vector<node_handle_t> *values, Type **shape) {
    ENTER("SFOR");
    // A `static for` written as a statement drops every round's value; written as the generator
    // of a comprehension (`values`), each round's value is one field of the result. Two
    // generators share the node: `fields T` walks a type's fields, `indices ts` (H51 step 6)
    // walks the positions of a tuple value.
    const bool byIndices = gct->staticForIndices();
    Type *subject        = nullptr;
    if (byIndices) {
        // `indices e` reads only the type of `e`. A build-time tuple gives it without a FILL
        // (H51 step 6): `ts`, or a tuple comprehension whose elements stay separate nodes.
        const std::any operandRes = visit(gct->staticForOperand());
        if (operandRes.type() == typeid(StaticTuple)) {
            subject = boundType(any_cast<const StaticTuple &>(operandRes).type);
        } else {
            node_handle_t operand = valueNodeOf(operandRes);
            subject = operand != nullptr ? boundType(valueTypeOf(nodeTypeOf(operand))) : nullptr;
        }
        if (subject == nullptr || subject->code() == TypeCode::TypeVar ||
            containsTypeVar(subject)) {
            diags_->of(SemanticDiag::StaticTypeNotConcrete)
                .atOrigin(gct->origin())
                .commit(subject == nullptr ? std::string("unknown") : subject->toString());
            throw BuildAbortException();
        }
        if (subject->code() != TypeCode::Tuple || !tree::isInternal(subject)) {
            const std::string valueText = sourceTextOf(gct->staticForOperand());
            diags_->of(SemanticDiag::StaticIndicesNotTuple)
                .atOrigin(gct->origin())
                .commit(valueText.empty() ? std::string("ts") : valueText, subject->toString());
            throw BuildAbortException();
        }
    } else {
        subject = resolveTypeNode(gct->staticForOperand()->dataType(), gct->staticForOperand());
        if (subject == nullptr || subject->code() == TypeCode::TypeVar ||
            containsTypeVar(subject)) {
            diags_->of(SemanticDiag::StaticTypeNotConcrete)
                .atOrigin(gct->origin())
                .commit(subject == nullptr ? std::string("unknown") : subject->toString());
            throw BuildAbortException();
        }
        if (!tree::isInternal(subject)) {
            diags_->of(SemanticDiag::StaticForNotComposite)
                .atOrigin(gct->origin())
                .commit(subject->toString());
            throw BuildAbortException();
        }
    }

    const std::string valueName = gct->staticForValueName();
    const std::string typeName  = gct->staticForTypeName();
    const GCT::node_ptr_t body  = gct->staticForBody();
    const size_t count          = tree::arity(subject);
    node_handle_t lastEffect{};
    if (shape != nullptr) {
        *shape = subject;
    }
    for (size_t i = 0; i < count; ++i) {
        const std::string entryName = tree::childName(subject, i);
        // `F` is looked up from the front of the bindings (findTypeBinding scans from there),
        // and the fields are walked in declaration order. `indices` binds no `F`: the round's
        // position is an ordinary `int` value instead.
        node_handle_t indexNode{};
        if (!byIndices) {
            typeBindings_.insert(typeBindings_.begin(), {typeName, tree::childType(subject, i)});
        } else {
            const data_ptr_t indexValue = tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(i)));
            indexNode = currGraph_->addStaticDataNode(indexValue);
            rememberConstant(indexNode, indexValue);
            registerNodeOrigin(context_, indexNode, body, "gir.static.index");
        }
        staticValueScopes_.emplace_back();
        if (valueName != "_") {
            // `_` is a place that binds nothing (the wildcard of `match`): a use of it must not
            // find a value. Every other name of the head becomes a compile-time value here.
            staticValueScopes_.back().push_back({valueName, entryName, indexNode});
        }
        enterBlockScope();
        const GCT::Node *outerUnused = std::exchange(unusedStatement_, body);
        const GCT::Node *outerSyncedBlock =
            std::exchange(syncedBlockOverride_, synced_ ? body : nullptr);
        if (values != nullptr) {
            // The value of each round is the field's value: the body's last statement is not a
            // dropped statement, so it keeps its value (and is not rejected as unused).
            unusedStatement_ = nullptr;
        }
        const node_handle_t before = lastSyncedNode_;
        node_handle_t value{};
        bool voidElement = false;
        {
            StaticExpansionScope expansion(staticExpansionDepth_);
            try {
                value = visitExecNode(body);
            } catch (...) {
                syncedBlockOverride_ = outerSyncedBlock;
                unusedStatement_     = outerUnused;
                leaveBlockScope();
                staticValueScopes_.pop_back();
                if (!byIndices) {
                    typeBindings_.erase(typeBindings_.begin());
                }
                throw;
            }
        }
        syncedBlockOverride_ = outerSyncedBlock;
        unusedStatement_     = outerUnused;
        if (values != nullptr) {
            if (value == nullptr || valueTypeOf(nodeTypeOf(value))->code() == TypeCode::Void) {
                // A field with no value is no element: the result would be one field short.
                voidElement = true;
            } else {
                values->push_back(value);
            }
        } else if (synced_ && lastSyncedNode_ != nullptr && lastSyncedNode_ != before) {
            // In a sync block the body's statements are part of the chain around the `static for`.
            // Hand the last effect back to the enclosing block, so that the chain is reachable from
            // the block's value even though the `static for` itself has none.
            lastEffect = lastSyncedNode_;
        }
        leaveBlockScope();
        staticValueScopes_.pop_back();
        if (!byIndices) {
            typeBindings_.erase(typeBindings_.begin());
        }
        if (voidElement) {
            diags_->of(SemanticDiag::StaticComprehensionVoidElement)
                .atOrigin(body->origin())
                .commit(entryName, subject->toString());
            throw BuildAbortException();
        }
    }
    LEAVE("SFOR");
    return lastEffect;
}

node_handle_t Builder::visitStaticLoopNode(const GCT::node_ptr_t &gct) {
    ENTER("SLOOP");
    // `static loop s1 = a1, ..., sk = ak for (n, F) in fields T do body`: the fold the fields
    // of `T` unroll into (or the positions of a tuple value, `in indices ts`). The body is built
    // once per round in the current graph; its value is the next state. No LOOP node, no
    // region, and nothing of the state or the generator left in the graph: the first values are
    // ordinary values and each round reads the previous round's node directly (a DREF to a
    // state name resolves to that node).
    const bool byIndices = gct->staticLoopIndices();
    Type *subject        = nullptr;
    if (byIndices) {
        // As in visitStaticForNode: the operand only gives the tuple's type.
        const std::any operandRes = visit(gct->staticLoopOperand());
        if (operandRes.type() == typeid(StaticTuple)) {
            subject = boundType(any_cast<const StaticTuple &>(operandRes).type);
        } else {
            node_handle_t operand = valueNodeOf(operandRes);
            subject = operand != nullptr ? boundType(valueTypeOf(nodeTypeOf(operand))) : nullptr;
        }
        if (subject == nullptr || subject->code() == TypeCode::TypeVar ||
            containsTypeVar(subject)) {
            diags_->of(SemanticDiag::StaticTypeNotConcrete)
                .atOrigin(gct->origin())
                .commit(subject == nullptr ? std::string("unknown") : subject->toString());
            throw BuildAbortException();
        }
        if (subject->code() != TypeCode::Tuple || !tree::isInternal(subject)) {
            const std::string valueText = sourceTextOf(gct->staticLoopOperand());
            diags_->of(SemanticDiag::StaticIndicesNotTuple)
                .atOrigin(gct->origin())
                .commit(valueText.empty() ? std::string("ts") : valueText, subject->toString());
            throw BuildAbortException();
        }
    } else {
        subject = resolveTypeNode(gct->staticLoopOperand()->dataType(), gct->staticLoopOperand());
        if (subject == nullptr || subject->code() == TypeCode::TypeVar ||
            containsTypeVar(subject)) {
            diags_->of(SemanticDiag::StaticTypeNotConcrete)
                .atOrigin(gct->origin())
                .commit(subject == nullptr ? std::string("unknown") : subject->toString());
            throw BuildAbortException();
        }
        if (!tree::isInternal(subject)) {
            diags_->of(SemanticDiag::StaticForNotComposite)
                .atOrigin(gct->origin())
                .commit(subject->toString());
            throw BuildAbortException();
        }
    }

    const size_t k              = gct->staticLoopStateCount();
    const std::string valueName = gct->staticLoopValueName();
    const std::string typeName  = gct->staticLoopTypeName();
    const GCT::node_ptr_t body  = gct->staticLoopBody();

    // The names of the head are declared once, together (as for a run-time loop). The walked
    // entry's name `n` (`j`) is one of them for this purpose: `static loop n = ..., ... for
    // (n, F)` would read the state where it meant the field.
    {
        std::unordered_set<std::string> names;
        if (valueName != "_") {
            names.insert(valueName);
        }
        for (size_t j = 0; j < k; ++j) {
            if (!names.insert(gct->staticLoopStateName(j)).second) {
                diags_->of(SemanticDiag::LoopNameRepeated)
                    .atOrigin(gct->origin())
                    .commit(gct->staticLoopStateName(j));
                throw BuildAbortException();
            }
        }
    }

    // The first values are evaluated once, where the loop is, before the state names exist.
    std::vector<node_handle_t> current;
    std::vector<Type *> stateTypes;
    current.reserve(k);
    stateTypes.reserve(k);
    for (size_t j = 0; j < k; ++j) {
        const auto &initial = gct->staticLoopInitial(j);
        node_handle_t node  = valueOf(functionValueOf(valueNodeOf(visit(initial)), initial));
        Type *type          = valueTypeOf(nodeTypeOf(node));
        if (type == nullptr || type->code() == TypeCode::Void) {
            diags_->of(SemanticDiag::LoopStateWithoutType)
                .atOrigin(initial->origin())
                .commit(gct->staticLoopStateName(j), type ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
        current.push_back(node);
        stateTypes.push_back(type);
    }
    Type *stateType = k == 1 ? stateTypes.front() : TupleType::create(stateTypes);

    const size_t count = tree::arity(subject);
    for (size_t i = 0; i < count; ++i) {
        const std::string entryName = tree::childName(subject, i);
        node_handle_t indexNode{};
        if (!byIndices) {
            typeBindings_.insert(typeBindings_.begin(), {typeName, tree::childType(subject, i)});
        } else {
            const data_ptr_t indexValue = tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(i)));
            indexNode = currGraph_->addStaticDataNode(indexValue);
            rememberConstant(indexNode, indexValue);
            registerNodeOrigin(context_, indexNode, body, "gir.static.index");
        }
        staticValueScopes_.emplace_back();
        if (valueName != "_") {
            staticValueScopes_.back().push_back({valueName, entryName, indexNode});
        }
        enterBlockScope();
        for (size_t j = 0; j < k; ++j) {
            // The state names read the previous round's value; each round has its own block
            // scope, so the same name is bound afresh (not a redeclaration).
            (void)insertNode(gct->staticLoopStateName(j), current[j]);
        }
        const GCT::Node *outerUnused = std::exchange(unusedStatement_, nullptr);
        const GCT::Node *outerSyncedBlock =
            std::exchange(syncedBlockOverride_, synced_ ? body : nullptr);
        node_handle_t value{};
        {
            StaticExpansionScope expansion(staticExpansionDepth_);
            try {
                value = visitExecNode(body);
            } catch (...) {
                syncedBlockOverride_ = outerSyncedBlock;
                unusedStatement_     = outerUnused;
                leaveBlockScope();
                staticValueScopes_.pop_back();
                if (!byIndices) {
                    typeBindings_.erase(typeBindings_.begin());
                }
                throw;
            }
        }
        syncedBlockOverride_ = outerSyncedBlock;
        unusedStatement_     = outerUnused;

        if (k == 1) {
            if (value != nullptr) {
                Type *bodyType = valueTypeOf(nodeTypeOf(value));
                if (stateTypes.front()->assignableFrom(bodyType)) {
                    current.front() = value;
                } else if (bodyType != nullptr && entersImplicitly(stateTypes.front(), bodyType)) {
                    node_handle_t coerced = coerceTo(value, stateTypes.front(), body);
                    current.front()          = coerced != nullptr ? coerced : value;
                } else {
                    diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                        .atOrigin(body->origin())
                        .commit(
                            bodyType ? bodyType->toString() : std::string("void"),
                            stateType->toString());
                    leaveBlockScope();
                    staticValueScopes_.pop_back();
                    if (!byIndices) {
                        typeBindings_.erase(typeBindings_.begin());
                    }
                    throw BuildAbortException();
                }
            }
            // A body that ends with `return` (or `throw`) leaves the function; the rounds after
            // it are unreachable and keep the state as it was.
        } else if (value != nullptr) {
            Type *bodyType = valueTypeOf(nodeTypeOf(value));
            TupleType *tuple = bodyType != nullptr && tree::isInternal(bodyType) &&
                                       bodyType->code() == TypeCode::Tuple
                                   ? tt::as_ptr<TupleType>(bodyType)
                                   : nullptr;
            bool fits = tuple != nullptr && tuple->size() == k;
            for (size_t j = 0; fits && j < k; ++j) {
                Type *componentType = tuple->typeAt(j);
                fits = stateTypes[j]->assignableFrom(componentType) ||
                       entersImplicitly(stateTypes[j], componentType);
            }
            if (!fits) {
                diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                    .atOrigin(body->origin())
                    .commit(
                        bodyType ? bodyType->toString() : std::string("void"),
                        stateType->toString());
                leaveBlockScope();
                staticValueScopes_.pop_back();
                if (!byIndices) {
                    typeBindings_.erase(typeBindings_.begin());
                }
                throw BuildAbortException();
            }
            // One state per component of the body's tuple, in the order written. A state
            // that takes a value implicitly (an `int` into an `int?`) keeps the state's
            // type, so the next round reads the state's type and the same rule applies.
            for (size_t j = 0; j < k; ++j) {
                Type *componentType = tuple->typeAt(j);
                node_handle_t component =
                    currGraph_->addAccsNode(componentType, static_cast<uint32_t>(j));
                linkNodes(LinkType::Norm, valueOf(value), component);
                registerNodeOrigin(context_, component, body, "gir.static.loop.state");
                node_handle_t stateValue = stateTypes[j]->assignableFrom(componentType)
                                               ? component
                                               : coerceTo(component, stateTypes[j], body);
                current[j] = stateValue != nullptr ? stateValue : component;
            }
        }
        leaveBlockScope();
        staticValueScopes_.pop_back();
        if (!byIndices) {
            typeBindings_.erase(typeBindings_.begin());
        }
    }
    LEAVE("SLOOP");
    if (k == 1) {
        return current.front();
    }
    // Several states are a tuple, as a run-time loop's several states are (design principles
    // 3.10). The pack is the one a comprehension makes; the loop itself has no node.
    return packStaticValues(false, nullptr, current, gct, "gir.static.loop.states");
}

std::any Builder::visitStaticComprehensionNode(const GCT::node_ptr_t &gct) {
    ENTER("SCOMP");
    // The comprehension is its generator plus the pack: the rounds' values are the fields of a
    // struct (or the elements of a tuple) of the same shape. Nothing of it is left in the graph
    // but the straight-line code of the rounds and one FILL.
    //
    // `T { ... }` names the shape: the field names and the kind come from `T`, which must be the
    // type the generator walks.
    const bool sameShape = gct->comprehensionSameShape();
    Type *declaredShape  = nullptr;
    if (sameShape) {
        const GCT::node_ptr_t shapeNode = gct->comprehensionShapeType();
        declaredShape                   = resolveTypeNode(shapeNode->dataType(), shapeNode);
        if (declaredShape == nullptr || declaredShape->code() == TypeCode::TypeVar ||
            containsTypeVar(declaredShape)) {
            diags_->of(SemanticDiag::StaticTypeNotConcrete)
                .atOrigin(shapeNode->origin())
                .commit(
                    declaredShape == nullptr ? std::string("unknown") : declaredShape->toString());
            throw BuildAbortException();
        }
        if (!tree::isInternal(declaredShape)) {
            diags_->of(SemanticDiag::SameShapeComprehensionNotComposite)
                .atOrigin(shapeNode->origin())
                .commit(declaredShape->toString());
            throw BuildAbortException();
        }
    }
    std::vector<node_handle_t> values;
    Type *shape = nullptr;
    const GCT::node_ptr_t generator = gct->comprehensionGenerator();
    if (!sameShape && gct->comprehensionIsStruct() && generator->staticForIndices()) {
        // A struct comprehension names its fields from the walked type; positions have no
        // names, so there is nothing to name the fields.
        diags_->of(SemanticDiag::ConstructNotSupported)
            .atOrigin(generator->origin())
            .commit(
                "{ static for ... in indices ... }",
                "a struct comprehension takes its field names from 'fields T'; write a "
                "tuple comprehension '( static for ... in indices ... )' or walk 'fields T'");
        throw BuildAbortException();
    }
    (void)visitStaticForNode(generator, &values, &shape);
    if (sameShape) {
        if (shape == nullptr || shape != declaredShape) {
            diags_->of(SemanticDiag::SameShapeComprehensionMismatch)
                .atOrigin(generator->origin())
                .commit(
                    declaredShape->toString(),
                    shape == nullptr ? std::string("an unknown type") : shape->toString());
            throw BuildAbortException();
        }
    }
    if (!sameShape && !gct->comprehensionIsStruct()) {
        // A tuple comprehension is a build-time tuple: its rounds are separate element nodes,
        // and the FILL is built only if the value is used at runtime (valueNodeOf).
        type_vec_t types;
        types.reserve(values.size());
        for (const node_handle_t &value : values) {
            types.push_back(valueTypeOf(nodeTypeOf(value)));
        }
        StaticTuple tuple;
        tuple.type           = TupleType::create(std::move(types));
        tuple.elements       = std::move(values);
        tuple.materializable = true;
        tuple.origin         = gct;
        LEAVE("SCOMP");
        return tuple;
    }
    if (sameShape && declaredShape->code() != TypeCode::Struct) {
        // A tuple shape fills the elements by position; the field names it does not have are
        // the positions the rounds already give.
        type_vec_t types;
        types.reserve(values.size());
        for (const node_handle_t &value : values) {
            types.push_back(valueTypeOf(nodeTypeOf(value)));
        }
        StaticTuple tuple;
        tuple.type           = TupleType::create(std::move(types));
        tuple.elements       = std::move(values);
        tuple.materializable = true;
        tuple.origin         = gct;
        LEAVE("SCOMP");
        return tuple;
    }
    if (sameShape) {
        node_handle_t packed =
            packStaticValues(true, declaredShape, values, gct, "gir.static.shape.fill");
        LEAVE("SCOMP");
        return packed;
    }
    node_handle_t packed =
        packStaticValues(true, shape, values, gct, "gir.static.comprehension.fill");
    LEAVE("SCOMP");
    return packed;
}

node_handle_t Builder::packStaticValues(
    bool isStruct, Type *shape, const std::vector<node_handle_t> &values,
    const GCT::node_ptr_t &origin, const char *originKind) {
    type_vec_t types;
    types.reserve(values.size());
    for (const node_handle_t &value : values) {
        types.push_back(valueTypeOf(nodeTypeOf(value)));
    }
    Type *type = nullptr;
    if (isStruct && shape != nullptr) {
        StructTypeFactory fields;
        for (size_t i = 0; i < values.size(); ++i) {
            fields.add(std::string(tree::childName(shape, i)), types[i]);
        }
        type = fields.build();
    } else {
        type = TupleType::create(std::move(types));
    }
    return fillStaticValues(type, values, origin, originKind);
}

node_handle_t Builder::materializeStaticTuple(
    const StaticTuple &tuple, const GCT::node_ptr_t &at) {
    return fillStaticValues(
        tuple.type,
        tuple.elements,
        at != nullptr ? at : tuple.origin,
        "gir.static.tuple.fill");
}

node_handle_t Builder::fillStaticValues(
    Type *type, const std::vector<node_handle_t> &values, const GCT::node_ptr_t &origin,
    const char *originKind) {
    // A static value of the shape's size, and a FILL that reads one slot per value: the same
    // construction `tree_map` now reaches through a comprehension over the fields.
    core::mm::NoCollectionScope guard;
    const bool tuple = type->code() == TypeCode::Tuple;
    const slot_t empty =
        tuple
            ? core::rtdata::toSlot(::Tuple::create(values.size(), core::mm::autoSpace()))
            : core::rtdata::toSlot(::Struct::create(values.size(), core::mm::autoSpace()));
    auto &draft = currGraph_->draft();
    auto base   = draft.ref(draft.materializeStaticValue(empty, type));
    std::vector<size_t> slots(values.size());
    std::iota(slots.begin(), slots.end(), 0);
    node_handle_t out = currGraph_->addFillNode(
        type,
        runtime::makeFillPayload(
            tuple ? runtime::GCFillKind::Tuple : runtime::GCFillKind::Struct, slots));
    linkNodes(LinkType::Norm, base, out);
    for (const node_handle_t &value : values) {
        linkNodes(LinkType::With, valueOf(value), out);
    }
    registerNodeOrigin(context_, out, origin, originKind);
    return out;
}

node_handle_t Builder::visitStaticAccessNode(const GCT::node_ptr_t &gct) {
    ENTER("SACC");
    const GCT::node_ptr_t keyNode = gct->staticAccessKey();

    // The key is a compile-time constant: a literal, the `n` of an enclosing `static for`, a
    // name bound once to a constant (`let first = ( static for ... ).[0]`), or an expression
    // whose value is known at build time (`( static for (n, F) in fields S => n ).[0]` in the
    // library is such an expression: a comprehension over the field names, indexed at 0).
    const auto keyOfData = [](const data_ptr_t &data) -> std::optional<std::string> {
        if (auto text = std::dynamic_pointer_cast<StringData>(data)) {
            return text->data();
        }
        if (data->type() != nullptr && data->type()->code() == TypeCode::Int64) {
            return std::to_string(std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)->data());
        }
        if (data->type() != nullptr && data->type()->code() == TypeCode::Int32) {
            return std::to_string(std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)->data());
        }
        return std::nullopt;
    };
    std::optional<std::string> key;
    if (keyNode->type() == GCT::NodeKind::DATA) {
        key = keyOfData(keyNode->data());
    } else if (keyNode->type() == GCT::NodeKind::DREF) {
        const auto bound = nodeAt(keyNode->name());
        if (!bound.has_value()) {
            key = staticValueNamed(keyNode->name());
        } else if (!isVariable(*bound)) {
            // A `let` bound to a value the compiler knows is a constant too: the binding is
            // immutable, and `first_leaf` in the library names the first field once this way.
            // A `var` is not: it can be assigned another key before the access.
            if (data_ptr_t data = constantOfNode(*bound)) {
                key = keyOfData(data);
            }
        }
    }
    if (!key.has_value() && keyNode->type() != GCT::NodeKind::DATA &&
        keyNode->type() != GCT::NodeKind::DREF) {
        // A key written as an expression: build it where the access is and take its constant.
        // A value that is not constant is still refused below.
        const std::any keyRes = visit(keyNode);
        if (node_handle_t keyValue = valueNodeOf(keyRes)) {
            if (data_ptr_t data = constantOfNode(keyValue)) {
                key = keyOfData(data);
            }
        }
    }
    if (!key.has_value()) {
        diags_->of(SemanticDiag::StaticAccessNotConstant)
            .atOrigin(keyNode->origin())
            .commit(
                keyNode->type() == GCT::NodeKind::DREF ? keyNode->name()
                                                       : std::string("expression"));
        throw BuildAbortException();
    }

    const bool dropped = gct == unusedStatement_;
    // A build-time tuple has no container node to project from: `ts.[j]` is the element node
    // itself, chosen here (H51 step 6). No FILL and no ACCS are built.
    const std::any targetRes = visit(gct->staticAccessTarget());
    if (targetRes.type() == typeid(StaticTuple)) {
        const StaticTuple &tuple = any_cast<const StaticTuple &>(targetRes);
        uint32_t index           = 0;
        const char *begin        = key->data();
        const char *end          = begin + key->size();
        const auto parsed        = std::from_chars(begin, end, index);
        if (parsed.ec != std::errc() || parsed.ptr != end || index >= tuple.elements.size()) {
            diags_->of(SemanticDiag::StaticAccessIndexOutOfRange)
                .atOrigin(gct->origin())
                .commit(*key, tuple.type->toString(), std::to_string(tuple.elements.size()));
            throw BuildAbortException();
        }
        if (dropped) {
            LEAVE("SACC");
            return nullptr;
        }
        LEAVE("SACC");
        return tuple.elements[index];
    }
    // The target is still built when the access is dropped, so that a call in it keeps its
    // effects; only the ACCS node is left out.
    node_handle_t target = valueNodeOf(targetRes);
    if (target == nullptr) {
        diags_->of(SemanticDiag::StaticAccessNotInternal)
            .atOrigin(gct->origin())
            .commit(std::string("void"));
        throw BuildAbortException();
    }
    Type *subject = nodeTypeOf(target);

    Type *fieldType     = nullptr;
    bool byIndex        = false;
    uint32_t index      = 0;
    std::string fieldId = *key;
    if (subject->code() == TypeCode::Struct) {
        auto *structType    = tt::as_ptr<StructType>(subject);
        const auto found    = structType->findField(*key);
        if (!found.has_value()) {
            diags_->of(SemanticDiag::StaticAccessUnknownField)
                .atOrigin(gct->origin())
                .commit(*key, subject->toString());
            throw BuildAbortException();
        }
        fieldType = structType->typeAt(found.value());
        fieldId   = std::string(structType->fieldName(found.value()));
    } else if (subject->code() == TypeCode::Tuple && tree::isInternal(subject)) {
        auto *tupleType = tt::as_ptr<TupleType>(subject);
        const char *begin = key->data();
        const char *end   = begin + key->size();
        const auto parsed = std::from_chars(begin, end, index);
        if (parsed.ec != std::errc() || parsed.ptr != end || index >= tupleType->size()) {
            diags_->of(SemanticDiag::StaticAccessIndexOutOfRange)
                .atOrigin(gct->origin())
                .commit(*key, subject->toString(), std::to_string(tupleType->size()));
            throw BuildAbortException();
        }
        fieldType = tupleType->typeAt(index);
        byIndex   = true;
    } else {
        diags_->of(SemanticDiag::StaticAccessNotInternal)
            .atOrigin(gct->origin())
            .commit(subject->toString());
        throw BuildAbortException();
    }

    if (dropped) {
        LEAVE("SACC");
        return nullptr;
    }

    // `t.[n].[m]` inside a `static for`: every inner round reads the same parent, which is the
    // same node, so the access is made once and reused. The field was validated above; a target
    // that makes a fresh node per visit (a `var` read, a call) never matches an earlier round.
    const std::string cacheKey = byIndex ? ("#" + std::to_string(index)) : ("." + fieldId);
    if (staticExpansionDepth_ > 0) {
        const auto found = staticAccessCache_.find({target, cacheKey});
        if (found != staticAccessCache_.end()) {
            LEAVE("SACC");
            return found->second;
        }
    }

    // The tuple key is the `uint32_t` index and the struct key its field name: the nodes a
    // comprehension over `fields T` builds for the same tree.
    node_handle_t accsNode =
        byIndex ? currGraph_->addAccsNode(fieldType, index)
                : currGraph_->addAccsNode(fieldType, fieldId);
    linkNodes(LinkType::Norm, target, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs.static");
    if (staticExpansionDepth_ > 0) {
        staticAccessCache_[{target, cacheKey}] = accsNode;
    }
    LEAVE("SACC");
    return accsNode;
}

} // namespace camel::compile::gir