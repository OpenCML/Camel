/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Explicit native reduction fixtures and independent core-domain traversal checks.
#include "camel/core/operator_table.h"
#include "tiles/internal/codegen_cache.h"
#include "tiles/internal/kernel_ir.h"
#include "tiles/program.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace camel::tiles;
using namespace camel::tiles::kernel;
namespace sem = camel::core::sem;

namespace {
void attach(Program &program, size_t node, sem::IndexedInstance instance, sem::ReductionLaw law) {
    static size_t sequence = 0;
    const auto uri = "tilestest:reduction_domain_" + std::to_string(sequence++);
    sem::OperatorSemantics declaration{.key = sem::OperatorKey::of(uri)};
    declaration.laws = {std::move(law)};
    auto &table = camel::core::OperatorTable::instance();
    const auto id = table.intern(uri);
    table.declareSemantics(uri, declaration);
    auto &value = program.nodes.at(node);
    value.semantic = program.contracts.size();
    program.contracts.push_back({.uri = uri, .key = declaration.key, .declaration = table.semantics(id)});
    instance.law = 0;
    value.indexing = std::move(instance);
}
sem::ReductionLaw sumLaw() {
    return {.name = "sum", .kind = sem::LawKind::Add,
            .input = camel::core::type::TypeCode::Float32,
            .accumulator = camel::core::type::TypeCode::Float64,
            .result = camel::core::type::TypeCode::Float32};
}
Program outputPermutation() {
    Program program;
    program.nodes = {{.op = "input", .shape = {24}, .data = std::vector<float>(24)},
                     {.op = "sum_axis", .shape = {4, 2}, .inputs = {0}, .attributes = {99}}};
    program.outputs = {1};
    sem::IndexedInstance instance;
    const auto i = instance.exprs.dim(0), r = instance.exprs.dim(1), j = instance.exprs.dim(2);
    instance.domain.dims = {{"i", instance.exprs.constant(2), sem::DimRole::Parallel},
                            {"r", instance.exprs.constant(3), sem::DimRole::Reduction},
                            {"j", instance.exprs.constant(4), sem::DimRole::Parallel}};
    const auto address = instance.exprs.add(
        instance.exprs.add(instance.exprs.mul(i, instance.exprs.constant(12)),
                           instance.exprs.mul(r, instance.exprs.constant(4))), j);
    instance.norm = {{.kind = sem::AccessKind::Affine, .coords = {address}}};
    instance.output = {.coords = {j, i}, .mode = sem::WriteMode::Combine};
    attach(program, 1, std::move(instance), sumLaw());
    return program;
}
template<class Mutate> void rejects(const Program &original, Mutate mutate, const char *reason) {
    auto changed = original;
    mutate(changed, *changed.nodes[1].indexing);
    try {
        (void)reductionAccess(changed, changed.nodes[1], IndexExpr(0), IndexExpr(0));
    } catch (const std::exception &error) {
        if (std::string(error.what()).find(reason) != std::string::npos)
            return;
        throw;
    }
    throw std::runtime_error(std::string("accepted incompatible reduction domain: ") + reason);
}
} // namespace

