// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What one application of rules to a program cost (H226): the program it produced and the
 * counters the engine kept while applying them. `rewrite` and `saturate` answer the program
 * alone; `rewrite::run` answers this value, and its readers give the numbers, so the cost of a
 * rule application can be measured from Camel.
 *
 * The object keeps the program it produced; the counters are plain numbers. An application
 * rewrites every graph of the program; the rewrites, the nodes visited and the nodes erased are
 * summed over the graphs, and the rounds are the rounds the whole application made (every graph
 * advances in the same round), not a sum of per-graph round counts.
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/other.h"
#include "camel/runtime/program.h"
#include "camel/runtime/writings.h"

#include <cstdint>
#include <string>

namespace camel::rewritemod {

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class RunType final : public type::OtherType {
  public:
    static RunType *get();
    static bool is(const type::Type *type);

    std::string toString() const override { return "Run"; }
    std::string mangle() const override { return "rewrite.Run"; }
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *other) const override;
    type::CastSafety castSafetyFrom(type::Type *source) const override;
    bool assignableFrom(type::Type *source) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    explicit RunType(type::TypeCode code) : OtherType(code) {}
};

class RunObject final : public rtdata::Object {
  public:
    RunObject(const RunObject &)            = delete;
    RunObject &operator=(const RunObject &) = delete;

    static RunObject *create(
        const camel::runtime::Program *program, const camel::runtime::RewriteStats &stats,
        double seconds, camel::core::mm::IAllocator &allocator);

    const camel::runtime::Program *program() const { return program_; }
    size_t rewrites() const { return stats_.applied; }
    size_t rounds() const { return stats_.rounds; }
    size_t visited() const { return stats_.visited; }
    size_t erased() const { return stats_.erased; }
    bool fixpoint() const { return stats_.fixpoint; }
    /// The wall time of the application: compiling the rules, matching, rewriting and the
    /// commit (nothing else).
    double seconds() const { return seconds_; }

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(camel::core::mm::IAllocator &allocator, const type::Type *type, bool deep = false) const
        override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    RunObject(
        const camel::runtime::Program *program, const camel::runtime::RewriteStats &stats,
        double seconds)
        : program_(program), stats_(stats), seconds_(seconds) {}

    const camel::runtime::Program *program_;
    camel::runtime::RewriteStats stats_;
    double seconds_;
};

} // namespace camel::rewritemod