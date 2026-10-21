/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
#include "camel/core/type/tree.h"
#include "camel/utils/scope.h"

#include <charconv>
#include <numeric>

#define DEBUG_LEVEL -1

namespace camel::compile::gir {

using namespace camel::core::data;
using namespace camel::core::type;

namespace {

std::string typePatternText(uint16_t patterns) {
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
    add(GCT::TypeCaseKind::Leaf, "#leaf");
    add(GCT::TypeCaseKind::Wildcard, "_");
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
        // a value position stays unresolved, as it should.
        locals.emplace_back();
        if (!node->staticForValueName().empty()) {
            locals.back().insert(node->staticForValueName());
        }
        walkStaticBodyNames(node->staticForBody(), locals);
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

bool Builder::typeCaseMatches(Type *subject, uint16_t patterns) const {
    const bool isStruct = subject->code() == TypeCode::Struct;
    const bool isTuple  = subject->code() == TypeCode::Tuple && tree::isInternal(subject);
    if ((patterns & typeCaseBit(GCT::TypeCaseKind::Wildcard)) != 0) {
        return true;
    }
    if (isStruct && (patterns & typeCaseBit(GCT::TypeCaseKind::Struct)) != 0) {
        return true;
    }
    if (isTuple && (patterns & typeCaseBit(GCT::TypeCaseKind::Tuple)) != 0) {
        return true;
    }
    return (patterns & typeCaseBit(GCT::TypeCaseKind::Leaf)) != 0 && !isStruct && !isTuple;
}

node_handle_t Builder::visitTypeMatchNode(const GCT::node_ptr_t &gct) {
    ENTER("TMTCH");
    ASSERT(gct->size() >= 2, "A match type has a subject type and cases.");
    Type *subject = boundType(gct->at(0)->dataType());
    if (subject == nullptr || subject->code() == TypeCode::TypeVar ||
        containsTypeVar(subject)) {
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
                    .commit(typePatternText(patterns));
                throw BuildAbortException();
            }
        }

        GCT::node_ptr_t selected = nullptr;
        for (size_t i = 0; i < caseCount; ++i) {
            const auto &caseNode = gct->at(i + 1);
            if (typeCaseMatches(subject, caseNode->typeCasePatterns())) {
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
    // of a comprehension (`values`), each round's value is one field of the result.
    Type *subject = boundType(gct->staticForType()->dataType());
    if (subject == nullptr || subject->code() == TypeCode::TypeVar || containsTypeVar(subject)) {
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

    const std::string valueName = gct->staticForValueName();
    const std::string typeName  = gct->staticForTypeName();
    const GCT::node_ptr_t body  = gct->staticForBody();
    const size_t count          = tree::arity(subject);
    node_handle_t lastEffect{};
    if (shape != nullptr) {
        *shape = subject;
    }
    for (size_t i = 0; i < count; ++i) {
        // `F` is looked up from the front of the bindings (findTypeBinding scans from there),
        // and the fields are walked in declaration order.
        typeBindings_.insert(typeBindings_.begin(), {typeName, tree::childType(subject, i)});
        staticValueScopes_.emplace_back();
        staticValueScopes_.back().push_back({valueName, tree::childName(subject, i)});
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
                typeBindings_.erase(typeBindings_.begin());
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
        typeBindings_.erase(typeBindings_.begin());
        if (voidElement) {
            diags_->of(SemanticDiag::StaticComprehensionVoidElement)
                .atOrigin(body->origin())
                .commit(tree::childName(subject, i), subject->toString());
            throw BuildAbortException();
        }
    }
    LEAVE("SFOR");
    return lastEffect;
}

node_handle_t Builder::visitStaticComprehensionNode(const GCT::node_ptr_t &gct) {
    ENTER("SCOMP");
    // The comprehension is its generator plus the pack: the rounds' values are the fields of a
    // struct (or the elements of a tuple) of the same shape. Nothing of it is left in the graph
    // but the straight-line code of the rounds and one FILL.
    std::vector<node_handle_t> values;
    Type *shape = nullptr;
    (void)visitStaticForNode(gct->comprehensionGenerator(), &values, &shape);
    node_handle_t packed = packStaticValues(
        gct->comprehensionIsStruct(), shape, values, gct, "gir.static.comprehension.fill");
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
        type = TupleType::create(types);
    }
    // The same construction as the C++ expansion of `tree_map` (src/compile/gir/tree.cpp): a
    // static value of the shape's size, and a FILL that reads one slot per value.
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

    // The key is a compile-time constant: a literal, or the `n` of an enclosing `static for`.
    std::optional<std::string> key;
    if (keyNode->type() == GCT::NodeKind::DATA) {
        const data_ptr_t data = keyNode->data();
        if (auto text = std::dynamic_pointer_cast<StringData>(data)) {
            key = text->data();
        } else if (data->type() != nullptr && data->type()->code() == TypeCode::Int64) {
            key = std::to_string(std::dynamic_pointer_cast<PrimaryData<int64_t>>(data)->data());
        } else if (data->type() != nullptr && data->type()->code() == TypeCode::Int32) {
            key = std::to_string(std::dynamic_pointer_cast<PrimaryData<int32_t>>(data)->data());
        }
    } else if (keyNode->type() == GCT::NodeKind::DREF && !nodeAt(keyNode->name()).has_value()) {
        key = staticValueNamed(keyNode->name());
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
    // The target is still built when the access is dropped, so that a call in it keeps its
    // effects; only the ACCS node is left out.
    node_handle_t target = valueNodeOf(visit(gct->staticAccessTarget()));
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

    // The tuple key is the `uint32_t` index and the struct key its field name: the same nodes
    // the C++ expansion of `tree_map` builds for the same tree (src/compile/gir/tree.cpp).
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