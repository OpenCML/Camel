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
#include "camel/core/type/tree.h"
#include "camel/utils/scope.h"

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

} // namespace camel::compile::gir