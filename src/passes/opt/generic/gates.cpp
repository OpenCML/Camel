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
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* Move a forwarding gate's waits to its consumers without moving the value's producer. */
#include "camel/runtime/control.h"
#include "rewrite.h"

#include <algorithm>
#include <vector>

namespace camel::passes::generic {
using namespace camel::runtime;

bool bypassValueGate(GraphDraft &draft, gc_node_ref_t id, size_t &edgeBudget) {
    const auto *gate = draft.header(id);
    if (!gate || gate->kind != GCNodeKind::Gate || !edgeBudget || !isReplaceable(draft, id) ||
        draft.normInputsOf(id).size() != 1 || !draft.withInputsOf(id).empty() ||
        draft.ctrlInputsOf(id).empty()) {
        return false;
    }
    const size_t valueUses = draft.normUsersOf(id).size() + draft.withUsersOf(id).size();
    const size_t allUses   = valueUses + draft.ctrlUsersOf(id).size();
    // Unread gates have their own DCE rule. Bound both local inspection and potential fanout
    // before allocating scratch storage; retaining a shared barrier can be cheaper than
    // materializing the Cartesian product of waits and consumers.
    if (!valueUses || allUses > 64 || draft.ctrlInputsOf(id).size() > 64)
        return false;

    struct User {
        gc_node_ref_t id;
        bool control = false;
    };
    std::vector<User> users;
    const auto addUser = [&](gc_node_ref_t user, bool control) {
        auto it = std::ranges::find(users, user, &User::id);
        if (it == users.end())
            users.push_back({user, control});
        else
            it->control |= control;
    };
    for (auto inputs : {draft.normUsersOf(id), draft.withUsersOf(id)}) {
        for (auto user : inputs) {
            const auto *h = draft.header(user);
            // JOIN's values are alternatives. A CTRL edge to JOIN would wait for a producer
            // in an unselected arm. Keep the gate until a branch-aware rewrite can place
            // that wait inside the right arm. BRCH context values are conservative too.
            if (!h || h->kind == GCNodeKind::Join || h->kind == GCNodeKind::Brch)
                return false;
            addUser(user, false);
        }
    }
    for (auto user : draft.ctrlUsersOf(id))
        addUser(user, true);
    const auto predecessors = controlFrontier(draft, draft.ctrlInputsOf(id));
    const auto controlUsers = std::ranges::count_if(users, [](const auto &u) { return u.control; });
    const size_t cost       = predecessors.size() * users.size() + controlUsers;
    if (cost > 64 || cost > edgeBudget ||
        cost > draft.ctrlInputsOf(id).size() + controlUsers + 1 + 8)
        return false;

    const auto value = draft.normInputsOf(id).front();
    draft.replaceAllValueUses(id, value);
    for (const auto &user : users) {
        if (user.control) {
            draft.unlinkInput(DraftEdgeKind::Ctrl, user.id, id);
            // A control-only user also waited for value production, not just the gate's
            // explicit CTRL inputs. Keep that completion (and any fault in the producer).
            appendControlDependency(draft, value, user.id);
        }
        for (auto predecessor : predecessors) {
            appendControlDependency(draft, predecessor, user.id);
        }
    }
    draft.eraseNode(id);
    edgeBudget -= cost;
    return true;
}
} // namespace camel::passes::generic
