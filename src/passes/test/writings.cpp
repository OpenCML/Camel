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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


/*
 * Test-only check of WritingStore: prints "WRITINGS <name> ok" for each property that holds and
 * "WRITINGS <name> FAIL <why>" for each that does not. The program itself is returned unchanged.
 */
#include "camel/passes/test/writings.h"

#include "camel/runtime/writings.h"

#include <format>
#include <string>

using namespace camel::runtime;

namespace {

struct Report {
    std::ostream &os;
    void check(const char *name, bool ok, const std::string &why = "") {
        os << "WRITINGS " << name << (ok ? " ok" : " FAIL " + why) << "\n";
    }
};

void unionFind(Report &r) {
    WritingStore store;
    r.check("merge_new", store.merge(5, 3) && store.merge(3, 9) && !store.merge(9, 5));
    r.check("root_is_smallest", store.find(0, 9) == 3 && store.find(0, 5) == 3);
    const auto members = store.members(0, 9);
    r.check(
        "members_ascending",
        members.size() == 3 && members[0] == 3 && members[1] == 5 && members[2] == 9);
    r.check("alone", store.find(0, 7) == 7 && store.members(0, 7).empty());
    // The same classes whatever the order of the merges.
    WritingStore other;
    other.merge(9, 3);
    other.merge(5, 9);
    other.merge(3, 5);
    r.check(
        "order_free",
        other.find(0, 5) == 3 && other.members(0, 5).size() == 3 &&
            other.classRoots(0) == std::vector<gc_node_ref_t>{3});
    // A log gives back the classes.
    WritingStore again(store.log());
    r.check("from_log", again.find(0, 9) == 3 && again.members(0, 3).size() == 3);
}

void layers(Report &r) {
    WritingStore store;
    store.merge(1, 2, 0);
    store.merge(2, 3, 0x01);
    store.merge(7, 8, 0x02);
    // A context with no permission sees only the exact equivalence.
    r.check("strict_context", store.same(0, 1, 2) && !store.same(0, 2, 3));
    r.check("granted_context", store.same(0x01, 1, 3) && !store.same(0x01, 7, 8));
    r.check("two_grants", store.same(0x03, 1, 3) && store.same(0x03, 7, 8));
    // A layer made after the merges has the same classes as one made before.
    WritingStore late;
    late.merge(1, 2, 0);
    late.merge(2, 3, 0x01);
    late.find(0x01, 1);
    late.merge(3, 4, 0);
    r.check("exact_reaches_layers", late.same(0x01, 1, 4) && late.same(0, 3, 4));
    r.check("new_flag", !late.merge(1, 4, 0x01) && late.merge(10, 11, 0x01));
    WritingStore many;
    bool refused = false;
    try {
        for (uint8_t context : {0, 1, 2, 4, 8}) {
            many.find(context, 0);
        }
    } catch (const std::logic_error &) {
        refused = true;
    }
    r.check("layer_limit", refused);
}

void deep(Report &r) {
    WritingStore store;
    constexpr gc_node_ref_t n = 100000;
    for (gc_node_ref_t i = n; i > 1; --i) {
        store.merge(i, i - 1);
    }
    r.check("long_chain", store.find(0, n) == 1 && store.members(0, 1).size() == n);
}

} // namespace

const camel::runtime::Program *
WritingsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    Report report{os};
    unionFind(report);
    layers(report);
    deep(report);
    return program;
}
