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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/* Test-only: the local algebra as rules, saturated and extracted (default and strict policy). */
#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kSaturateTestPass     = "test::saturate";
inline constexpr const char *kSaturateSafeTestPass = "test::saturate_safe";
inline constexpr const char *kSaturateWideTestPass = "test::saturate_wide";
inline constexpr const char *kSaturateOneRoundTestPass = "test::saturate_one_round";
/* Reports the properties of saturation instead of a snapshot of its counters: the run terminates,
   a second saturation of the same writings adds no writing and no union, and two sessions over the
   same program produce the same counters. Prints no timing, so its output is comparable. */
inline constexpr const char *kSaturatePropsTestPass = "test::saturate_props";
class SaturateTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
class SaturateSafeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
/* The default policy, and also the function `wide` of the scale tool (a program of any size). */
class SaturateWideTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
/* The default policy with a budget of one round. */
class SaturateOneRoundTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
/* The properties of saturation (termination, the fixed point of the writings, and the same
   counters from two runs over the same program), under the default numeric policy. */
class SaturatePropsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
    bool acceptsWritings() const override { return true; }
};
