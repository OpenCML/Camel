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
 * Created: Sep. 27, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "compiler_tracer.h"

#ifndef NDEBUG

namespace profiler {

void CompilerTracer::lexerStart(const std::string &filename) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("lexer_phase");
    }
}

void CompilerTracer::lexerEnd(const std::string &filename) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::lexerToken(const std::string &token_type) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("token_processed");
    }
}

void CompilerTracer::parserStart(const std::string &rule_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("parser_phase");
    }
}

void CompilerTracer::parserEnd(const std::string &rule_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::parserError(const std::string &error_msg) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("parser_error");
    }
}

void CompilerTracer::semanticStart(const std::string &phase) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("semantic_phase");
    }
}

void CompilerTracer::semanticEnd(const std::string &phase) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::semanticSymbol(const std::string &symbol_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("symbol_processed");
    }
}

void CompilerTracer::typeCheckStart(const std::string &expr_type) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("typecheck_phase");
    }
}

void CompilerTracer::typeCheckEnd(const std::string &expr_type) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::typeInference(const std::string &var_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("type_inference");
    }
}

void CompilerTracer::codegenStart(const std::string &target) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("codegen_phase");
    }
}

void CompilerTracer::codegenEnd(const std::string &target) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::codegenFunction(const std::string &func_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("function_generated");
    }
}

void CompilerTracer::optimizationStart(const std::string &pass_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_BEGIN("optimization_phase");
    }
}

void CompilerTracer::optimizationEnd(const std::string &pass_name) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_END();
    }
}

void CompilerTracer::optimizationApplied(const std::string &optimization) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("optimization_applied");
    }
}

void CompilerTracer::memoryAlloc(size_t size, const std::string &type) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("memory_allocated");
    }
}

void CompilerTracer::memoryFree(size_t size, const std::string &type) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("memory_freed");
    }
}

void CompilerTracer::memoryUsage(size_t current, size_t peak) {
    if (is_visual_tracing_active()) {
        TRACE_EVENT_INSTANT("memory_usage_updated");
    }
}

void CompilerTracer::counterTokens(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("tokens_processed", count, "compiler");
    }
}

void CompilerTracer::counterASTNodes(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("ast_nodes", count, "compiler");
    }
}

void CompilerTracer::counterSymbols(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("symbols", count, "compiler");
    }
}

void CompilerTracer::counterFunctions(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("functions", count, "compiler");
    }
}

void CompilerTracer::counterErrors(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("errors", count, "compiler");
    }
}

void CompilerTracer::counterWarnings(int count) {
    if (is_visual_tracing_active()) {
        VisualTracer::getInstance().recordCounter("warnings", count, "compiler");
    }
}

} // namespace profiler

#endif
