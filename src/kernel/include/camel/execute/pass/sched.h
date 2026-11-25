// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"

class GraphSchedulePass : public GraphIRPass {
  public:
    GraphSchedulePass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~GraphSchedulePass() = default;

    virtual const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override = 0;

    /// Whether this pass executes the program. A scheduling pass usually does -- that is what its
    /// kind means (design principles 9.1) -- and the one that does not is the pass that ends the
    /// pipeline without a run (9.2).
    ///
    /// This is the criterion of "is this pass an entry", and it has two readers, both about the
    /// entry program: a pass that does not execute is not given the entry's arguments (the pipeline
    /// driver), and a list that ends in it does not need the entry module to define `main`
    /// (`passListEndsWithoutExecuting`, which asks before any pass can be constructed and so reads
    /// the answer where the pass declares it -- `NopPass::kName` -- rather than from a string of its
    /// own). A pass that stops executing the program says so once, beside its own declaration.
    virtual bool executesTheProgram() const { return true; }
};

/// The scheduling pass that produces no effect: it discards the graph and executes nothing, so the
/// pipeline ends here without a run (design principles 9: a scheduling pass answers with the
/// residual graph or an empty one; this one answers with an empty one and does no work).
class NopPass : public GraphSchedulePass {
  public:
    /// The name the pipeline spells this pass by, written once. Both questions about whether a pass
    /// executes the program read it: the registration of the pass
    /// (`src/stdlib/std/nop/nop.cpp`) and `passListEndsWithoutExecuting`, which answers for a
    /// pipeline before any pass can be built. A rename moves both, so the two cannot disagree.
    static constexpr const char *kName = "std::nop";

    NopPass(const camel::core::context::context_ptr_t &ctx) : GraphSchedulePass(ctx) {};
    virtual ~NopPass() = default;

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

    bool executesTheProgram() const override { return false; }

    /// A program with values that have several writings is taken and dropped: this pass reads no
    /// part of the program, so the state never reaches anything that would have to choose one
    /// writing, and nothing has to be extracted on the way. Declaring it is what makes the recipe
    /// design principles 9.1 gives for looking at a graph without running it legal on such a
    /// program -- `camel f.cml std::cxx::saturate std::gir std::nop` prints the classes and stops,
    /// where without it the pipeline would fall through to `std::default` and be refused with
    /// `PassNeedsExtraction`. Every pass that reads the program keeps answering as it did: this
    /// declaration is about the one pass that discards it.
    bool acceptsWritings() const override { return true; }
};
