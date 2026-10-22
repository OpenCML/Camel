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
#include "camel/execute/pass/params.h"
#include "camel/execute/pass/runtime_trans.h"
#include "camel/runtime/program.h"
#include <string>

/// What a JSON GIR document is: the display document every reader has seen since version 1, or
/// the canonical document of `docs/technical/gir-interchange.md` (version 2), which keeps only
/// what graph equivalence is defined on.
struct JsonGirOptions {
    bool canonical = false;
};

std::string dumpJsonRuntimeGraph(
    camel::runtime::GCGraph *, const camel::core::context::context_ptr_t &,
    const JsonGirOptions &options = {});
/// The same for a graph of `program`.
std::string dumpJsonRuntimeGraph(
    const camel::runtime::Program &program, camel::runtime::GCGraph *root,
    const camel::core::context::context_ptr_t &ctx, const JsonGirOptions &options = {});
/// The canonical document of `root`: reachable graphs only, names, origins and node ids erased,
/// annotation regions (inline, block) dissolved, types as a structural table with named
/// declarations as atoms. `docs/technical/gir-interchange.md`, "Canonical documents".
std::string
dumpCanonicalRuntimeGraph(const camel::runtime::Program &program, camel::runtime::GCGraph *root);
/// The parameters of `std::jgir`; the default options produce the version 1 document.
PassDescriptor jgirDescriptor();

class JsonGirDumpPass : public RuntimeGraphTranslatePass {
  public:
    explicit JsonGirDumpPass(
        const camel::core::context::context_ptr_t &ctx, JsonGirOptions options = {})
        : RuntimeGraphTranslatePass(ctx), options_(options) {}
    const camel::runtime::Program *apply(const camel::runtime::Program *, std::ostream &) override;

  private:
    JsonGirOptions options_;
};
