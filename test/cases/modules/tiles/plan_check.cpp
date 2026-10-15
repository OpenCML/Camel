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
        p.output = 4;
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
        rejects(p, [](auto &x) { x.output = 1; }, "output has no materialized result");
        rejects(p, [](auto &x) { x.launches[0].blockX = 0; }, "launch dimensions");
        std::cout << "tiles plan validation: 10 rejected mutations\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
