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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY: `test::operator_semantics` exercises the registration and query interface of
 * operator semantics (camel/core/operator_semantics.h, OperatorTable::declareSemantics) and
 * prints one line per check, `SEMANTICS <check> <result>`. It can only be named when the
 * environment variable they are named by a test pipeline (fault.h).
 *
 * It declares operators of the protocol `semtest`, which no module has: every registration
 * check of design section 4.4 that phase 1 implements gets one declaration that must be
 * rejected; then the version rules of bind, the declaration epoch, the key text and the
 * permission names. The program's graph is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kOperatorSemanticsTestPass = "test::operator_semantics";

class OperatorSemanticsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
