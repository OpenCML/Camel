/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Check physical lowering against the core map, including quotient/remainder views.
#include "tiles/internal/access.h"
#include "tiles/program.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace camel::tiles;
namespace sem = camel::core::sem;
namespace {
sem::IndexedInstance domain(const Shape &shape) {
    sem::IndexedInstance result;
    for (size_t i = 0; i < shape.size(); ++i) {
        result.domain.dims.push_back({"d" + std::to_string(i), result.exprs.constant(shape[i]), sem::DimRole::Parallel});
        result.output.coords.push_back(result.exprs.dim(static_cast<uint32_t>(i)));
    }
    return result;
}
Program view(Shape source, Shape target, sem::IndexedInstance instance) {
    Program p;
    p.nodes = {{.op = "input", .shape = std::move(source)},
               {.op = "reindex", .shape = std::move(target), .inputs = {0}, .indexing = std::move(instance)}};
    return p;
}
template<class F> void rejects(const Program &source, F change, const std::string &reason) {
    auto p = source;
    change(p.nodes[1], *p.nodes[1].indexing);
    try { (void)kernel::coreViewAddress(p, p.nodes[1], kernel::IndexExpr(0)); }
    catch (const std::exception &error) {
        if (std::string(error.what()).find(reason) != std::string::npos) return;
        throw;
    }
    throw std::runtime_error("accepted malformed core view: " + reason);
}
}
void checkCoreViews() {
    auto instance = domain({2, 4, 3});
    const auto flat = instance.exprs.add(instance.exprs.scale(3, instance.exprs.dim(0)), instance.exprs.dim(2));
    const auto two = instance.exprs.constant(2);
    instance.norm = {{.kind = sem::AccessKind::Affine,
                      .coords = {instance.exprs.floorDiv(flat, two), instance.exprs.mod(flat, two)}},
                     {.kind = sem::AccessKind::None}};
    auto p = view({3, 2}, {2, 4, 3}, instance);
    const auto address = kernel::coreViewAddress(p, p.nodes[1], kernel::IndexExpr::variable("i"));
    for (size_t i = 0; i < 24; ++i)
        if (address.evaluate({{"i", i}}) != (i / 12) * 3 + i % 3)
            throw std::runtime_error("quasi-affine core view changed its logical map");
    auto permutation = domain({4, 2, 3});
    permutation.norm = {{.kind = sem::AccessKind::Affine,
                         .coords = {permutation.exprs.dim(1), permutation.exprs.dim(2), permutation.exprs.dim(0)}}};
    const auto permuted = view({2, 3, 4}, {4, 2, 3}, permutation);
    const auto permAddress = kernel::coreViewAddress(permuted, permuted.nodes[1], kernel::IndexExpr::variable("i"));
    for (size_t i = 0; i < 24; ++i)
        if (permAddress.evaluate({{"i", i}}) != ((i / 3) % 2) * 12 + (i % 3) * 4 + i / 6)
            throw std::runtime_error("strided core view changed its logical map");
    auto emptyMap = domain({0, 4});
    emptyMap.norm = {{.kind = sem::AccessKind::Affine, .coords = {emptyMap.exprs.constant(-1)}}};
    const auto empty = view({0}, {0, 4}, emptyMap);
    if (kernel::coreViewAddress(empty, empty.nodes[1], kernel::IndexExpr(0)).evaluate({}) != 0)
        throw std::runtime_error("empty core view evaluated an unused address");
    rejects(p, [](auto &, auto &i) { i.norm[0].coords[0] = i.exprs.constant(3); }, "outside operand domain");
    rejects(p, [](auto &, auto &i) { i.norm[0].coords[0] = i.exprs.constant(-1); }, "negative or unresolved");
    rejects(p, [](auto &, auto &i) { i.norm[1].kind = sem::AccessKind::Affine; }, "unbound operand");
    rejects(p, [](auto &n, auto &) { n.strides = {6, 0, 1}; }, "private strides");
    rejects(p, [](auto &n, auto &) { n.shape = {2, 4, 2}; }, "does not match the output");
    rejects(p, [](auto &, auto &i) { i.output.coords[0] = i.exprs.dim(1); }, "identity domain");
    rejects(p, [](auto &, auto &i) {
        i.norm[0].coords[0] = i.exprs.add(i.exprs.constant(INT64_MAX), i.exprs.dim(0));
    }, "addition overflow");
    std::cout << "tiles core views: quasi-affine maps, strided simplification, empty domains and rejected mutations passed\n";
}
