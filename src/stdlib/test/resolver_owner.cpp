// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Who owns an operator's type rule, read from a second context built beside the running one.
 *
 * An operator's type rule -- the signature a call is typed through -- names the types the
 * declarations of *its* context wrote. `Range` is the type a declaration writes, not a type the
 * process has: each context's prelude declares its own, and `to_range` is a rule of the context
 * that declared it. So the rule belongs to the context, and the process's operator table keeps
 * only what an operator is and what implements it.
 *
 * This pass reads that with both contexts alive at once. It asks the running context for the
 * `to_range` rule and for the `Range` its signature returns, builds a second context, declares and
 * builds its prelude (the rules a declaration writes join the built-in group of their name when the
 * declaring module is built), asks the second for the same two things, and reads the running
 * context's answer again -- the second context is gone by the last reading. One line for the test
 * plan:
 *
 *   RESOLVER-PROBE a_rule=<0|1> b_rule=<0|1> rule_differs=<0|1> a_exit=Range b_exit=Range
 *                  exit_differs=<0|1> a_unchanged=<0|1> a_exit_stable=<0|1> a_alive=<0|1>
 *                  b_prelude=<built|...>
 *
 * `a_rule` / `b_rule` are both contexts answering a rule for `to_range`, `rule_differs` is the two
 * answers being different rules (two live contexts do not contend for one), `a_exit` / `b_exit` are
 * the names of the types the two rules return and `exit_differs` is those being two different
 * `Range` objects -- each context's own, which is the reading this pass exists for. `a_unchanged`
 * is the running context's rule being the same rule and the same `Range` after the second context
 * was read: a second context's declarations do not overwrite the first's answers. `a_exit_stable`
 * is the same reading taken after the second context's scope ended: the running context's answer
 * does not go stale and does not dangle when the other context that registered rules is
 * destroyed. `a_alive` is the program this pass was given, still allocated in the graph space
 * after a major collection. `b_prelude` is what the second context made of its own declarations:
 * `built` is the case the reading above is about, any other answer says why it has no rule of its
 * own, and a reading taken without one would say nothing about two contexts.
 *
 * The second context is given the root the running one reads its prelude from: a context built
 * with no search root finds no prelude, and a context with no declarations of its own has no
 * `Range` to answer with.
 *
 * The defect this pass is written for: the rules lived in the process's operator table, keyed by
 * URI, so the second context's declarations replaced the first's answer for the same URI (the
 * first context then typed its calls with a `Range` it never declared, and the two contexts
 * contended for one entry), and a rule outlived the context whose types it names.
 */

#include "camel/passes/test/resolver_owner.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type/composite/func.h"
#include "camel/execute/pass/registration.h"
#include "camel/utils/path.h"

#include <exception>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace {

using camel::core::context::Context;
using camel::core::context::context_ptr_t;
using camel::core::type::FunctionType;
using camel::core::type::FuncTypeResolver;
using camel::core::type::Type;

/// The URI the prelude's declaration of `to_range` gives the rule, and the operator's own name:
/// asked in both spellings so the reading does not depend on which one a group publishes under.
constexpr std::string_view kToRangeUris[] = {"to_range", ":range/to_range"};

/// The rule a context answers for `to_range`, or null.
std::shared_ptr<FuncTypeResolver> ruleOf(const context_ptr_t &context) {
    for (std::string_view uri : kToRangeUris) {
        if (auto rule = context->resolverOf(std::string(uri)); rule != nullptr) {
            return rule;
        }
    }
    return nullptr;
}

/// The signature a rule declares: the prelude's declaration writes one, and a rule that computes
/// its result keeps the declared form as the signature of its parameters and its result.
FunctionType *signatureOf(const FuncTypeResolver &rule) {
    if (FunctionType *declared = rule.declaredSignature(); declared != nullptr) {
        return declared;
    }
    if (FunctionType *grafted = rule.graftedSignature(); grafted != nullptr) {
        return grafted;
    }
    return rule.fixedSignature();
}

/// The type a rule's declaration returns, or null.
Type *exitTypeOf(const std::shared_ptr<FuncTypeResolver> &rule) {
    if (rule == nullptr) {
        return nullptr;
    }
    FunctionType *signature = signatureOf(*rule);
    if (signature == nullptr || !signature->hasExitType()) {
        return nullptr;
    }
    return signature->exitType();
}

