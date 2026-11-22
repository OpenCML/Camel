// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The matcher: patterns, the left side against a node, and the node a right side builds.

#include "camel/runtime/writings.h"
#include "writings_internal.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

using namespace detail;

// ---------------------------------------------------------------- rules

Pattern patVar(uint32_t var, camel::core::type::Type *type) {
    Pattern p;
    p.kind = Pattern::Kind::Var;
    p.var  = var;
    p.type = type;
    return p;
}

Pattern patConst(uint32_t var, camel::core::type::Type *type) {
    Pattern p   = patVar(var, type);
    p.constOnly = true;
    return p;
}

Pattern patLiteral(slot_t value, camel::core::type::Type *type) {
    Pattern p;
    p.kind  = Pattern::Kind::Constant;
    p.value = canonicalConstant(value, type);
    p.type  = type;
    return p;
}

Pattern patOp(camel::core::OperId oper, std::vector<Pattern> args, camel::core::type::Type *type) {
    Pattern p;
    p.kind = Pattern::Kind::Op;
    p.oper = oper;
    p.type = type;
    p.args = std::move(args);
    return p;
}

Pattern patCompute(
    camel::core::type::Type *type, std::function<slot_t(std::span<const slot_t>)> compute,
    std::vector<Pattern> args) {
    Pattern p;
    p.kind    = Pattern::Kind::Compute;
    p.type    = type;
    p.compute = std::move(compute);
    p.args    = std::move(args);
    return p;
}

std::optional<std::pair<slot_t, camel::core::type::Type *>> Match::constant(uint32_t var) const {
    return writings.constantOf(vars[var]);
}

namespace {

void tracePatternConstants(
    const Pattern &pattern, const camel::core::rtdata::Object::RefRelocator &relocate) {
    if (pattern.kind == Pattern::Kind::Constant && pattern.type && pattern.type->isGCTraced() &&
        pattern.value != NullSlot) {
        auto *ref = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(pattern.value);
        if (ref) {
            ref = relocate(
                ref,
                pattern.type,
                camel::core::rtdata::RefTraceInfo{
                    .owner     = nullptr,
                    .ownerType = nullptr,
                    .slotType  = pattern.type,
                    .ownerKind = "RulePattern",
                    .slotName  = {},
                    .slotIndex = 0,
                });
            pattern.value = camel::core::rtdata::toSlot(ref);
        }
    }
    for (const Pattern &arg : pattern.args) {
        tracePatternConstants(arg, relocate);
    }
}

} // namespace

void traceRuleConstants(
    std::span<const Rule> rules, const camel::core::rtdata::Object::RefRelocator &relocate) {
    for (const Rule &rule : rules) {
        tracePatternConstants(rule.lhs, relocate);
        tracePatternConstants(rule.rhs, relocate);
    }
}

std::optional<std::pair<slot_t, camel::core::type::Type *>>
DraftWritings::constantOf(gc_node_ref_t node) {
    const camel::core::sem::PermissionMask context = contextOf(node);
    const auto members                             = store_.members(context, node);
    const auto look                                = [&](gc_node_ref_t candidate)
        -> std::optional<std::pair<slot_t, camel::core::type::Type *>> {
        const auto *h = draft_.header(candidate);
        if (h == nullptr || !draft_.alive(candidate) || h->kind != GCNodeKind::Data ||
            h->dataIndex >= 0) {
            return std::nullopt;
        }
        const auto index = static_cast<size_t>(-h->dataIndex);
        if (index >= draft_.staticSlots().size() || !takesPart(candidate)) {
            return std::nullopt;
        }
        camel::core::type::Type *slotType = draft_.staticSlotTypes()[index];
        camel::core::type::Type *ty       = slotType != nullptr ? slotType : h->dataType;
        return std::pair{canonicalConstant(draft_.staticSlots()[index], ty), ty};
    };
    if (members.empty()) {
        return look(node);
    }
    for (gc_node_ref_t member : members) {
        if (auto found = look(member)) {
            return found;
        }
    }
    return std::nullopt;
}

