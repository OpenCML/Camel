/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Mutation tests for plan validation: failures must precede device execution.
#include "tiles/program.h"
#include "tiles/device.h"
#include "tiles/internal/index_expr.h"
#include "tiles/internal/access.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace camel::tiles;

template<class F> void rejects(const Program &original, F mutate, const char *reason) {
    auto p = original;
    mutate(p);
    try { validatePlan(p); }
    catch (const std::runtime_error &e) {
        if (std::string(e.what()).find(reason) != std::string::npos) return;
        throw;
    }
    throw std::runtime_error(std::string("accepted invalid plan: ") + reason);
}

int main() {
    try {
        using kernel::IndexExpr;
        const auto i = IndexExpr::variable("i");
        // Transpose a 2x3 array, then apply a strided slice of the flattened view.
        const auto transposed = (i % IndexExpr(2)) * IndexExpr(3) + i / IndexExpr(2);
        const auto composed = transposed.substitute("i", IndexExpr(1) + i * IndexExpr(2));
        const uint64_t transposeExpected[] = {0, 3, 1, 4, 2, 5};
        const uint64_t sliceExpected[] = {3, 4, 5};
        for (uint64_t at = 0; at < 6; ++at)
            if (transposed.evaluate({{"i", at}}) != transposeExpected[at])
                throw std::runtime_error("structured transpose mapping mismatch");
        for (uint64_t at = 0; at < 3; ++at)
            if (composed.evaluate({{"i", at}}) != sliceExpected[at])
                throw std::runtime_error("structured composed mapping mismatch");
        auto rejected = [](auto action) {
            try { action(); } catch (const std::exception &) { return; }
            throw std::runtime_error("accepted invalid index expression");
        };
        rejected([&] { (void)(i / IndexExpr(0)); });
        rejected([&] { (void)(i % IndexExpr(0)); });
        rejected([&] { (void)IndexExpr::variable("i;return"); });
        rejected([&] { (void)i.evaluate({}); });
        rejected([&] { (void)(i + IndexExpr(1)).evaluate({{"i", UINT64_MAX}}); });
        rejected([&] { (void)(IndexExpr(UINT64_MAX) * IndexExpr(2)); });
        if ((IndexExpr(3) * IndexExpr(7)).cuda() != "21ULL")
            throw std::runtime_error("index constant folding mismatch");
        std::cout << "tiles index expressions: composition and checked evaluation passed\n";
        using kernel::Predicate;
        const auto bounded = Predicate::less(i, IndexExpr(3));
        const kernel::Access access{0, transposed, bounded};
        const auto tiled = access.substitute("i", IndexExpr::variable("base") + i);
        for (uint64_t base = 0; base < 5; ++base) {
            for (uint64_t lane = 0; lane < 4; ++lane) {
                const auto actual = tiled.evaluate({{"base", base}, {"i", lane}});
                if (actual.has_value() != (base + lane < 3) ||
                    (actual && *actual != transposeExpected[base + lane]))
                    throw std::runtime_error("masked access substitution mismatch");
            }
        }
        const kernel::Access masked{0, i + IndexExpr(1), Predicate(false)};
        if (masked.evaluate({{"i", UINT64_MAX}}))
            throw std::runtime_error("masked address was evaluated");
        const auto invalid = Predicate::less(IndexExpr(1) / i, IndexExpr(3));
        if ((Predicate(false) && invalid).evaluate({{"i", 0}}) ||
            !(Predicate(true) || invalid).evaluate({{"i", 0}}))
            throw std::runtime_error("predicate short circuit mismatch");
        rejected([&] { (void)invalid.evaluate({{"i", 0}}); });
        if ((!Predicate::equal(i, IndexExpr(2))).evaluate({{"i", 2}}))
            throw std::runtime_error("predicate equality mismatch");
        std::cout << "tiles accesses: masked composition and lazy evaluation passed\n";
        Program fusedReduction;
        fusedReduction.nodes = {{.op="input", .shape={4,2}, .data={1,2,3,4,5,6,7,8}},
                         {.op="input", .shape={}, .data={0.5}},
                         {.op="multiply", .shape={4,2}, .inputs={0,1}},
                         {.op="sum_axis", .shape={4}, .inputs={2}, .attributes={1}}};
        fusedReduction.outputs = {3};
        schedule(fusedReduction);
        if (fusedReduction.launches.size() != 1 || fusedReduction.storage.slots[2] != SIZE_MAX ||
            fusedReduction.launches[0].implementation != "ordered_reduce")
            throw std::runtime_error("producer was not fused into ordered reduction");
        rejects(fusedReduction, [](auto &x) { x.launches[0].blockX = 256; }, "ordered reduction operation or block");
        rejects(fusedReduction, [](auto &x) { x.nodes[3].op = "max_axis"; }, "ordered reduction operation or block");
        auto epilogue = fusedReduction;
        epilogue.nodes.push_back(Node{.op="add", .shape={4}, .inputs={3,1}});
        epilogue.outputs = {4};
        schedule(epilogue);
        if (epilogue.launches.size() != 1 || epilogue.storage.slots[3] != SIZE_MAX)
            throw std::runtime_error("reduction epilogue was materialized");
        rejects(epilogue, [](auto &x) {
            x.nodes[4] = Node{.op="reshape", .shape={1,4}, .inputs={3}};
        }, "ordered reduction epilogue domain");
        rejects(epilogue, [](auto &x) {
            x.nodes[4] = Node{.op="sum_axis", .shape={4}, .inputs={2}, .attributes={1}};
        }, "ordered reduction operation or block");
        fusedReduction.outputs = {2,3};
        schedule(fusedReduction);
        if (fusedReduction.launches.size() != 2 || fusedReduction.storage.slots[2] == SIZE_MAX)
            throw std::runtime_error("reduction fused an observable producer output");
        std::cout << "tiles ordered reductions: fusion boundaries and plan mutations passed\n";
        Program scalarInput;
        scalarInput.nodes = {{.op="input", .shape={}, .scalarBits=0x3ff0000000000001ULL, .dtype=DType::F64}};
        scalarInput.outputs = {0};
        schedule(scalarInput);
        if (bytes(scalarInput.nodes[0]) != 8 || !hostData(scalarInput.nodes[0]) ||
            !scalarInput.launches.empty())
            throw std::runtime_error("host scalar input representation mismatch");
        rejects(scalarInput, [](auto &x) { x.nodes[0].scalarBits.reset(); }, "invalid scalar input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].data = {1.0}; }, "invalid scalar input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].dtype = DType::F32; }, "invalid input payload");
        rejects(scalarInput, [](auto &x) { x.nodes[0].shape = {1}; }, "input payload dtype");
        std::cout << "tiles scalar inputs: typed payload and shape validation passed\n";
        Program p;
        p.nodes = {{.op="input", .shape={4}, .data={1,2,3,4}},
                   {.op="add", .shape={4}, .inputs={0,0}},
                   {.op="relu", .shape={4}, .inputs={1}},
                   {.op="sum_axis", .shape={}, .inputs={2}, .attributes={0}},
                   {.op="add", .shape={4}, .inputs={2,3}}};
        p.outputs = {4};
        schedule(p);
        validatePlan(p);
        if (p.launches.size() != 3 || p.storage.slots[1] != SIZE_MAX)
            throw std::runtime_error("expected chain fusion and shared-result boundary");
        rejects(p, [](auto &x) { x.nodes[1].inputs[0] = 4; }, "topologically earlier");
        rejects(p, [](auto &x) { x.nodes[1].shape = {5}; }, "binary result shape");
        rejects(p, [](auto &x) { x.nodes[3].attributes = {2}; }, "reduction axis");
        rejects(p, [](auto &x) { x.storage.slots[2] = x.storage.slots[0]; }, "overlapping buffer lifetimes");
        rejects(p, [](auto &x) { x.storage.bytes[x.storage.slots[0]] = 1; }, "undersized/missing allocation");
        rejects(p, [](auto &x) { x.launches[0].members.push_back(2); }, "duplicate/invalid scheduled value");
        rejects(p, [](auto &x) { x.launches[0].elements = 3; }, "launch extent");
        rejects(p, [](auto &x) { x.storage.slots[2] = SIZE_MAX; }, "register value escapes stage");
        rejects(p, [](auto &x) { x.outputs = {1}; }, "output has no materialized result");
        rejects(p, [](auto &x) { x.launches[0].blockX = 0; }, "launch dimensions");
        rejects(p, [](auto &x) { x.launches[0].implementation = "parallel_max"; }, "parallel maximum operation");
        rejects(p, [](auto &x) { x.launches[0].implementation = "unknown"; }, "unknown implementation");
        Program indexed;
        indexed.nodes = {{.op="input", .shape={2,3}, .data={1,2,3,4,5,6}},
                         {.op="reindex", .shape={3,2}, .inputs={0}, .strides={1,3}}};
        indexed.outputs = {1};
        schedule(indexed);
        validatePlan(indexed);
        rejects(indexed, [](auto &x) { x.nodes[1].strides = {1}; }, "index map rank");
        rejects(indexed, [](auto &x) { x.nodes[1].offset = 1; }, "index map outside source domain");
        rejects(indexed, [](auto &x) { x.nodes[1].strides[0] = SIZE_MAX; }, "index map address overflow");
        rejects(p, [](auto &x) { x.nodes[1].strides = {1}; }, "unexpected index map");
        rejects(p, [](auto &x) { x.nodes[2].op = "add"; x.nodes[2].inputs = {1,1}; }, "fused value must have one use");
        rejects(p, [](auto &x) { x.outputs = {}; }, "missing output");
        rejects(p, [](auto &x) { x.outputs = {4,99}; }, "missing output");
        rejects(p, [](auto &x) { x.outputs = {0,4}; }, "overlapping buffer lifetimes");
        auto multi = p;
        multi.outputs = {1,4,1,0};
        schedule(multi);
        validatePlan(multi);
        if (multi.storage.slots[1] == SIZE_MAX || multi.storage.slots[0] == multi.storage.slots[4])
            throw std::runtime_error("returned intermediates must remain live through download");
        auto reduced = indexed;
        reduced.nodes[1] = Node{.op="sum_to", .shape={1,3}, .inputs={0}};
        schedule(reduced);
        validatePlan(reduced);
        rejects(reduced, [](auto &x) { x.nodes[1].shape = {1,2,3}; }, "sum_to broadcast domain");
        auto scalar = indexed;
        scalar.nodes[1] = Node{.op="sum_all", .shape={}, .inputs={0}, .dtype=DType::F64};
        schedule(scalar);
        validatePlan(scalar);
        rejects(scalar, [](auto &x) { x.storage.bytes[x.storage.slots[1]] = 4; }, "undersized/missing allocation");
        rejects(scalar, [](auto &x) { x.nodes[1].dtype = DType::F32; }, "scalar reduction type");
        rejects(scalar, [](auto &x) { x.nodes[1].op = "relu"; }, "unsupported operation dtype");
        auto borrowed = indexed;
        auto resident = std::make_shared<DeviceValue>();
        resident->shape = {2,3};
        resident->address = 1; // Validator-only sentinel, never passed to CUDA.
        resident->owner = std::make_shared<int>(0);
        borrowed.nodes[0].resident = resident;
        borrowed.nodes[0].data.clear();
        schedule(borrowed);
        validatePlan(borrowed);
        if (borrowed.storage.slots[0] != SIZE_MAX || borrowed.storage.bytes.size() != 1)
            throw std::runtime_error("borrowed input must not allocate writable storage");
        rejects(borrowed, [](auto &x) { x.storage.slots[0] = 0; }, "borrowed input has writable allocation");
        rejects(borrowed, [](auto &x) { x.nodes[0].data = {1}; }, "invalid resident input");
        rejects(borrowed, [](auto &x) { x.nodes[1].resident = x.nodes[0].resident; }, "computed value borrows input storage");
        rejects(borrowed, [](auto &x) { x.nodes[0].resident = std::make_shared<DeviceValue>(); }, "invalid resident input");
        rejects(borrowed, [](auto &x) {
            auto value = std::make_shared<DeviceValue>(*x.nodes[0].resident);
            value->dtype = DType::F64;
            x.nodes[0].resident = value;
        }, "invalid resident input");
        rejects(borrowed, [](auto &x) { x.nodes[0].dtype = DType::F64; }, "input payload dtype");
        Program scalarOps;
        scalarOps.nodes = {{.op="constant64", .attributes={0}, .dtype=DType::F64},
                           {.op="constant64", .attributes={0}, .dtype=DType::F64},
                           {.op="scalar_add", .inputs={0,1}, .dtype=DType::F64},
                           {.op="scalar_neg", .inputs={2}, .dtype=DType::F64}};
        scalarOps.outputs = {3};
        schedule(scalarOps);
        if (scalarOps.launches.size() != 1 || scalarOps.storage.bytes != std::vector<size_t>{8})
            throw std::runtime_error("scalar chain did not fuse into one f64 result");
        rejects(scalarOps, [](auto &x) { x.nodes[2].dtype = DType::F32; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[2].inputs = {0}; }, "scalar arithmetic type or arity");
        rejects(scalarOps, [](auto &x) { x.nodes[0] = Node{.op="input", .data={1}}; }, "scalar operand type");
        std::cout << "tiles plan validation: 33 rejected mutations\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
