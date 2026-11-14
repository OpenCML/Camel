// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::rulesets: the index a program's ruleset modules hold. Prints one line per rule
 *
 *   RULESET <family> <head>/<arity> -> <rule>
 *
 * over every ruleset module built into this program, in family order, and hands the program on
 * unchanged. With the `query` parameter it answers lookups as well, in the same words a transform
 * asks them (`family|head|arity`, several separated by `;`):
 *
 *   LOOKUP <family> <head>/<arity> -> <rule> | no rule | no such family
 *
 * A question with no field (`:op/itod`) asks the other half of the same reading: the key a rule of
 * that operator is looked up under, which is read off the operator itself
 * (`camel/core/forward.h#ruleKeyOf`), and `none` when it has no key at all:
 *
 *   KEY <uri> -> <head>/<arity> | none
 *
 * This is the reading a ruleset is asked for, so a test can pin it: the index is a table the
 * transforms read, and a table nobody can ask is only a printout that looks like an answer.
 *
 * With `sig=true` it prints, one line per component of each rule's `<rule>__rhs` signature, whether
 * the type table holds that component:
 *
 *   SIG <family> <rule> exit interned=yes|no|absent type=<text>
 *   SIG <family> <rule> with <i> interned=... type=...
 *   SIG <family> <rule> norm <i> interned=... type=...
 *
 * `interned=no` is a type the table does not hold (its `typeId()` is 0), which is the defect a
 * `TypeId` key cannot tell apart from another such type; `absent` is a component a signature does
 * not have (no result type yet, or a hole the declaration has not typed).
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/runtime/program.h"

#include <string>

class RulesetsPass : public RuntimeGraphRewritePass {
  public:
    RulesetsPass(
        const camel::core::context::context_ptr_t &ctx, std::string query, bool forward,
        bool sig)
        : RuntimeGraphRewritePass(ctx), query_(std::move(query)), forward_(forward), sig_(sig) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    // What the caller asked, empty when it asked nothing: `family|head|arity`, `;`-separated.
    std::string query_;
    // Whether to print the forward rules the operators were bound to, and what the index answers
    // for them now (`forward=true`): the reading that shows the index is what a transform reads.
    bool forward_ = false;
    // Whether to print each component of each rule's `__rhs` signature and whether the type table
    // holds it (`sig=true`): the reading of a frozen signature's interning, component by component.
    bool sig_ = false;
};