gc_node_ref_t DraftWritings::constantNode(
    slot_t value, camel::core::type::Type *type, camel::core::sem::PermissionMask perms,
    uint32_t region) {
    const slot_t stored = canonicalConstant(value, type);
    NodeKey key;
    key.kind    = static_cast<uint8_t>(GCNodeKind::Data);
    key.perms   = perms;
    key.region  = region;
    key.typeId  = canonicalTypeIdOf(type);
    key.payload = constantPayload(stored, type);
    auto &ix    = *index_;
    if (auto found = ix.byKey.find(key); found != ix.byKey.end()) {
        return found->second;
    }
    GraphDraft::WritingScope scope(draft_);
    const gc_node_ref_t node = draft_.materializeStaticValue(stored, type);
    if (perms != 0) {
        draft_.setNodePermissions(node, perms);
    }
    if (region != 0) {
        draft_.setNodeRegion(node, region);
    }
    index(node);
    return node;
}

/// Whether a pattern term of `patternType` may match a node of `nodeType`: the type the pattern
/// was written for must accept the node's (a `f32[..]` pattern reads a `f32[3]`,
/// a pattern for `f32[..]` never reads a `int[..]`). The scalar operators have one id per
/// element type, so their id already says this; the tensor operators share one id for every
/// element type, and without the check a tensor law would rewrite another dtype's arithmetic
/// with an f32 rounding argument (rewrite design 2.2).
namespace detail {
bool acceptsType(
    const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType) {
    return patternType == nullptr || nodeType == nullptr ||
           patternType->assignableFrom(const_cast<camel::core::type::Type *>(nodeType));
}
} // namespace

