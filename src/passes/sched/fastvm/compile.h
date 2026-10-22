// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "bytecode.h"
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "optimize.h"
#include <functional>
#include <optional>
#include <span>

namespace ctx = camel::core::context;

struct CompileStrategy {
    bool enableTailCallDetection = true;
    bool enableInlineOperators   = false;

    // optimization strategies to apply
    OptimizationStrategyCode optimizationStrategies = OptimizationStrategyCode::None;
};

struct BytecodeIndex {
    size_t offset;
    size_t length;
    camel::runtime::GCGraph *runtimeGraph;
};

struct LinkedBytecodeResult {
    bytecode_vec_t codes;
    std::vector<BytecodeIndex> graphs;
    std::unordered_map<camel::runtime::GCGraph *, size_t> offsetMap;
};

bytecode_vec_t compile(
    const ctx::context_ptr_t &ctx, const camel::runtime::Program &program,
    camel::runtime::GCGraph *graph, const CompileStrategy &opt = {},
    std::unordered_map<size_t, camel::source::origin_id_t> *localPcOrigins = nullptr);

LinkedBytecodeResult compileAndLink(
    ctx::context_ptr_t ctx, const camel::runtime::Program &program, camel::runtime::GCGraph *entry,
    const CompileStrategy &opt);

/// The entry pc of a graph that already has bytecode, if it does.
using KnownEntryPc = std::function<std::optional<size_t>(camel::runtime::GCGraph *)>;

/**
 * Compiles the graphs reachable from `entry` that `known` (graph -> entry pc) has no pc for,
 * as bytecode placed at `baseOffset` onward, and links it: direct calls and tail calls reach both
 * the new graphs and the known ones. The result's offsets are absolute; its offsetMap holds the
 * new graphs only. This is how graphs created while the program runs (a gradient built at run
 * time) join the bytecode compiled at startup.
 */
LinkedBytecodeResult compileAndLinkFrom(
    ctx::context_ptr_t ctx, const camel::runtime::Program &program, camel::runtime::GCGraph *entry,
    const CompileStrategy &opt, const KnownEntryPc &known, size_t baseOffset,
    bool registerOrigins = true);

/**
 * The same for exactly the graphs listed (in that order), whatever else they reach: for a host
 * that runs only some of the graphs of a program as bytecode (the parallel scheduler). Every
 * graph a listed graph calls directly must be listed too or have a pc in `known`.
 */
LinkedBytecodeResult compileAndLinkGraphs(
    ctx::context_ptr_t ctx, const camel::runtime::Program &program,
    std::span<camel::runtime::GCGraph *const> graphs, const CompileStrategy &opt,
    const KnownEntryPc &known, size_t baseOffset, bool registerOrigins = true);

std::string opCodeToString(const Bytecode &bc, const ctx::context_ptr_t &context);
