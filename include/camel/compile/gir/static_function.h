// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/rtdata/tuple.h"

namespace camel::compile::gir {

/*
 * Compile-time static function carrier.
 *
 * This object is intentionally GIR-local. It exists only inside compile-time
 * graph static slots so rewrite and validation logic can keep using normal
 * GC-traced object graphs while runtime `::Function` stays a pure GCGraph
 * closure carrier.
 */
class StaticFunction : public camel::core::rtdata::Object {
  public:
    StaticFunction(const StaticFunction &)            = delete;
    StaticFunction &operator=(const StaticFunction &) = delete;

    static StaticFunction *create(
        const std::shared_ptr<DraftGraphBuilder> &graph, const camel::core::type::Type *tupleType,
        camel::core::mm::IAllocator &allocator);

    const std::shared_ptr<DraftGraphBuilder> &graph() const { return graph_; }
    void setGraph(std::shared_ptr<DraftGraphBuilder> graph) {
        ASSERT(graph != nullptr, "Compile-time static function graph cannot be null.");
        graph_ = std::move(graph);
    }
    ::Tuple *tuple() { return closure_; }
    const ::Tuple *tuple() const { return closure_; }
    const camel::core::type::TupleType *tupleType() const;

    bool equals(
        const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
        bool deep = false) const override;
    camel::core::rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(
        const camel::core::rtdata::Object::RefRelocator &relocate,
        const camel::core::type::Type *type) override;

  private:
    explicit StaticFunction(
        std::shared_ptr<DraftGraphBuilder> graph, const camel::core::type::TupleType *tupleType)
        : graph_(std::move(graph)), closure_(nullptr), tupleType_(tupleType) {}

    std::shared_ptr<DraftGraphBuilder> graph_;
    ::Tuple *closure_                              = nullptr;
    const camel::core::type::TupleType *tupleType_ = nullptr;
};

/*
 * A function value whose graph is not one of this program's compile graphs.
 *
 * A value-level transform answers a call with a graph of its own, built while the caller's graph
 * is being built (`camel/compile/gir/compile_time.h`): the graph is made in a program of the
 * transform's -- the way the run-time kernel makes one in a program of its own -- and encoded
 * there, so there is no compile graph of this program to name. The value names that program and
 * the graph in it, which is what a run-time `::Function` holds, and the encoder turns this
 * carrier into exactly such a value (graph_encode.cpp, canonicalizeStaticSlot).
 *
 * It is a carrier and not a decision: it holds what the transform answered and nothing about why.
 */
class StaticProgramFunction : public camel::core::rtdata::Object {
  public:
    StaticProgramFunction(const StaticProgramFunction &)            = delete;
    StaticProgramFunction &operator=(const StaticProgramFunction &) = delete;

    static StaticProgramFunction *create(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        const camel::core::type::FunctionType *type, camel::core::mm::IAllocator &allocator);

    /// The program the graph lives in.
    const camel::runtime::Program *program() const { return program_; }
    camel::runtime::GraphId graphId() const { return graph_; }
    /// The type of the value: the lifted signature the call was resolved to.
    const camel::core::type::FunctionType *funcType() const { return type_; }
    /// The closure layout of the value: empty, because the graph it names is entered with the
    /// parameters a caller passes and reads no captured value.
    const camel::core::type::TupleType *tupleType() const;

    bool equals(
        const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
        bool deep = false) const override;
    camel::core::rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(
        const camel::core::rtdata::Object::RefRelocator &relocate,
        const camel::core::type::Type *type) override;

  private:
    StaticProgramFunction(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        const camel::core::type::FunctionType *type)
        : program_(program), graph_(graph), type_(type) {}

    const camel::runtime::Program *program_             = nullptr;
    camel::runtime::GraphId graph_                      = camel::runtime::kInvalidGraphId;
    const camel::core::type::FunctionType *type_        = nullptr;
};

/// Whether a static slot's `Function` value is `StaticFunction`: the carrier of a compile graph of
/// this program. Callers that read the graph behind a function value ask this first, because the
/// other carrier (`StaticProgramFunction`) names a graph this program does not hold.
bool isStaticFunction(const camel::core::rtdata::Object *object);
/// Whether a static slot's `Function` value is `StaticProgramFunction`.
bool isStaticProgramFunction(const camel::core::rtdata::Object *object);

} // namespace camel::compile::gir