// Test constructors explicitly state the ordinary tensor operator's instance. The
// production planner never synthesizes missing semantics from these operator tags.
void attachNativeReduction(Program &program, size_t node) {
    auto &value = program.nodes.at(node);
    const auto &shape = program.nodes.at(value.inputs.at(0)).shape;
    sem::IndexedInstance instance;
    std::vector<sem::ExprId> dims;
    std::vector<bool> reduced(shape.size(), true);
    if (value.op.ends_with("_axis")) {
        for (size_t d = 0; d < shape.size(); ++d)
            reduced[d] = d == size_t(value.attributes.at(0));
    } else if (value.op == "sum_to") {
        const auto shift = shape.size() - value.shape.size();
        for (size_t d = shift; d < shape.size(); ++d)
            reduced[d] = value.shape[d - shift] != shape[d];
    }
    for (size_t d = 0; d < shape.size(); ++d) {
        dims.push_back(instance.exprs.dim(d));
        instance.domain.dims.push_back({"d" + std::to_string(d), instance.exprs.constant(shape[d]),
                                       reduced[d] ? sem::DimRole::Reduction : sem::DimRole::Parallel});
    }
    instance.norm = {{.kind = sem::AccessKind::Affine, .coords = dims}};
    instance.output.mode = sem::WriteMode::Combine;
    if (value.op.ends_with("_axis")) {
        for (size_t d = 0; d < shape.size(); ++d)
            if (!reduced[d])
                instance.output.coords.push_back(dims[d]);
            else if (value.shape.size() == shape.size())
                instance.output.coords.push_back(instance.exprs.constant(0));
    } else if (value.op == "sum_to") {
        const auto shift = shape.size() - value.shape.size();
        for (size_t d = shift; d < shape.size(); ++d)
            instance.output.coords.push_back(reduced[d] ? instance.exprs.constant(0) : dims[d]);
    }
    auto law = sumLaw();
    const auto f32 = camel::core::type::TypeCode::Float32;
    const bool maximum = value.op == "max_axis";
    law.result = value.dtype == DType::F64 ? camel::core::type::TypeCode::Float64 : f32;
    if (maximum || value.op == "sum_to")
        law.accumulator = f32;
    if (maximum) {
        law.kind = sem::LawKind::Max;
        law.initBits = 0xff800000ULL;
        law.empty = sem::LawEmpty::Fault;
    }
    if (value.op == "mean_all" || value.op == "mean_axis")
        law.finalize = sem::LawFinalize::DivideByCount;
    if (value.op == "mean_all")
        law.empty = sem::LawEmpty::Fault;
    attach(program, node, std::move(instance), std::move(law));
}

