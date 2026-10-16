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
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2024
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Versioned runtime GIR interchange, independent of layout engines and editor protocols.
#pragma once
#include "camel/execute/pass/runtime_trans.h"
#include <string>
std::string
dumpJsonRuntimeGraph(camel::runtime::GCGraph *, const camel::core::context::context_ptr_t &);
class JsonGirDumpPass : public RuntimeGraphTranslatePass {
  public:
    explicit JsonGirDumpPass(const camel::core::context::context_ptr_t &ctx)
        : RuntimeGraphTranslatePass(ctx) {}
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *, std::ostream &) override;
};