bool DraftWritings::matchFrom(
    camel::core::sem::PermissionMask context, std::vector<Item> &work, size_t at,
    std::vector<gc_node_ref_t> &bind, std::vector<uint8_t> &bound,
    std::vector<gc_node_ref_t> *waits, bool commuted, const std::function<bool()> &emit,
    MatchRemainder *remainder) {
    if (at == work.size()) {
        return emit();
    }
    const Item item  = work[at];
    const Pattern &p = *item.pattern;
    switch (p.kind) {
    case Pattern::Kind::Var:
        if (p.constOnly && !constantOf(item.node).has_value()) {
            return false;
        }
        if (const auto *header = draft_.header(item.node);
            header == nullptr || !acceptsType(p.type, header->dataType)) {
            return false;
        }
        if (bound[p.var]) {
            bool same = store_.same(context, bind[p.var], item.node);
            if (!same && p.constOnly) {
                // A repeated `const c` is a repeated constant value, not a repeated node: source
                // `x * 3.0 + x * 3.0` has two DATA nodes of the same value.
                const auto a = constantOf(bind[p.var]);
                const auto b = constantOf(item.node);
                same = a && b && sameConstantValue(a->first, a->second, b->first, b->second);
            }
            return same && matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
        }
        bound[p.var] = 1;
        bind[p.var]  = item.node;
        {
            const bool matched =
                matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
            bound[p.var] = 0;
            return matched;
        }
    case Pattern::Kind::Constant: {
        const auto found = constantOf(item.node);
        if (!found || !sameConstantValue(found->first, found->second, p.value, p.type)) {
            return false;
        }
        return matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
    }
    case Pattern::Kind::Op: {
        // The root of a rule is the node itself; below it, any writing of the value read.
        std::vector<gc_node_ref_t> candidates;
        if (at == 0) {
            candidates.push_back(item.node);
        } else {
            const auto members = store_.members(context, item.node);
            candidates.assign(members.begin(), members.end());
            if (candidates.empty()) {
                candidates.push_back(item.node);
            }
        }
        // One pairing of the pattern's operands with a candidate's data inputs. `swapped` reads
        // the two inputs in the other order; it is legal only for a binary term whose operator's
        // declared semantics say the exchange gives the same value for this element type.
        const auto matchCandidate = [&](gc_node_ref_t candidate, bool swapped) -> bool {
            const auto *h = draft_.header(candidate);
            if (h == nullptr || !draft_.alive(candidate) || h->kind != GCNodeKind::Oper ||
                h->permissions != matchPerms_ || !acceptsType(p.type, h->dataType) ||
                !takesPart(candidate) ||
                reinterpret_cast<const GCOperBody *>(draft_.payloadOf(candidate).data())->oper !=
                    p.oper) {
                return false;
            }
            const KernelArguments arguments = kernelArgumentsOf(draft_, candidate);
            const auto &inputs              = arguments.norm;
            if (inputs.size() != p.args.size() || !arguments.with.empty()) {
                return false;
            }
            if (swapped && (inputs.size() != 2 || !declaredCommutative(p.oper, h->dataType))) {
                return false;
            }
            // What this term waits for: the rewrite carries it onto the writing that replaces the
            // root, so the law does not move the computation across an edge that orders it -- a
            // control edge, or the sync edge of the `sync` block's chain it is written in.
            const size_t savedWaits = waits != nullptr ? waits->size() : 0;
            if (waits != nullptr) {
                for (gc_node_ref_t wait : draft_.waitInputsOf(candidate)) {
                    if (std::find(waits->begin(), waits->end(), wait) == waits->end()) {
                        waits->push_back(wait);
                    }
                }
            }
            const size_t saved = work.size();
            for (size_t i = 0; i < inputs.size(); ++i) {
                const size_t input = swapped ? (i == 0 ? 1 : 0) : i;
                work.push_back({&p.args[i], inputs[input]});
            }
            const bool matched =
                matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
            work.resize(saved);
            if (waits != nullptr) {
                waits->resize(savedWaits);
            }
            return matched;
        };
        bool accepted = false;
        for (gc_node_ref_t candidate : candidates) {
            accepted = matchCandidate(candidate, false) || accepted;
        }
        if (!accepted && commuted) {
            // Nothing matched in the order the law wrote. A law is about values, not about the
            // order the program wrote them in: if the operator's semantics say exchanging its
            // operands gives the same value, the other order denotes the same left side, so the
            // right side (built below from the law's own text) is the same value too. The
            // natural order was tried first, so a law that writes the operands in a particular
            // order keeps that order whenever the program matches it, and the replacement keeps
            // the written shape. Operators that do not declare commutativity are never commuted,
            // and a two-way rule's reverse already reads the other order (`commuted` false).
            for (gc_node_ref_t candidate : candidates) {
                accepted = matchCandidate(candidate, true) || accepted;
            }
        }
        if (!accepted && commuted && remainder != nullptr && at == 0 && p.args.size() >= 2) {
            // A chain of an operator whose own declaration says exchanging its operands and
            // regrouping its chain give the same value is read as the multiset of its terms
            // (rewrite design 6): `a + b + c` is not expanded into every bracketing and order, so a
            // pattern that names fewer terms than the program wrote -- `x + 0` -- finds its `0` in
            // the multiset and leaves the rest to the one variable the left side leaves for it.
            // Reached only when the written order and the exchanged order both missed, and only
            // for a one-way law, as the exchange is.
            const auto matchMultiset = [&](gc_node_ref_t candidate) -> bool {
                const auto *h = draft_.header(candidate);
                if (h == nullptr || !draft_.alive(candidate) || h->kind != GCNodeKind::Oper ||
                    h->permissions != matchPerms_ || !acceptsType(p.type, h->dataType) ||
                    !takesPart(candidate) || !draft_.withInputsOf(candidate).empty() ||
                    reinterpret_cast<const GCOperBody *>(draft_.payloadOf(candidate).data())
                            ->oper != p.oper ||
                    !declaredAssociative(p.oper, h->dataType)) {
                    return false;
                }
                // The terms: what takes part in the chain is a term of it, the rest is a leaf. The
                // intermediate nodes are collected too, because they are the only values the
                // multiset view must not build (design 6 reads a chain that is already there).
                std::vector<gc_node_ref_t> terms;
                const std::function<void(gc_node_ref_t)> collect = [&](gc_node_ref_t in) {
                    const auto *ih = draft_.header(in);
                    const bool inChain =
                        ih != nullptr && draft_.alive(in) && ih->kind == GCNodeKind::Oper &&
                        ih->permissions == matchPerms_ && takesPart(in) &&
                        draft_.withInputsOf(in).empty() && draft_.ctrlInputsOf(in).empty() &&
                        draft_.syncInputsOf(in).empty() && draft_.normInputsOf(in).size() == 2 &&
                        reinterpret_cast<const GCOperBody *>(draft_.payloadOf(in).data())->oper ==
                            p.oper;
                    if (!inChain) {
                        terms.push_back(in);
                        return;
                    }
                    for (gc_node_ref_t next : draft_.normInputsOf(in)) {
                        collect(next);
                    }
                };
                for (gc_node_ref_t in : draft_.normInputsOf(candidate)) {
                    collect(in);
                }
                std::sort(terms.begin(), terms.end());
                // The pattern's own terms: its chain flattens the same way.
                std::vector<const Pattern *> pterms;
                const std::function<void(const Pattern &)> flatten = [&](const Pattern &q) {
                    if (q.kind == Pattern::Kind::Op && q.oper == p.oper) {
                        for (const Pattern &arg : q.args) {
                            flatten(arg);
                        }
                        return;
                    }
                    pterms.push_back(&q);
                };
                for (const Pattern &arg : p.args) {
                    flatten(arg);
                }
                if (pterms.size() > terms.size()) {
                    return false;
                }
                // Which variable may take the terms no pattern term names: one that stands for a
                // single value of the left side and is read exactly once, so a repeated variable
                // (`x + x`) never swallows the rest and nothing is dropped by accident.
                size_t restIndex = pterms.size();
                if (pterms.size() < terms.size()) {
                    std::vector<uint32_t> seen;
                    const std::function<void(const Pattern &)> count = [&](const Pattern &q) {
                        if (q.kind == Pattern::Kind::Var) {
                            seen.push_back(q.var);
                            return;
                        }
                        for (const Pattern &arg : q.args) {
                            count(arg);
                        }
                    };
                    count(p);
                    size_t candidates_ = 0;
                    for (size_t i = 0; i < pterms.size(); ++i) {
                        const Pattern &q = *pterms[i];
                        if (q.kind != Pattern::Kind::Var || q.constOnly) {
                            continue;
                        }
                        if (std::count(seen.begin(), seen.end(), q.var) != 1) {
                            continue;
                        }
                        restIndex = i;
                        ++candidates_;
                    }
                    if (candidates_ != 1) {
                        return false;
                    }
                }
                // The terms the pattern's own terms take, and the ones the rest variable takes:
                // every term of the chain goes to exactly one of them, or the left side would
                // mean a value the program does not have. Which term each pattern term takes is a
                // search, not a walk over every permutation: a chain of two hundred calls must not
                // be permuted to find out that not one of them is the zero a law asks for (the
                // timeout of was this explosion through saturation). So a pattern term that
                // can read no term of the chain ends the search at once, the pattern terms that
                // could read the fewest terms are placed first, and terms that are one value are
                // interchangeable -- the second of two equal terms says nothing the first did not.
                const size_t named = restIndex == pterms.size() ? pterms.size() : pterms.size() - 1;
                std::vector<size_t> namedIndex;
                for (size_t i = 0; i < pterms.size(); ++i) {
                    if (i != restIndex) {
                        namedIndex.push_back(i);
                    }
                }
                // Whether one pattern term could read one term of the chain: the necessary
                // conditions of the cases of the matcher, checked without binding anything.
                const auto reads = [&](const Pattern &q, gc_node_ref_t term) {
                    const auto *th = draft_.header(term);
                    if (th == nullptr || !draft_.alive(term)) {
                        return false;
                    }
                    switch (q.kind) {
                    case Pattern::Kind::Var:
                        if (q.constOnly && !constantOf(term).has_value()) {
                            return false;
                        }
                        return acceptsType(q.type, th->dataType);
                    case Pattern::Kind::Constant: {
                        const auto found = constantOf(term);
                        return found &&
                               sameConstantValue(found->first, found->second, q.value, q.type);
                    }
                    case Pattern::Kind::Op: {
                        // A term stands for its whole class: any writing of it is a candidate,
                        // exactly as it is lower down the pattern.
                        const auto readsWriting = [&](gc_node_ref_t writing) {
                            const auto *wh = draft_.header(writing);
                            return wh != nullptr && draft_.alive(writing) &&
                                   wh->kind == GCNodeKind::Oper && wh->permissions == matchPerms_ &&
                                   acceptsType(q.type, wh->dataType) && takesPart(writing) &&
                                   reinterpret_cast<const GCOperBody *>(
                                       draft_.payloadOf(writing).data())
                                           ->oper == q.oper;
                        };
                        const auto members = store_.members(context, term);
                        if (members.empty()) {
                            return readsWriting(term);
                        }
                        for (gc_node_ref_t writing : members) {
                            if (readsWriting(writing)) {
                                return true;
                            }
                        }
                        return false;
                    }
                    case Pattern::Kind::Compute:
                        return false;
                    }
                    return false;
                };
                std::vector<size_t> fits(named, 0);
                for (size_t i = 0; i < named; ++i) {
                    for (gc_node_ref_t term : terms) {
                        if (reads(*pterms[namedIndex[i]], term)) {
                            ++fits[i];
                        }
                    }
                    if (fits[i] == 0) {
                        return false;
                    }
                }
                std::vector<size_t> visit(named);
                for (size_t i = 0; i < named; ++i) {
                    visit[i] = i;
                }
                std::sort(visit.begin(), visit.end(), [&](size_t a, size_t b) {
                    return fits[a] < fits[b];
                });
                const size_t savedWaits = waits != nullptr ? waits->size() : 0;
                if (waits != nullptr) {
                    for (gc_node_ref_t wait : draft_.waitInputsOf(candidate)) {
                        if (std::find(waits->begin(), waits->end(), wait) == waits->end()) {
                            waits->push_back(wait);
                        }
                    }
                }
                std::vector<size_t> placement(named, 0);
                std::vector<uint8_t> used(terms.size(), 0);
                const size_t savedWork                  = work.size();
                const std::function<bool(size_t)> place = [&](size_t step) -> bool {
                    if (step == named) {
                        for (size_t i = 0; i < named; ++i) {
                            work.push_back({pterms[namedIndex[i]], terms[placement[i]]});
                        }
                        if (restIndex != pterms.size()) {
                            // What the rest variable stands for, set before the condition and the
                            // right side read it: the terms the pattern's own terms did not take.
                            remainder->set  = true;
                            remainder->var  = pterms[restIndex]->var;
                            remainder->oper = p.oper;
                            remainder->terms.clear();
                            for (size_t i = 0; i < terms.size(); ++i) {
                                if (!used[i]) {
                                    remainder->terms.push_back(terms[i]);
                                }
                            }
                        } else {
                            remainder->set = false;
                            remainder->terms.clear();
                        }
                        const bool matched = matchFrom(
                            context,
                            work,
                            at + 1,
                            bind,
                            bound,
                            waits,
                            commuted,
                            emit,
                            remainder);
                        work.resize(savedWork);
                        if (!matched) {
                            remainder->set = false;
                            remainder->terms.clear();
                        }
                        return matched;
                    }
                    const size_t pi  = visit[step];
                    const Pattern &q = *pterms[namedIndex[pi]];
                    for (size_t j = 0; j < terms.size(); ++j) {
                        if (used[j] != 0 || !reads(q, terms[j])) {
                            continue;
                        }
                        bool equal = false;
                        for (size_t k = 0; k < j && !equal; ++k) {
                            equal = used[k] == 0 && terms[k] == terms[j];
                        }
                        if (equal) {
                            continue;
                        }
                        used[j]       = 1;
                        placement[pi] = j;
                        if (place(step + 1)) {
                            return true;
                        }
                        used[j] = 0;
                    }
                    return false;
                };
                if (!place(0)) {
                    if (waits != nullptr) {
                        waits->resize(savedWaits);
                    }
                    return false;
                }
                return true;
            };
            for (gc_node_ref_t candidate : candidates) {
                if (matchMultiset(candidate)) {
                    accepted = true;
                    break;
                }
            }
        }
        return accepted;
    }
    case Pattern::Kind::Compute:
        return false; // right side only
    }
    return false;
}

