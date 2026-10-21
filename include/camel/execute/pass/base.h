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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GraphIRPass {
  protected:
    camel::core::context::context_ptr_t context_;

  public:
    GraphIRPass(const camel::core::context::context_ptr_t &ctx) : context_(ctx) {};
    virtual ~GraphIRPass() = default;

    // Compile finishes before pass execution starts. The post-compile pipeline
    // therefore carries runtime programs (camel/runtime/program.h) exclusively: a pass gets
    // the program the previous one left and returns the program for the next, the one it was
    // given when it did not rewrite, nullptr when it consumed the program.
    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) = 0;

    // A pass that reads or rewrites graphs with several writings (writings.h). The pass driver
    // extracts one writing per value before a pass that consumes the program (a back end, an
    // exporter), and refuses the others.
    virtual bool acceptsWritings() const { return false; }
};

class NullGraphIRPass : public GraphIRPass {
  public:
    NullGraphIRPass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~NullGraphIRPass() = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

using PassFactory =
    std::function<std::unique_ptr<GraphIRPass>(const camel::core::context::context_ptr_t &ctx)>;

/**
 * Registers a pass contributed by a module under a scoped name such as "tensor::fuse".
 *
 * Modules register their passes when they are loaded (during compilation, before any pass
 * runs). Lookup consults the built-in `std` scope first, so modules cannot shadow built-in
 * passes. Registering the same name again replaces the earlier factory.
 */
void registerModulePass(const std::string &path, PassFactory factory);

enum class PassApplyStatus {
    Transformed,
    Consumed,
    Failed,
};

struct PassApplyResult {
    const camel::runtime::Program *program = nullptr;
    PassApplyStatus status{PassApplyStatus::Transformed};

    bool failed() const { return status == PassApplyStatus::Failed; }
    bool consumed() const { return status == PassApplyStatus::Consumed; }
};

/**
 * Checks every name of a pass list, after alias resolution, before anything runs. Passes
 * contributed by modules ("tensor::fuse", "onnx::export") exist once the program's modules
 * are loaded, so this is called after compilation. Throws RuntimeDiag::UnrecognizedGraphPass
 * with stage `pass:<name>` for the first unknown name, after listing the available passes on
 * stderr. applyPassesDetailed calls it for its own list; a driver that runs several lists
 * (the given passes, then the fallback) calls it for all of them first.
 *
 * With a context, a name that is not known yet and is written `<module>::<pass>` first loads
 * the module `<module>` (as an `import` of it would): naming a module's pass on the command
 * line is enough to have it, the program need not import a module it does not use. A module
 * that cannot be found leaves the name unknown, which is reported as before.
 */
void requireKnownPasses(
    const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx = nullptr);

/**
 * The index of the first pass of the list that consumes the graph by its kind (a scheduling or
 * a translation pass); the passes written after it can never run. nullopt when there is none.
 * Unknown names are skipped.
 */
std::optional<size_t> firstConsumingPass(
    const std::vector<std::string> &passes, const camel::core::context::context_ptr_t &ctx);

/**
 * True when the first pass of the list that consumes the graph is a translation pass: the list
 * produces a translated artefact (a dump, generated code, ...) and the program is never
 * executed. False when no pass consumes the graph (the fallback then runs it) or when the first
 * consumer is a scheduling pass. Unknown names are skipped: a module pass that the program does
 * not load cannot be a translation of it.
 *
 * A caller decides with this whether the entry module must define `main`: translation passes
 * work on library modules too, execution does not.
 */
bool passListTranslatesProgram(
    const std::vector<std::string> &passes, const camel::core::context::context_ptr_t &ctx);

/**
 * The warning RuntimeDiag::PassesNeverRun for the passes written after the first consuming pass
 * of the list, or nullopt when there are none.
 */
std::optional<camel::core::error::Diagnostic> passesNeverRunWarning(
    const std::vector<std::string> &passes, const camel::core::context::context_ptr_t &ctx);

/**
 * True when the pipeline names `std::profile` (or an alias of it) anywhere: the driver turns on
 * the phase recorder before compiling, because the report of that pass carries the phase split
 * of the compilation, which starts before any pass can run.
 */
bool pipelineNamesProfile(const std::vector<std::string> &passes);

/**
 * A warning (SemanticDiag::TagHasNoReader) for each tag the program writes whose declaration
 * names the passes that read it (`read_by`) and none of them is in the pipeline: the tag then
 * has no effect. A reader matches a pass of the list by name or by the pass's alias, and a
 * module name (`tensor`) matches every pass of it (`tensor::fuse`). The warnings are returned
 * in the order the tags are first written; the caller applies the warning policy and reports
 * them.
 */
std::vector<camel::core::error::Diagnostic> tagsNeverReadWarnings(
    const std::vector<std::string> &passes, const camel::core::context::context_ptr_t &ctx);

/**
 * Applies passes to the given graph.
 *
 * Return value contract:
 * - `PassApplyStatus::Failed`: pass execution failed; caller should inspect `ctx->rtmDiags()`
 * - `PassApplyStatus::Consumed`: execution/scheduling finished successfully and consumed the graph
 * - non-null graph: pass transformed the graph and left it for later passes
 */
PassApplyResult applyPassesDetailed(
    const camel::runtime::Program *program, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);

/** Backward-compatible wrapper: only returns the program, without status. */
const camel::runtime::Program *applyPasses(
    const camel::runtime::Program *program, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);
