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
#include "camel/core/data/composite/array.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
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

/// The text a round's value is known by when the body uses it as the key of a `t.[…]`: the
/// digits of an integer or the characters of a string, the two renderings the static access
/// reads a key back with. Anything else (a float element, say) is named by its own text, which
/// is not a key any structure has: the round is still a value the body can compute with.
std::string sequenceElementName(const data_ptr_t &data) {
    if (data == nullptr) {
        return "?";
    }
    if (auto text = std::dynamic_pointer_cast<StringData>(data)) {
        return text->data();
    }
    if (data->type() != nullptr) {
        if (data->type()->code() == TypeCode::Int64) {
            return std::to_string(std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)->data());
        }
        if (data->type()->code() == TypeCode::Int32) {
            return std::to_string(std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)->data());
        }
    }
    return data->toString();
}

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

void Builder::insertParameterNames(
    const GCT::node_ptr_t &func, const FunctionType *signature,
    std::unordered_set<std::string> &into) {
    if (signature != nullptr) {
        for (size_t i = 0; i < signature->argNamesCount(); ++i) {
            const std::string_view arg = signature->argNameAt(i);
            if (!arg.empty() && arg != GCT::kDiscardName) {
                into.insert(std::string(arg));
            }
        }
    }
    if (func == nullptr) {
        return;
    }
    // A parameter written as a tuple pattern is one value whose elements the body reads by name.
    if (const auto *tuples = GCT::Tree::of(func)->tupleParamsOf(func)) {
        for (const GCT::Tree::TupleParamBinding &tupleParam : *tuples) {
            for (const std::string &name : tupleParam.names) {
                if (!name.empty() && name != GCT::kDiscardName) {
                    into.insert(name);
                }
            }
        }
    }
}

void Builder::resolveGenericTemplateNames(const generic_template_ptr_t &tmpl) {
    if (tmpl == nullptr || tmpl->func == nullptr || tmpl->func->size() < 2) {
        return;
    }
    std::vector<std::unordered_set<std::string>> locals;
    // Every parameter of the declaration is a name its body may read: the `with` parameters and
    // the `constas` values among them, and the rest parameter's name. What their types are is not
    // read here; only the names are.
    locals.emplace_back();
    insertParameterNames(tmpl->func, tmpl->funcType, locals.back());
    walkStaticBodyNames(tmpl->func->at(1), locals, true);
    // A `when` clause is not a body: it belongs to the condition evaluator, which already answers
    // the declaration-site half of it for a function that is not generic, where it is written
    // (visitFuncNode). The same half is answered here, so that it does not wait for an
    // instantiation either: a name the declaration does not make visible, a predicate of a module
    // that is not imported (and the module that declares it is named), two imports of one name, a
    // name that is not a compile-time predicate, a condition that is not a bool, a condition that
    // is not a compile-time expression.
    //
    // Whether the condition *holds* stays the instantiation's question, and so does a query on a
    // type parameter: at the declaration the parameter is open, and only the instance knows what
    // it stands for (`WhenTypeQueryNotConcrete`, cases/compile/when/open_type.cml).
    if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
        const WhenResult early =
            evaluateWhenCondition(when->condition, tmpl->funcType, {}, {}, tmpl);
        switch (early.outcome) {
        case WhenOutcome::UnknownName:
        case WhenOutcome::NotImported:
        case WhenOutcome::NotCompileTime:
        case WhenOutcome::PredicateAmbiguous:
        case WhenOutcome::NotBool:
        case WhenOutcome::NotEvaluable:
            reportWhenProblem(early, tmpl->func->name(), when->condition);
            break;
        default:
            break;
        }
    }
}

bool Builder::moduleSourceDeclares(const std::string &written) {
    ModuleUnit *unit = currentModuleUnit();
    if (unit == nullptr || unit->tree == nullptr) {
        return false;
    }
    if (!unit->sourceNamesRead) {
        // Read once: every name any node of the module's source carries.
        unit->sourceNamesRead = true;
        const GCT::node_ptr_t root = unit->tree->root();
        std::vector<GCT::node_ptr_t> pending{root};
        while (!pending.empty()) {
            const GCT::node_ptr_t node = pending.back();
            pending.pop_back();
            if (node == nullptr) {
                continue;
            }
            switch (node->type()) {
            // Only the kinds whose `name()` is a name (gct/node.h): a VARI binds the NREFs that
            // are its children, and those are collected on their own.
            case GCT::NodeKind::FUNC:
            case GCT::NodeKind::DECL:
            case GCT::NodeKind::NREF:
                if (!node->name().empty()) {
                    unit->sourceNames.insert(node->name());
                }
                break;
            default:
                break;
            }
            for (size_t i = 0; i < node->size(); ++i) {
                pending.push_back(node->at(i));
            }
        }
    }
    return unit->sourceNames.contains(written) ||
           unit->sourceNames.contains(moduleLevelName(written));
}

