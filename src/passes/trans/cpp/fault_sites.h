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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The places of a translated program where a fault can happen (camel/standalone/core.h, Site).
 *
 * For each place and kind of fault, the report is rendered now, when the program is written,
 * by the compiler's own renderer (src/core/error/diagnostics/render.cpp), in both formats
 * `camel` has (the rich text and the one-line form). What only the running program knows is
 * left as a mark (the byte 0x1F and the index of the value) that the runtime fills in. So a
 * fault in the generated program reports the same text as in the interpreters, source excerpt
 * included, without the program carrying a renderer or its source files
 * (docs/design/cpp-standalone/README.cn.md, 7.1).
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/runtime/graph.h"

#include "cpp_types.h"

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace camel::passes::cpp {

/// The source origin of `ref`, or of the nearest input that has one (a node the compiler
/// synthesized carries none; what it reads does).
uint64_t nodeOriginNear(const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref);

/// The definition of the function `graph` was compiled from, or the first of its nodes that
/// has a source position.
uint64_t graphOriginOf(
    const camel::core::context::Context &context, const camel::runtime::GCGraph *graph);

class SiteTable {
  public:
    explicit SiteTable(camel::core::context::context_ptr_t context)
        : context_(std::move(context)) {}

    /// The text that stands for the `k`-th value the runtime passes to fault() (k < 10).
    static std::string valueMark(size_t k);

    /// The name of the Site of a fault of `kind` at node `node` of `graph`. `args` are the
    /// arguments of the diagnostic's message and suggestion templates, in order: a constant
    /// text, or valueMark(k).
    std::string siteFor(
        const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t node,
        camel::core::error::RuntimeDiag kind, const std::vector<std::string> &args);

    /// The name of the Place of the call of a runtime module's operator at `node` of `graph`
    /// (camel/standalone/core.h): the source excerpt and the start of the one-line report,
    /// to which the generated program adds the runtime's diagnostic when the operator faults.
    std::string placeFor(const camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t node);

    /// The name of the Site of a fault of `kind` at `origin`, a place of the program that is
    /// not a node: the entry function, for the reports of its command-line arguments.
    std::string siteAt(
        uint64_t origin, camel::core::error::RuntimeDiag kind, const std::vector<std::string> &args);

    /// A Site of a diagnostic that is not located in the program (the report of an error
    /// value nobody handled: what the value says is its location is put in when it happens).
    std::string unlocated(camel::core::error::RuntimeDiag kind, const std::vector<std::string> &args);

    /// The definitions of the sites asked for, in that order.
    std::string definitions() const;
    bool empty() const { return sites_.empty(); }

  private:
    struct Site {
        std::string name;
        std::string text;
        std::string brief;
        std::string comment; // where it is, for the reader of the generated file
        bool place = false;  // a Place: `text` is the excerpt and `brief` the start
        std::string kind;     // for an error value (attempt): the name without its prefix
        std::string message;  // for an error value: the message
        std::string location; // for an error value: `file.cml:line:col`
    };

    /// A new Site of a fault of `kind` at `origin` (none when it is kInvalidOriginId), whose
    /// name is `at_<line>_<column>`. Shared by siteFor and siteAt.
    std::string makeSite(
        uint64_t origin, camel::core::error::RuntimeDiag kind, const std::vector<std::string> &args);

    /// `file.cml:line:col` of `d`'s range, the file without its directory (as an error value
    /// names it, src/execute/fault_isolation.cpp).
    static std::string locationOf(const camel::core::error::Diagnostic &d);

    camel::core::context::context_ptr_t context_;
    std::map<std::tuple<const void *, uint16_t, uint32_t>, size_t> index_;
    std::vector<Site> sites_;
    NameScope names_;
};

} // namespace camel::passes::cpp
