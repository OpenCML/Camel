/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Mutation tests for plan validation: failures must precede device execution.
#include "tiles/program.h"
#include "tiles/device.h"
#include <iostream>
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
        auto resident = std::make_shared<DeviceTensor>();
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
        rejects(borrowed, [](auto &x) { x.nodes[0].resident = std::make_shared<DeviceTensor>(); }, "invalid resident input");
        std::cout << "tiles plan validation: 28 rejected mutations\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