void Builder::walkStaticBodyNames(
    const GCT::node_ptr_t &node, std::vector<std::unordered_set<std::string>> &locals,
    bool intoFunctions) {
    if (!node) {
        return;
    }
    switch (node->type()) {
    case GCT::NodeKind::FUNC: {
        if (!intoFunctions) {
            // A function written in a case that is not selected is read when it is built. Its
            // parameters and body are a scope of their own; names here would not resolve against
            // the scopes around it.
            return;
        }
        // The declaration-site check of a generic template reads into the functions written in
        // its body (a lambda, a function of the body): their own parameters bind and the names
        // around them stay visible, exactly as they are when the body is built.
        locals.emplace_back();
        if (node->size() > 0 &&
            node->at(0)->type() == GCT::NodeKind::TYPE && node->at(0)->dataType() != nullptr &&
            node->at(0)->dataType()->code() == TypeCode::Function) {
            insertParameterNames(
                node, tt::as_ptr<FunctionType>(node->at(0)->dataType()), locals.back());
        }
        if (node->size() > 1) {
            walkStaticBodyNames(node->at(1), locals, true);
        }
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::DECL:
        // A declaration read ahead of its use: the body is elsewhere, and the signature's types
        // are read where the declaration is.
        return;
    case GCT::NodeKind::EXEC: {
        locals.emplace_back();
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
        }
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::NREF:
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
        }
        if (!locals.empty() && !node->name().empty() &&
            node->name() != GCT::kDiscardName) {
            locals.back().insert(node->name());
        }
        return;
    case GCT::NodeKind::VARI:
        // A VARI node carries no name of its own: the names it binds are the NREFs that are its
        // children (`var x = 1`, program.cpp collectBoundNames), and the NREF case above binds
        // each of them. `name()` is not a VARI's to read (gct/node.h).
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
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
        // The declaration-site check of a generic template is the one caller that reads a body
        // no instance has built, so a name a declaration of the module gives later in the source
        // is not a misspelling there: the module is one scope and the top level is built by
        // dependency, not in written order. Every other caller (an unselected case of
        // `match type`) keeps the stricter reading it was written with.
        if (!found && intoFunctions && moduleSourceDeclares(node->name())) {
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
        walkStaticBodyNames(node->staticForBody(), locals, intoFunctions);
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::SLOOP: {
        // The first values are outside the names of the head; `n` and the state names are
        // bound in the body only (as for a run-time loop).
        for (size_t j = 0; j < node->staticLoopStateCount(); ++j) {
            walkStaticBodyNames(node->staticLoopInitial(j), locals, intoFunctions);
        }
        locals.emplace_back();
        const std::string &valueName = node->staticLoopValueName();
        if (valueName != "_") {
            locals.back().insert(valueName);
        }
        for (size_t j = 0; j < node->staticLoopStateCount(); ++j) {
            locals.back().insert(node->staticLoopStateName(j));
        }
        walkStaticBodyNames(node->staticLoopBody(), locals, intoFunctions);
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::CASE: {
        // The pattern binds its names in the arm's body only: `case v`, `case error e`,
        // `case Shape::Rect { w, h }` (design principles 5.3). A `case` written as a value
        // (`case 1`) binds nothing, and the value it compares is read outside the bindings.
        const GCT::CaseType pattern = node->caseType();
        const size_t bodyAt =
            pattern == GCT::CaseType::Value && node->size() > 1 ? size_t(1) : size_t(0);
        for (size_t i = 0; i < bodyAt; ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
        }
        locals.emplace_back();
        if (pattern == GCT::CaseType::Bind || pattern == GCT::CaseType::Error) {
            const std::string &bound = node->binding();
            if (!bound.empty() && bound != GCT::kDiscardName) {
                locals.back().insert(bound);
            }
        } else if (pattern == GCT::CaseType::Enum) {
            const GCT::Node::EnumCase &variant = node->enumCase();
            for (size_t i = 0; i < variant.count; ++i) {
                const std::string *bound = variant.bindings[i];
                if (bound != nullptr && !bound->empty() && *bound != GCT::kDiscardName) {
                    locals.back().insert(*bound);
                }
            }
        }
        for (size_t i = bodyAt; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
        }
        locals.pop_back();
        return;
    }
    case GCT::NodeKind::LOOP: {
        // `loop s1 = a1, ... (for x in xs)? (while c)? do b`: the first values and the sequence
        // are read where the loop is, the condition sees the state's names, and the body sees
        // the state's names and the element (design principles 3.10).
        const size_t states = node->loopStateCount();
        for (size_t j = 0; j < states; ++j) {
            walkStaticBodyNames(node->loopInitial(j), locals, intoFunctions);
        }
        if (const GCT::node_ptr_t sequence = node->loopSequence()) {
            walkStaticBodyNames(sequence, locals, intoFunctions);
        }
        locals.emplace_back();
        for (size_t j = 0; j < states; ++j) {
            const std::string &state = node->loopStateName(j);
            if (!state.empty() && state != GCT::kDiscardName) {
                locals.back().insert(state);
            }
        }
        if (camel::core::loopHasIterable(node->loopKind())) {
            const std::string &element = node->loopElementName();
            if (!element.empty() && element != GCT::kDiscardName) {
                locals.back().insert(element);
            }
        }
        if (const GCT::node_ptr_t condition = node->loopCondition()) {
            walkStaticBodyNames(condition, locals, intoFunctions);
        }
        walkStaticBodyNames(node->loopBody(), locals, intoFunctions);
        locals.pop_back();
        return;
    }
    default:
        for (size_t i = 0; i < node->size(); ++i) {
            walkStaticBodyNames(node->at(i), locals, intoFunctions);
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

std::optional<Builder::StaticSequence>
Builder::staticLoopSequenceOf(const GCT::node_ptr_t &subject) {
    // `for i in <序列>` (H462, and the ruling of 2026-10-25 on which `in` forms count, H475):
    // one rule, not a list of accepted spellings. The expression after `in` is a sequence the
    // compiler works out when it is built only from compile-time constants joined by operations
    // that are pure and cannot fail (design principles 3.4); then it is evaluated and unrolled.
    // Nothing here builds a node of the operand: a run-time value leaves the rounds open, and
    // the caller refuses it (`StaticLoopSequenceNotKnown`).
    if (subject == nullptr) {
        return std::nullopt;
    }
    // A sequence the compiler holds as one value: an array literal (`[1, 2, 3]`), or a name
    // bound to one (`let xs = [1, 2, 3]`; a `var` does not count, it can be assigned another
    // array before the loop). The rounds are its elements, so walking it costs no node.
    const auto ofArrayData = [](const data_ptr_t &data) -> std::optional<StaticSequence> {
        if (data == nullptr || data->type() == nullptr ||
            data->type()->code() != TypeCode::Array) {
            return std::nullopt;
        }
        auto array = std::dynamic_pointer_cast<ArrayData>(data);
        if (array == nullptr || !array->resolved()) {
            return std::nullopt;
        }
        StaticSequence sequence;
        sequence.elementType = tt::as_ptr<ArrayType>(data->type())->elemType();
        // Each round is an ordinary value of the array's element type, exactly as the array
        // would give it: an element written with another of the types that enter it (an `int`
        // literal in an `f32[]`) takes the element type here, as it would in the array itself.
        for (const data_ptr_t &element : array->raw()) {
            data_ptr_t value = element;
            if (value != nullptr && sequence.elementType != nullptr &&
                !sequence.elementType->equals(value->type())) {
                if (data_ptr_t converted = value->convertTo(sequence.elementType)) {
                    value = converted;
                }
            }
            sequence.values.push_back(value);
        }
        return sequence;
    };
    const auto ofConstantArray = [&](const GCT::node_ptr_t &value) -> std::optional<StaticSequence> {
        if (value == nullptr) {
            return std::nullopt;
        }
        if (value->type() == GCT::NodeKind::DATA) {
            return ofArrayData(value->data());
        }
        if (value->type() == GCT::NodeKind::DREF) {
            const auto bound = nodeAt(value->name());
            if (bound.has_value()) {
                if (isVariable(*bound)) {
                    return std::nullopt;
                }
                return ofArrayData(constantOfNode(*bound));
            }
            return ofArrayData(constantOfNode(staticValueNodeNamed(value->name())));
        }
        return std::nullopt;
    };
    if (std::optional<StaticSequence> sequence = ofConstantArray(subject)) {
        return sequence;
    }
    // The integers of a sequence the compiler can count out: `int` ends and a step. Reading one
    // end reuses what the compiler already knows, without building anything of a run-time
    // expression: a literal (or an expression of literals only, `0..(2 + 3)`) is read from the
    // tree, a name is read from what it is bound to -- and only a binding that is not a `var`,
    // which may be assigned before the loop, counts -- and anything else is put to the
    // build-time evaluation the rest of the compiler folds with.
    const auto intOf = [](const data_ptr_t &data) -> std::optional<int64_t> {
        if (data == nullptr) {
            return std::nullopt;
        }
        if (auto number = std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)) {
            return number->data();
        }
        if (auto number = std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)) {
            return static_cast<int64_t>(number->data());
        }
        return std::nullopt;
    };
    const auto intBoundOf = [&](const GCT::node_ptr_t &expr) -> std::optional<int64_t> {
        if (expr == nullptr) {
            return std::nullopt;
        }
        const std::optional<literal::Value> written = literalValueOfExpr(expr);
        if (written.has_value()) {
            return written->asInt64();
        }
        if (expr->type() != GCT::NodeKind::DREF) {
            return intOf(constantValueOrNull(expr));
        }
        data_ptr_t data = nullptr;
        const auto bound = nodeAt(expr->name());
        if (bound.has_value()) {
            if (isVariable(*bound)) {
                return std::nullopt;
            }
            data = constantOfNode(*bound);
        } else {
            data = constantOfNode(staticValueNodeNamed(expr->name()));
        }
        return intOf(data);
    };
    // The rounds `from, from + step, ...` up to but not including `to`, counted exactly as the
    // `range` that `a..b` lowers to counts them (`__range__`): below `to` for a positive step,
    // above it for a negative one. Nothing of the sequence is built; the rounds are values.
    const auto walkIntegers = [](int64_t from, int64_t to, int64_t step) {
        StaticSequence sequence;
        sequence.elementType = Type::Int();
        for (int64_t value = from; step > 0 ? value < to : value > to; value += step) {
            sequence.values.push_back(
                tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(value)));
        }
        return sequence;
    };
    // The constant interval `a..b`: read exactly as the `..` operator reads it everywhere else
    // (the half-open `[a, b)`, so `0..5` gives 0, 1, 2, 3, 4), and the pair of two bounds only
    // in the sequence position of a loop (`isIntervalExpr`); in any other position `a..b` is the
    // `int[]` of `intervalValue`. An interval is two `int` ends and nothing else: `..` builds an
    // `int[]`, so there is no float or character interval to walk.
    if (isIntervalExpr(subject)) {
        const std::optional<int64_t> from = intBoundOf(subject->at(1));
        const std::optional<int64_t> to   = intBoundOf(subject->at(2));
        if (!from.has_value() || !to.has_value()) {
            return std::nullopt;
        }
        return walkIntegers(*from, *to, 1);
    }
    // A compile-time tuple: `for x in ts` walks its positions and binds the type at each one
    // (not the element's value -- the ordinary `loop` is the one that binds a value). `ts` is
    // the rest parameter of the expansion being built; `indices ts` walks the same positions
    // and binds them as integers, which is what 6.8.4/6.8.5 of the language reference describe.
    if (subject->type() == GCT::NodeKind::DREF) {
        if (const RestBinding *rest = restBindingNamed(subject->name())) {
            Type *tupleType = boundType(rest->tuple.type);
            if (tupleType != nullptr && tupleType->code() == TypeCode::Tuple &&
                tree::isInternal(tupleType)) {
                StaticSequence sequence;
                for (size_t i = 0; i < tree::arity(tupleType); ++i) {
                    sequence.types.push_back(tree::childType(tupleType, i));
                }
                return sequence;
            }
        }
    }
    // `range(a, b, step)` with constant arguments: the same integers the interval gives, on the
    // step the program wrote -- the same path as `a..b`, read by the same reader and counted by
    // the same walk, because `range` is what the interval lowers to (`intervalValue`) and the
    // array it builds elsewhere is what these rounds are the elements of. The step has to be
    // written -- a step of one is `a..b`, one thing one spelling -- so the two-argument call is
    // refused here as it is where the call stands (`RangeTwoArgumentsRemoved`, which the call
    // path reports after overload resolution), and a step of zero, which names no sequence at
    // all, is `RangeStepZero` here as it is there (design principles 4.2; the rulings of
    // 2026-10-24 on `h248-generator-and-composition` and on `h426-interval-step-form`).
    if (subject->type() == GCT::NodeKind::LINK && subject->size() >= 1) {
        const GCT::node_ptr_t &callee = subject->at(0);
        const bool namedRange = callee != nullptr && callee->type() == GCT::NodeKind::DREF &&
                                callee->name() == "range";
        if (namedRange && subject->size() - 1 == 2) {
            const auto where = callee->origin() != 0 ? callee->origin() : subject->origin();
            diags_->of(SemanticDiag::RangeTwoArgumentsRemoved).atOrigin(where).commit();
            throw BuildAbortException();
        }
        if (namedRange && subject->size() - 1 == 3) {
            const GCT::node_ptr_t &stepNode = subject->at(subject->size() - 1);
            const std::optional<int64_t> step = intBoundOf(stepNode);
            if (step.has_value() && *step == 0) {
                const auto where =
                    stepNode->origin() != 0 ? stepNode->origin() : subject->origin();
                diags_->of(SemanticDiag::RangeStepZero)
                    .atOrigin(where)
                    .commit(sourceTextOf(subject->at(1)), sourceTextOf(subject->at(2)));
                throw BuildAbortException();
            }
            const std::optional<int64_t> start = intBoundOf(subject->at(1));
            const std::optional<int64_t> end   = intBoundOf(subject->at(2));
            if (start.has_value() && end.has_value() && step.has_value()) {
                return walkIntegers(*start, *end, *step);
            }
        }
    }
    // The one rule's general case: an expression the build-time evaluation already holds as an
    // array of constants (`compileTimeExpression`, the folding every other constant expression of
    // the program goes through). A name is not offered to it here: what a `let` or a `var` is
    // bound to was read above, and a `var` may hold another array by the time the loop runs. A
    // refusal is a sequence the compiler does not know, and the caller says so by name.
    if (subject->type() != GCT::NodeKind::DREF) {
        if (std::optional<StaticSequence> sequence =
                ofArrayData(constantValueOrNull(subject))) {
            return sequence;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<int64_t>>
Builder::staticLoopIntSequenceOf(const GCT::node_ptr_t &subject) {
    const std::optional<StaticSequence> sequence = staticLoopSequenceOf(subject);
    if (!sequence.has_value() || sequence->byTypes()) {
        return std::nullopt;
    }
    std::vector<int64_t> values;
    values.reserve(sequence->values.size());
    for (const data_ptr_t &value : sequence->values) {
        if (value == nullptr || value->type() == nullptr) {
            return std::nullopt;
        }
        if (auto number = std::dynamic_pointer_cast<PrimaryData<int64_t>>(value)) {
            values.push_back(number->data());
            continue;
        }
        if (auto number = std::dynamic_pointer_cast<PrimaryData<int32_t>>(value)) {
            values.push_back(static_cast<int64_t>(number->data()));
            continue;
        }
        return std::nullopt;
    }
    return values;
}

node_handle_t Builder::visitStaticLoopNode(const GCT::node_ptr_t &gct) {
    ENTER("SLOOP");
    // `static loop s1 = a1, ..., sk = ak for i in <序列> do body`: the fold the fields of `T`
    // unroll into (`in fields T`), the positions of a tuple value (`in indices ts`), or a
    // sequence the compiler works out (`in 0..5`, `in range(0, 9, 3)`, `in [1, 2, 3]`, `in
    // xs`; H462, the ruling of 2026-10-24, which dropped the head's parentheses, and the ruling
    // of 2026-10-25 that draws the line by the one rule of `staticLoopSequenceOf`). The body is
    // built once per round in the current graph; its value is the next state. No LOOP node, no
    // region, and nothing of the state or the generator left in the graph: the first values are
    // ordinary values and each round reads the previous round's node directly (a DREF to a
    // state name resolves to it).
    const bool byIndices  = gct->staticLoopIndices();
    const bool bySequence = gct->staticLoopSequence();
    const bool byFields   = !byIndices && !bySequence;
    Type *subject         = nullptr;
    // The rounds when the head writes a sequence out: a value per round (a constant interval, a
    // `range`, an array literal or a constant array) or a type per round (a compile-time tuple,
    // where each round binds the type at its position). Each round is a compile-time constant
    // either way, exactly as the `j` of `indices` is the round's position.
    std::optional<StaticSequence> written;
    if (bySequence) {
        // The sequence must be known when the function is compiled (the ruling of 2026-10-24:
        // "`static loop` 的序列必须在编译期已知"): a sequence the compiler cannot work out is
        // refused here, before anything of the body is built.
        written = staticLoopSequenceOf(gct->staticLoopOperand());
        if (!written.has_value()) {
            const std::string shown = sourceTextOf(gct->staticLoopOperand());
            diags_->of(SemanticDiag::StaticLoopSequenceNotKnown)
                .atOrigin(gct->origin())
                .commit(shown.empty() ? std::string("?") : shown);
            throw BuildAbortException();
        }
    } else if (byIndices) {
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
    // Whether the round's name is bound to a type rather than to a value: `F` for `fields T`
    // (which binds the field's name as a value too), and the single name of `for x in ts`, where
    // the round binds the type at that position and no value at all.
    const bool bindsRoundType =
        byFields || (written.has_value() && written->byTypes());
    const bool bindsRoundValue = !(bySequence && written.has_value() && written->byTypes());

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

    const size_t count = bySequence ? written->rounds() : tree::arity(subject);
    for (size_t i = 0; i < count; ++i) {
        // The name of the walked entry: for `fields` the field's name and for `indices` the
        // position, both as the text the compile-time value bindings carry; for a sequence of
        // values the element itself, which is what `t.[i]` reads back as the key; for a
        // compile-time tuple the type at that position, which is not a value at all.
        std::string entryName;
        node_handle_t indexNode{};
        if (bySequence && written->byTypes()) {
            if (valueName != "_") {
                typeBindings_.insert(typeBindings_.begin(), {valueName, written->types[i]});
            }
        } else if (bySequence) {
            const data_ptr_t elementValue = written->values[i];
            entryName = sequenceElementName(elementValue);
            indexNode = currGraph_->addStaticDataNode(elementValue);
            rememberConstant(indexNode, elementValue);
            registerNodeOrigin(context_, indexNode, body, "gir.static.index");
        } else if (byIndices) {
            entryName = tree::childName(subject, i);
            const data_ptr_t indexValue = tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(i)));
            indexNode = currGraph_->addStaticDataNode(indexValue);
            rememberConstant(indexNode, indexValue);
            registerNodeOrigin(context_, indexNode, body, "gir.static.index");
        } else {
            entryName = tree::childName(subject, i);
            typeBindings_.insert(typeBindings_.begin(), {typeName, tree::childType(subject, i)});
        }
        staticValueScopes_.emplace_back();
        if (valueName != "_" && bindsRoundValue) {
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
                if (bindsRoundType) {
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
                    if (bindsRoundType) {
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
                if (bindsRoundType) {
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
        if (bindsRoundType) {
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
        // A call of a pure function with constant arguments is a constant; a place that needs
        // one refuses by the call's name when it does not reduce to one.
        const FoldedCall folded = foldConstantCall(keyNode);
        if (folded.data != nullptr) {
            key = keyOfData(folded.data);
        } else if (folded.refusal != FoldRefusal::NotAProgramCall) {
            reportFoldRefusal(folded, keyNode);
        } else {
            // A key written as an expression: build it where the access is and take its
            // constant. A value that is not constant is still refused below.
            const std::any keyRes = visit(keyNode);
            if (node_handle_t keyValue = valueNodeOf(keyRes)) {
                if (data_ptr_t data = constantOfNode(keyValue)) {
                    key = keyOfData(data);
                }
            }
        }
    }
    if (!key.has_value()) {
        diags_->of(SemanticDiag::StaticAccessNotConstant)
            .atOrigin(keyNode->origin())
            .commit(
                keyNode->type() == GCT::NodeKind::LINK
                    ? writtenCallName(keyNode)
                    : keyNode->type() == GCT::NodeKind::DREF ? keyNode->name()
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