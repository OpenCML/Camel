// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    /// Write the document in the shape `egraph-serialize` reads (the classes as the egraph's
    /// e-classes, every writing as one of its nodes) instead of the display document: what the
    /// community's `egraph-visualizer` opens (§5.04 item 3).
    bool egraph = false;
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
/// The implementation of `std::jgir`; its name, documentation and parameters are the
/// declaration in stdlib/std.cml.
PassMaker jgirPass();

class JsonGirDumpPass : public RuntimeGraphTranslatePass {
  public:
    explicit JsonGirDumpPass(
        const camel::core::context::context_ptr_t &ctx, JsonGirOptions options = {})
        : RuntimeGraphTranslatePass(ctx), options_(options) {}
    const camel::runtime::Program *apply(const camel::runtime::Program *, std::ostream &) override;

    /// A graph with values that have several writings is written as it is: the document carries
    /// the classes, so a reader never sees an extraction it did not ask for.
    bool acceptsWritings() const override { return true; }

  private:
    JsonGirOptions options_;
};

