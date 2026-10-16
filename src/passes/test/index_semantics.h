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
 * TEST-ONLY: `test::index_semantics` exercises the core of the index semantics
 * (camel/core/index_semantics.h) without any module: the quasi-affine expression pool (its
 * simplifications, interning, text, evaluation and the forms it rejects), the structural
 * validation of instances, the query through the operator table for an operator of the
 * protocol `semtest` declared here, the text form, splitting dims for tiles and the strided
 * view of a row-major operand. It prints one line per check, `INDEX <check> <result>`, and
 * returns the program's graph unchanged. It can only be named when the environment variable
 * CAMEL_ENABLE_TEST_PASSES is set (fault.h).
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kIndexSemanticsTestPass = "test::index_semantics";

class IndexSemanticsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
