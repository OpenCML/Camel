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
 * This is the reading a ruleset is asked for, so a test can pin it: the index is a table the
 * transforms read, and a table nobody can ask is only a printout that looks like an answer.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/runtime/program.h"

#include <string>

class RulesetsPass : public RuntimeGraphRewritePass {
  public:
    RulesetsPass(
        const camel::core::context::context_ptr_t &ctx, std::string query, bool forward)
        : RuntimeGraphRewritePass(ctx), query_(std::move(query)), forward_(forward) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    // What the caller asked, empty when it asked nothing: `family|head|arity`, `;`-separated.
    std::string query_;
    // Whether to print the forward rules the operators were bound to, and what the index answers
    // for them now (`forward=true`): the reading that shows the index is what a transform reads.
    bool forward_ = false;
};