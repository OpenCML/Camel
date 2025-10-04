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

#pragma once

#ifndef NDEBUG

#include "visual_tracer.h"

namespace profiler {

class CompilerTracer {
  public:
    static void lexerStart(const std::string &filename);
    static void lexerEnd(const std::string &filename);
    static void lexerToken(const std::string &token_type);

    static void parserStart(const std::string &rule_name);
    static void parserEnd(const std::string &rule_name);
    static void parserError(const std::string &error_msg);

    static void semanticStart(const std::string &phase);
    static void semanticEnd(const std::string &phase);
    static void semanticSymbol(const std::string &symbol_name);

    static void typeCheckStart(const std::string &expr_type);
    static void typeCheckEnd(const std::string &expr_type);
    static void typeInference(const std::string &var_name);

    static void codegenStart(const std::string &target);
    static void codegenEnd(const std::string &target);
    static void codegenFunction(const std::string &func_name);

    static void optimizationStart(const std::string &pass_name);
    static void optimizationEnd(const std::string &pass_name);
    static void optimizationApplied(const std::string &optimization);

    static void memoryAlloc(size_t size, const std::string &type);
    static void memoryFree(size_t size, const std::string &type);
    static void memoryUsage(size_t current, size_t peak);

    static void counterTokens(int count);
    static void counterASTNodes(int count);
    static void counterSymbols(int count);
    static void counterFunctions(int count);
    static void counterErrors(int count);
    static void counterWarnings(int count);
};

#define TRACE_LEXER_START(filename) profiler::CompilerTracer::lexerStart(filename)
#define TRACE_LEXER_END(filename) profiler::CompilerTracer::lexerEnd(filename)
#define TRACE_LEXER_TOKEN(token_type) profiler::CompilerTracer::lexerToken(token_type)

#define TRACE_PARSER_START(rule) profiler::CompilerTracer::parserStart(rule)
#define TRACE_PARSER_END(rule) profiler::CompilerTracer::parserEnd(rule)
#define TRACE_PARSER_ERROR(msg) profiler::CompilerTracer::parserError(msg)

#define TRACE_SEMANTIC_START(phase) profiler::CompilerTracer::semanticStart(phase)
#define TRACE_SEMANTIC_END(phase) profiler::CompilerTracer::semanticEnd(phase)
#define TRACE_SEMANTIC_SYMBOL(symbol) profiler::CompilerTracer::semanticSymbol(symbol)

#define TRACE_TYPECHECK_START(type) profiler::CompilerTracer::typeCheckStart(type)
#define TRACE_TYPECHECK_END(type) profiler::CompilerTracer::typeCheckEnd(type)
#define TRACE_TYPEINFERENCE(var) profiler::CompilerTracer::typeInference(var)

#define TRACE_CODEGEN_START(target) profiler::CompilerTracer::codegenStart(target)
#define TRACE_CODEGEN_END(target) profiler::CompilerTracer::codegenEnd(target)
#define TRACE_CODEGEN_FUNCTION(func) profiler::CompilerTracer::codegenFunction(func)

#define TRACE_OPTIMIZATION_START(pass) profiler::CompilerTracer::optimizationStart(pass)
#define TRACE_OPTIMIZATION_END(pass) profiler::CompilerTracer::optimizationEnd(pass)
#define TRACE_OPTIMIZATION_APPLIED(opt) profiler::CompilerTracer::optimizationApplied(opt)

#define TRACE_MEMORY_ALLOC(size, type) profiler::CompilerTracer::memoryAlloc(size, type)
#define TRACE_MEMORY_FREE(size, type) profiler::CompilerTracer::memoryFree(size, type)
#define TRACE_MEMORY_USAGE(current, peak) profiler::CompilerTracer::memoryUsage(current, peak)

#define TRACE_COUNTER_TOKENS(count) profiler::CompilerTracer::counterTokens(count)
#define TRACE_COUNTER_AST_NODES(count) profiler::CompilerTracer::counterASTNodes(count)
#define TRACE_COUNTER_SYMBOLS(count) profiler::CompilerTracer::counterSymbols(count)
#define TRACE_COUNTER_FUNCTIONS(count) profiler::CompilerTracer::counterFunctions(count)
#define TRACE_COUNTER_ERRORS(count) profiler::CompilerTracer::counterErrors(count)
#define TRACE_COUNTER_WARNINGS(count) profiler::CompilerTracer::counterWarnings(count)

} // namespace profiler

#else

#define TRACE_LEXER_START(filename)
#define TRACE_LEXER_END(filename)
#define TRACE_LEXER_TOKEN(token_type)

#define TRACE_PARSER_START(rule)
#define TRACE_PARSER_END(rule)
#define TRACE_PARSER_ERROR(msg)

#define TRACE_SEMANTIC_START(phase)
#define TRACE_SEMANTIC_END(phase)
#define TRACE_SEMANTIC_SYMBOL(symbol)

#define TRACE_TYPECHECK_START(type)
#define TRACE_TYPECHECK_END(type)
#define TRACE_TYPEINFERENCE(var)

#define TRACE_CODEGEN_START(target)
#define TRACE_CODEGEN_END(target)
#define TRACE_CODEGEN_FUNCTION(func)

#define TRACE_OPTIMIZATION_START(pass)
#define TRACE_OPTIMIZATION_END(pass)
#define TRACE_OPTIMIZATION_APPLIED(opt)

#define TRACE_MEMORY_ALLOC(size, type)
#define TRACE_MEMORY_FREE(size, type)
#define TRACE_MEMORY_USAGE(current, peak)

#define TRACE_COUNTER_TOKENS(count)
#define TRACE_COUNTER_AST_NODES(count)
#define TRACE_COUNTER_SYMBOLS(count)
#define TRACE_COUNTER_FUNCTIONS(count)
#define TRACE_COUNTER_ERRORS(count)
#define TRACE_COUNTER_WARNINGS(count)

#endif
