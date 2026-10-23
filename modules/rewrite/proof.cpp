// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file proof.cpp
 * @brief The owned side of `Proof`: allocation, copying, printing, and the one traced reference.
 *
 * The record and the class are declared in proof.h so a reader (the debugger) can read a proof
 * without linking this module; everything a proof has to do at run time is here.
 */

#include "proof.h"

#include "camel/core/mm.h"

#include <new>
#include <ostream>

namespace camel::rewritemod {

namespace {

std::string countedText(size_t n, const char *one, const char *many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

ProofObject *allocateProof(
    const RewriteRecord &record, const camel::runtime::RewriteStats &stats,
    std::optional<ProofObject::Chain> chain, mm::IAllocator &allocator) {
    auto *copy = new RewriteRecord(record);
    void *mem  = allocator.alloc(sizeof(ProofObject), alignof(ProofObject));
    if (mem == nullptr) {
        delete copy;
        throw std::bad_alloc();
    }
    auto *proof = new (mem) ProofObject(copy, stats);
    if (chain) {
        proof->setChain(std::move(*chain));
    }
    // The collector does not look at dead young objects one by one; it must be told which ones
    // own memory it has to release (the same contract as `Foreign` and `Array`).
    allocator.noteFinalizable(proof);
    return proof;
}

/// One chain as `a = ... = b`: the first value, then a line per rewrite naming the value it
/// reaches, the rule, and whether the step was made the other way round.
void printChain(
    std::ostream &os, const camel::runtime::RewriteLog &log, const ProofObject::Chain &chain,
    const ProofObject::ChainPath &path, const std::string &indent) {
    os << indent << chain.from << "\n";
    for (const ProofObject::Link &link : path) {
        const camel::runtime::RewriteStep &step = log[link.step];
        os << indent << "  = " << linkAfter(step, link.reverse) << "  by " << step.rule;
        if (link.reverse) {
            os << "  (backwards)";
        }
        os << "  (round " << step.round << ", graph " << step.graph << ")\n";
    }
}

} // namespace

ProofObject *ProofObject::create(
    const RewriteRecord &record, const camel::runtime::RewriteStats &stats,
    mm::IAllocator &allocator) {
    return allocateProof(record, stats, std::nullopt, allocator);
}

ProofObject *ProofObject::createChain(
    const RewriteRecord &record, const camel::runtime::RewriteStats &stats, Chain chain,
    mm::IAllocator &allocator) {
    return allocateProof(record, stats, std::move(chain), allocator);
}

bool ProofObject::equals(const rtdata::Object *other, const Type *, bool) const {
    return this == other;
}

rtdata::Object *ProofObject::clone(mm::IAllocator &allocator, const Type *, bool) const {
    return chain_ ? createChain(*record_, stats_, *chain_, allocator)
                  : create(*record_, stats_, allocator);
}

void ProofObject::print(std::ostream &os, const Type *) const {
    if (chain_) {
        const Chain &chain = *chain_;
        if (chain.same) {
            os << "the same value: " << chain.from
               << " is one node, so no rewrite is needed to connect it to itself";
            return;
        }
        if (!chain.none.empty()) {
            os << chain.none;
            return;
        }
        const size_t count = chain.paths.size();
        if (count == 1) {
            os << "chain " << chain.from << " = " << chain.to << ": "
               << countedText(chain.paths.front().size(), "rewrite", "rewrites") << "\n";
            printChain(os, record_->steps, chain, chain.paths.front(), "  ");
            return;
        }
        os << countedText(count, "chain", "chains") << " of the same length from " << chain.from
           << " to " << chain.to << ", in the order the rewrites were made\n";
        for (size_t i = 0; i < count; ++i) {
            os << "  " << (i + 1) << ". ";
            printChain(os, record_->steps, chain, chain.paths[i], "     ");
        }
        return;
    }
    const auto &log = record_->steps;
    if (log.empty()) {
        os << "no rewrite matched in " << countedText(stats_.rounds, "round", "rounds")
           << ": the program is unchanged";
        return;
    }
    os << countedText(log.size(), "rewrite", "rewrites") << " in "
       << countedText(stats_.rounds, "round", "rounds") << ", " << stats_.visited
       << " nodes visited\n";
    for (const camel::runtime::RewriteStep &step : log) {
        const std::string before =
            step.matchedText.empty() ? std::string("%?") : step.matchedText.front();
        os << "  " << step.graph << " round " << step.round << ": " << before << "\n";
        os << "  = " << step.producedText << "  by " << step.rule << "\n";
        os << "    matched";
        for (const std::string &text : step.matchedText) {
            os << " " << text << ";";
        }
        os << "\n";
        if (!step.erasedText.empty()) {
            os << "    erased";
            for (const std::string &text : step.erasedText) {
                os << " " << text << ";";
            }
            os << "\n";
        }
    }
}

void ProofObject::finalize() noexcept {
    chain_.reset();
    delete record_;
    record_ = nullptr;
}

void ProofObject::updateRefs(const rtdata::Object::RefRelocator &relocate, const Type *type) {
    // A proof's steps are text and node ids, not GC values; the one reference it holds is the
    // program the run started from, which a chain may still name nodes of.
    if (input_ == nullptr ||
        !mm::graphSpace().isAllocated(const_cast<camel::runtime::Program *>(input_))) {
        input_ = nullptr;
        return;
    }
    input_ = static_cast<const camel::runtime::Program *>(relocate(
        const_cast<camel::runtime::Program *>(input_), nullptr,
        rtdata::RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = nullptr,
            .ownerKind = "Proof",
            .slotName  = "input",
        }));
}

} // namespace camel::rewritemod