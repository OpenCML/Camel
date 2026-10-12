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
 * Created: Oct. 04, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * In-place writes on a graph draft (see draft_writes.h).
 */

#include "camel/runtime/draft_writes.h"

#include "camel/core/mm/alloc/growable.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/composite/func.h"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

namespace type = camel::core::type;

namespace {

struct OperQueryKey {
    std::string uri;
    size_t withCount = 0;
    type::type_vec_t types; // with types, then norm types

    bool operator==(const OperQueryKey &other) const = default;
};

struct OperQueryKeyHash {
    size_t operator()(const OperQueryKey &key) const {
        size_t seed = std::hash<std::string>{}(key.uri) ^ (key.withCount * 0x9e3779b97f4a7c15ULL);
        for (const type::Type *t : key.types) {
            seed ^=
                std::hash<const void *>{}(t) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

std::mutex operQueryMutex;
std::unordered_map<OperQueryKey, type::FunctionType *, OperQueryKeyHash> &operQueryMemo() {
    static auto *memo =
        new std::unordered_map<OperQueryKey, type::FunctionType *, OperQueryKeyHash>();
    return *memo;
}

/// The function type an OPER's overload has for the types its arguments have now, or nullptr
/// when the overload is unknown or rejects them.
type::FunctionType *operFuncType(const GraphDraft &draft, gc_node_ref_t id, std::string_view uri) {
    type::type_vec_t with, norm;
    for (auto [inputs, types] :
         {std::pair{draft.withInputsOf(id), &with}, std::pair{draft.normInputsOf(id), &norm}}) {
        for (gc_node_ref_t in : inputs) {
            const DraftNodeHeader *h = draft.header(in);
            if (!h || !h->dataType) {
                return nullptr;
            }
            types->push_back(h->dataType);
        }
    }
    return operatorOverloadType(uri, with, norm);
}

bool isVarParam(const type::FunctionType *funcType, bool isWith, size_t index) {
    if (!funcType) {
        return false;
    }
    return isWith ? index < funcType->withTypesCount() && funcType->withIsVarAt(index)
                  : index < funcType->normTypesCount() && funcType->normIsVarAt(index);
}

} // namespace

type::FunctionType *operatorOverloadType(
    std::string_view uri, const type::type_vec_t &with, const type::type_vec_t &norm) {
    OperQueryKey key{std::string(uri), with.size(), with};
    key.types.insert(key.types.end(), norm.begin(), norm.end());
    {
        std::lock_guard lock(operQueryMutex);
        if (const auto it = operQueryMemo().find(key); it != operQueryMemo().end()) {
            return it->second;
        }
    }
    type::FunctionType *answer = nullptr;
    if (const auto resolver = camel::core::OperatorResolverRegistry::instance().find(uri)) {
        try {
            const std::vector<type::StaticArg> statics(norm.size());
            const auto resolved = resolver->resolveWith(with, norm, statics, Modifier::None);
            answer              = resolved ? *resolved : nullptr;
        } catch (const camel::core::mm::MemoryExhausted &) {
            throw; // not an answer about the overload
        } catch (const std::exception &) {
            answer = nullptr;
        }
    }
    std::lock_guard lock(operQueryMutex);
    return operQueryMemo().emplace(std::move(key), answer).first->second;
}

bool isVarArgument(const GraphDraft &draft, gc_node_ref_t user, bool isWith, size_t index) {
    const DraftNodeHeader *h = draft.header(user);
    if (!h) {
        return false;
    }
    switch (h->kind) {
    case GCNodeKind::Oper: {
        const std::string_view uri =
            reinterpret_cast<const GCOperBody *>(draft.payloadOf(user).data())->uri();
        if (camel::core::OperatorTraitsRegistry::instance().isPure(uri)) {
            return false;
        }
        const type::FunctionType *funcType = operFuncType(draft, user, uri);
        return funcType ? isVarParam(funcType, isWith, index) : true;
    }
    case GCNodeKind::Func: {
        const GCGraph *callee =
            reinterpret_cast<const GCFuncBody *>(draft.payloadOf(user).data())->calleeGraph;
        return callee && isVarParam(callee->funcType(), isWith, index);
    }
    case GCNodeKind::Call: {
        // The first with input is the callee; the others are its with arguments.
        const auto withInputs = draft.withInputsOf(user);
        if (withInputs.empty() || (isWith && index == 0)) {
            return false;
        }
        const DraftNodeHeader *callee = draft.header(withInputs.front());
        if (!callee || !callee->dataType || callee->dataType->code() != type::TypeCode::Function) {
            return false;
        }
        return isVarParam(
            static_cast<const type::FunctionType *>(callee->dataType),
            isWith,
            isWith ? index - 1 : index);
    }
    default:
        return false;
    }
}

bool isWrittenInPlace(const GraphDraft &draft, gc_node_ref_t id) {
    // The slot `id` reads: a GATE forwards the slot of its value input.
    for (size_t hops = 0; hops < draft.nodeSlotCount(); ++hops) {
        const DraftNodeHeader *h = draft.header(id);
        if (!h || h->kind != GCNodeKind::Gate || draft.normInputsOf(id).empty()) {
            break;
        }
        id = draft.normInputsOf(id).back();
    }
    // Every user of that slot, through the GATEs that forward it. The graph is acyclic, so the
    // walk ends without a visited set (a GATE reached twice is only looked at twice).
    std::vector<gc_node_ref_t> work{id};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (!draft.header(n)) {
            continue;
        }
        for (const bool isWith : {false, true}) {
            for (gc_node_ref_t user : isWith ? draft.withUsersOf(n) : draft.normUsersOf(n)) {
                const DraftNodeHeader *h = draft.header(user);
                if (!h) {
                    continue;
                }
                if (h->kind == GCNodeKind::Gate) {
                    work.push_back(user);
                    continue;
                }
                const auto inputs = isWith ? draft.withInputsOf(user) : draft.normInputsOf(user);
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (inputs[i] == n && isVarArgument(draft, user, isWith, i)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace camel::runtime
