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
 * A whole program as one C++ translation unit (docs/design/cpp-standalone/README.cn.md,
 * section 3): which graphs become functions, the entry, and the layout of the file.
 *
 * The file includes camel/standalone/runtime.h and nothing of the compiler: it builds with any
 * C++20 compiler and the headers of a Camel installation, and the program does not link the
 * runtime.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"

#include "function_emitter.h"

#include <string>
#include <vector>

namespace camel::passes::cpp {

enum class CppEmitMode {
    Plain,   // a program: the functions main reaches, and an `int main()`
    Module,  // the named functions of the module, without an entry
    Inspect, // as Module, each function preceded by a comment listing its nodes
    Bench,   // as Plain (kept for the pass name std::cpp::bench; design note, Q5)
};

class ProgramEmitter {
  public:
    ProgramEmitter(camel::core::context::context_ptr_t context, CppEmitMode mode)
        : context_(std::move(context)), mode_(mode), parts_(context_) {}

    /// The translation unit for the program whose root graph is `root`. Throws Refusal for a
    /// construct std::cpp does not translate.
    std::string emit(GCGraph *root);

  private:
    /// The graph of `main`, called by the root graph, and what the root passes it.
    struct Entry {
        GCGraph *main      = nullptr;
        gc_node_ref_t call = camel::runtime::kInvalidNodeRef; // the root's CALL of main
        std::vector<gc_node_ref_t> args; // nodes of the root: the captured values, in order
    };

    Entry findEntry(GCGraph *root) const;
    /// Every graph that is a function of the program, callees after their callers' discovery.
    std::vector<GCGraph *> collectFunctions(const std::vector<GCGraph *> &roots);
    /// The functions in the groups they are written in: one each, except functions that call
    /// each other in tail position, which share one loop (design note 6.3).
    std::vector<std::vector<GCGraph *>> tailCallGroups(const std::vector<GCGraph *> &functions);
    std::string entryFunction(GCGraph *root, const Entry &entry);
    std::string inspectComment(GCGraph *graph) const;

    camel::core::context::context_ptr_t context_;
    CppEmitMode mode_;
    ProgramParts parts_;
    NameScope globals_;
};

} // namespace camel::passes::cpp