gc_node_ref_t
DraftWritings::build(const Pattern &pattern, const Match &match, const DraftNodeInit &base) {
    switch (pattern.kind) {
    case Pattern::Kind::Var:
        if (match.remainder.set && match.remainder.var == pattern.var &&
            !match.remainder.terms.empty()) {
            // The terms of the chain the left side did not name, as one value of the same operator:
            // the canonical form of the multiset, ascending, folded left to right (rewrite design
            // 6; the terms are terms of a bit-exactly associative operator, so the bracketing is
            // the program's to choose and this one only has to be deterministic).
            std::vector<gc_node_ref_t> terms = match.remainder.terms;
            std::sort(terms.begin(), terms.end());
            GCOperBody body;
            body.oper = match.remainder.oper;
            std::vector<std::byte> bytes(sizeof(GCOperBody));
            std::memcpy(bytes.data(), &body, sizeof(body));
            gc_node_ref_t chain = terms.front();
            for (size_t i = 1; i < terms.size(); ++i) {
                DraftNodeInit init          = base;
                init.kind                   = GCNodeKind::Oper;
                init.dataIndex              = 0;
                init.dataType               = writingType(pattern.type, base.dataType);
                init.payload                = bytes;
                const gc_node_ref_t pair[2] = {chain, terms[i]};
                chain                       = lookupOrAdd(init, pair);
            }
            return chain;
        }
        return match.vars[pattern.var];
    case Pattern::Kind::Constant:
        return constantNode(
            pattern.value,
            writingType(pattern.type, base.dataType),
            base.permissions,
            base.region);
    case Pattern::Kind::Compute: {
        std::vector<slot_t> values;
        for (const Pattern &arg : pattern.args) {
            const gc_node_ref_t node = build(arg, match, base);
            const auto value         = constantOf(node);
            if (!value) {
                throw std::logic_error(
                    "A computed term of a rule reads a value that is not a constant.");
            }
            values.push_back(value->first);
        }
        return constantNode(
            pattern.compute(values),
            writingType(pattern.type, base.dataType),
            base.permissions,
            base.region);
    }
    case Pattern::Kind::Op: {
        std::vector<gc_node_ref_t> inputs;
        for (const Pattern &arg : pattern.args) {
            inputs.push_back(build(arg, match, base));
        }
        GCOperBody body;
        body.oper = pattern.oper;
        std::vector<std::byte> bytes(sizeof(GCOperBody));
        std::memcpy(bytes.data(), &body, sizeof(body));
        DraftNodeInit init = base;
        init.kind          = GCNodeKind::Oper;
        init.dataIndex     = 0;
        init.dataType      = writingType(pattern.type, base.dataType);
        init.payload       = bytes;
        return lookupOrAdd(init, inputs);
    }
    }
    return kInvalidNodeRef;
}

} // namespace camel::runtime