std::string nameOf(Type *type) { return type == nullptr ? std::string("none") : type->toString(); }

} // namespace

const camel::runtime::Program *
ResolverOwnerTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The context the running program is of: the rules a rewrite types its nodes through are this
    // context's, and the program carries the context it belongs to.
    const context_ptr_t running = program->context();
    if (running == nullptr) {
        os << "RESOLVER-PROBE no_context\n";
        return program;
    }

    const auto aRule  = ruleOf(running);
    Type *const aExit = exitTypeOf(aRule);

    std::shared_ptr<FuncTypeResolver> bRule;
    Type *bExit              = nullptr;
    bool aStillSame          = false;
    bool aExitSame           = false;
    bool bPreludeBuilt       = false;
    std::string bPreludeWhat = "no_prelude_module";
    {
        // The second context of the process, and its own declarations: the prelude is declared
        // (`checkPreludeDeclarations`, which is what a context does before it compiles anything)
        // and then built, because the rules a declaration writes join the built-in group of their
        // name when the declaring module is built, not when it is read.
        // The second context is given the root the running one reads its prelude from: a context
        // with no search root has no prelude, and then no declaration of its own to answer with.
        camel::core::context::EntryConfig config;
        if (const auto prelude = running->preludeModule(); prelude != nullptr) {
            config.searchPaths.push_back(
                camel::utils::pathToUtf8(
                    camel::utils::pathFromUtf8(prelude->path()).parent_path()));
        }
        context_ptr_t other = Context::create(config, camel::core::error::DiagsConfig{});
        if (const std::string broken = other->checkPreludeDeclarations(); !broken.empty()) {
            bPreludeWhat = "declare_failed:" + broken;
        } else if (other->preludeModule() == nullptr) {
            bPreludeWhat = "no_prelude_module";
        } else {
            auto prelude = std::static_pointer_cast<camel::core::module::UserDefinedModule>(
                other->preludeModule());
            try {
                bPreludeBuilt = prelude->compile(camel::core::module::CompileStage::Done);
                bPreludeWhat  = bPreludeBuilt ? "built" : "build_failed";
            } catch (const std::exception &e) {
                bPreludeWhat = std::string("build_threw:") + e.what();
            }
        }
        bRule = ruleOf(other);
        bExit = exitTypeOf(bRule);
        // Read the running context again while the second is alive: its own answer must not have
        // been replaced by the second context's.
        aStillSame = ruleOf(running) == aRule;
        aExitSame  = exitTypeOf(ruleOf(running)) == aExit;
    }
    // And once more with the second context destroyed: the running context's answer is neither
    // gone nor pointing at what the other context registered.
    const auto aAfterRule = ruleOf(running);
    const bool aStable =
        aAfterRule != nullptr && aAfterRule == aRule && exitTypeOf(aAfterRule) == aExit;

    camel::core::mm::autoSpace().majorGC();
    const bool alive =
        camel::core::mm::graphSpace().isAllocated(const_cast<camel::runtime::Program *>(program));

    os << "RESOLVER-PROBE a_rule=" << (aRule != nullptr ? 1 : 0)
       << " b_rule=" << (bRule != nullptr ? 1 : 0)
       << " rule_differs=" << (aRule != nullptr && bRule != nullptr && aRule != bRule ? 1 : 0)
       << " a_exit=" << nameOf(aExit) << " b_exit=" << nameOf(bExit)
       << " exit_differs=" << (aExit != nullptr && bExit != nullptr && aExit != bExit ? 1 : 0)
       << " a_unchanged=" << (aStillSame && aExitSame ? 1 : 0)
       << " a_exit_stable=" << (aStable ? 1 : 0) << " a_alive=" << (alive ? 1 : 0)
       << " b_prelude=" << bPreludeWhat << "\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements. Each name is the constant its own header declares,
/// so the name a pipeline writes and the class that answers it are one fact.
const PassKernel resolverOwnerKernel(kResolverOwnerTestPass, makePass<ResolverOwnerTestPass>());

} // namespace
