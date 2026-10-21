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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY: `test::tags` reads a declaration fact back from the runtime graph, the way a
 * scheduler does, and prints one line per graph: `TAGS independent <name> yes|no`. It covers
 * the runtime query of the standard tag `independent` (design principles 3.6), which no
 * scheduler consumes yet (the question is in docs/handover/reports/H85.md).
 *
 * Only nameable when CAMEL_ENABLE_TEST_PASSES is set (fault.h). The program is returned
 * unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kTagsTestPass = "test::tags";

class TagsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};