void checkReductionDomains() {
    auto program = outputPermutation();
    generate(program);
    if (canonicalAxisReduction(program, program.nodes[1]))
        throw std::runtime_error("generic domain was admitted to an incompatible physical axis kernel");
    for (size_t output = 0; output < 8; ++output)
        for (size_t r = 0; r < 3; ++r) {
            const auto access = reductionAccess(program, program.nodes[1], IndexExpr(output), IndexExpr(r));
            const auto expected = (output % 2) * 12 + r * 4 + output / 2;
            if (access.count != 3 || access.address.evaluate({}) != expected)
                throw std::runtime_error("permuted output/domain rank changed canonical contributions");
        }
    auto changed = program;
    auto &instance = *changed.nodes[1].indexing;
    instance.domain.dims[1].extent = instance.exprs.constant(2);
    const auto before = generationCacheStats();
    generate(changed);
    if (generationCacheStats().misses != before.misses + 1 || changed.source == program.source ||
        reductionAccess(changed, changed.nodes[1], IndexExpr(0), IndexExpr(0)).count != 2)
        throw std::runtime_error("domain contribution count was omitted from generated source/cache");
    Program interleaved;
    interleaved.nodes = {{.op = "input", .shape = {12}, .data = std::vector<float>(12)},
                         {.op = "sum_axis", .shape = {2}, .inputs = {0}}};
    interleaved.outputs = {1};
    sem::IndexedInstance ordered;
    const auto a = ordered.exprs.dim(0), b = ordered.exprs.dim(1), c = ordered.exprs.dim(2);
    ordered.domain.dims = {{"a", ordered.exprs.constant(3), sem::DimRole::Reduction},
                           {"b", ordered.exprs.constant(2), sem::DimRole::Parallel},
                           {"c", ordered.exprs.constant(2), sem::DimRole::Reduction}};
    ordered.norm = {{.kind = sem::AccessKind::Affine,
                     .coords = {ordered.exprs.add(ordered.exprs.add(
                         ordered.exprs.mul(a, ordered.exprs.constant(4)),
                         ordered.exprs.mul(b, ordered.exprs.constant(2))), c)}}};
    ordered.output = {.coords = {b}, .mode = sem::WriteMode::Combine};
    attach(interleaved, 1, std::move(ordered), sumLaw());
    const size_t expected[2][6] = {{0, 1, 4, 5, 8, 9}, {2, 3, 6, 7, 10, 11}};
    generate(interleaved);
    for (size_t output = 0; output < 2; ++output)
        for (size_t r = 0; r < 6; ++r) {
            const auto access = reductionAccess(interleaved, interleaved.nodes[1], IndexExpr(output), IndexExpr(r));
            if (access.count != 6 || access.address.evaluate({}) != expected[output][r])
                throw std::runtime_error("interleaved reduction dimensions changed lexicographic order");
        }
    rejects(program, [](auto &p, auto &) { p.nodes[1].indexing.reset(); }, "missing index instance");
    rejects(program, [](auto &, auto &i) { i.domain.dims[1].role = sem::DimRole::Sequential; }, "dimension roles");
    rejects(program, [](auto &, auto &i) { i.domain.dims[1].role = sem::DimRole::Parallel; }, "missing from reduction projection");
    rejects(program, [](auto &p, auto &i) { p.nodes[1].shape = {2, 2}; i.output.coords = {i.exprs.dim(0), i.exprs.dim(0)}; }, "incompatible reduction projection");
    rejects(program, [](auto &, auto &i) { i.output.coords[0] = i.exprs.dim(1); }, "reduction projection");
    rejects(program, [](auto &, auto &i) { i.domain.dims[1].extent = i.exprs.constant(4); }, "outside operand domain");
    auto empty = interleaved;
    auto &emptyInstance = *empty.nodes[1].indexing;
    emptyInstance.domain.dims[0].extent = emptyInstance.exprs.constant(0);
    auto faultLaw = sumLaw();
    faultLaw.empty = sem::LawEmpty::Fault;
    attach(empty, 1, emptyInstance, faultLaw);
    rejects(empty, [](auto &, auto &) {}, "empty reduction domain");
    empty.nodes[1].shape = {0};
    empty.nodes[1].indexing->domain.dims[1].extent = empty.nodes[1].indexing->exprs.constant(0);
    rejects(empty, [](auto &, auto &) {}, "empty reduction domain");
    Program emptyOutput;
    constexpr int64_t huge = 4294967296LL;
    emptyOutput.nodes = {{.op = "input", .shape = {huge, 0, huge}},
                          {.op = "max_axis", .shape = {huge, 0}, .inputs = {0}, .attributes = {2}}};
    emptyOutput.outputs = {1};
    sem::IndexedInstance sparse;
    const auto row = sparse.exprs.dim(0), column = sparse.exprs.dim(1), term = sparse.exprs.dim(2);
    const auto zero = sparse.exprs.constant(0), large = sparse.exprs.constant(huge);
    sparse.domain.dims = {{"row", large, sem::DimRole::Parallel},
                          {"column", zero, sem::DimRole::Parallel},
                          {"term", large, sem::DimRole::Reduction}};
    sparse.norm = {{.kind = sem::AccessKind::Affine,
                    .coords = {zero, zero, sparse.exprs.add(sparse.exprs.mul(large, row), term)}}};
    sparse.output = {.coords = {row, column}, .mode = sem::WriteMode::Combine};
    auto maximum = sumLaw();
    maximum.kind = sem::LawKind::Max;
    maximum.accumulator = camel::core::type::TypeCode::Float32;
    maximum.initBits = 0xff800000ULL;
    maximum.empty = sem::LawEmpty::Fault;
    maximum.exactAssociative = maximum.exactCommutative = maximum.hasIdentity = true;
    maximum.identityBits = 0xff800000ULL;
    attach(emptyOutput, 1, std::move(sparse), maximum);
    generate(emptyOutput);
    if (canonicalAxisReduction(emptyOutput, emptyOutput.nodes[1]) ||
        emptyOutput.launches[0].implementation != "standard" || emptyOutput.launches[0].elements != 0)
        throw std::runtime_error("empty output entered an overflowing physical axis fast path");
    std::cout << "tiles reduction domains: core extents, output permutations, canonical contributions and empty faults passed\n";
}
