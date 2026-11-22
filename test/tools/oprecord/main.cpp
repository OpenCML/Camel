// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * camel-oprecord: the operator record's own check, and the reading the `nodeMayFault` table is
 * compared with.
 *
 * `--self-check` walks the path a C++ registration takes and the readers it has:
 *
 *   intern -> a record nobody stated `panic` for (mayFault() is the conservative `true`)
 *   CppRegistration(uri, false) -> registerOperator -> find(uri)->mayFault() is false, and the
 *   record says the C++ entrance is the one that said so
 *   kernel(void *) -> Registry::kernel(id) hands back the same pointer
 *   the Camel entrance stating `panic = true` against that `false` -> refused, naming the item
 *   complete -> the epoch moves, and the published record refuses a later statement
 *   bind -> a key from outside the process is satisfied while the major version is equal
 *
 * `--table` reads `may_fault<TAB>uri` lines (what `camel inspect --ops` prints, one line per
 * operator) and answers, per operator, three readings: the table's `may_fault` the pass reads
 * today, the record's `mayFault()` when nobody has stated `panic` (the writer's gap), and the
 * record's `mayFault()` once the same value is stated through the declaration entrance (the
 * plumbing, which must reproduce the table row for row). It is the measurement behind "does the
 * record read the same `mayFault` as the table" and nothing in the tree reads it.
 */

#include "camel/core/operator_semantics.h"
#include "camel/ops/operator_record.h"

#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using camel::ops::CppRegistration;
using camel::ops::OperId;
using camel::ops::Record;
using camel::ops::Registry;

int failures = 0;

void check(bool ok, std::string_view what) {
    if (ok) {
        std::cout << "[PASS] " << what << "\n";
    } else {
        ++failures;
        std::cout << "[FAIL] " << what << "\n";
    }
}

/// Two objects whose addresses stand in for two kernels: a kernel is a pointer this process runs,
/// and the registry keeps it as one.
int kernelA = 1;
int kernelB = 2;

std::string uriOf(const Record *record) { return record == nullptr ? std::string() : record->uri(); }

/// A Camel declaration that states one item, which is what the first entrance hands `declare`.
Record statementOf(std::string_view uri, bool panic) {
    Record statement;
    statement.key         = camel::core::sem::OperatorKey::of(uri);
    statement.panic.value = panic;
    statement.panic.byCamel = true;
    return statement;
}

int selfCheck() {
    Registry &registry = Registry::instance();

    // ---- identity --------------------------------------------------------------------------
    const OperId copyId = registry.intern(":test/copy");
    check(
        copyId != camel::ops::kNoOper && registry.intern(":test/copy") == copyId,
        "intern answers one id for one URI, and the second intern answers the same id");

    const Record *copy = registry.find(copyId);
    check(
        copy != nullptr && uriOf(copy) == ":test/copy",
        "find(id) answers the record whose key is that URI");
    check(
        copy != nullptr && copy->mayFault() && !copy->panic.stated(),
        "a record nobody stated `panic` for reads `mayFault() == true`: nobody said, so a call may "
        "fail");

    // ---- the C++ entrance states `panic`, and a reader reads it ------------------------------
    CppRegistration cloner(":test/copy", false);
    cloner.pure().kernel(&kernelA);
    const OperId registered = registry.registerOperator(cloner);
    check(registered == copyId, "registerOperator answers the id the URI already had");
    check(registry.kernel(registered) == static_cast<void *>(&kernelA), "the kernel the "
        "registration handed over is the one Registry::kernel(id) hands back");

    copy = registry.find(":test/copy");
    check(
        copy != nullptr && copy->panic.stated() && copy->panic.byCpp && !copy->panic.byCamel,
        "the record remembers that the C++ entrance is the one that stated `panic`");
    check(
        copy != nullptr && !copy->mayFault(),
        "the registration's second argument reaches the record: `mayFault()` is false where it was "
        "the conservative true before");
    check(
        copy != nullptr && copy->pure() && copy->effect() == camel::core::sem::Effect::Pure,
        "`pure()` reaches the record: the effect is read as Pure from the empty `uses` clause");

    // ---- the other answer, and the two entrances disagreeing --------------------------------
    CppRegistration failing(":test/scale", true);
    failing.pure().kernel(&kernelB);
    const OperId failingId = registry.registerOperator(failing);
    check(
        registry.find(failingId) != nullptr && registry.find(failingId)->mayFault(),
        "a registration that states `panic = true` is read as one that may fail");
    check(
        registry.kernel(failingId) == static_cast<void *>(&kernelB),
        "two operators keep two kernels: the second registration's pointer is its own");

    bool refusedDisagreement = false;
    try {
        registry.declare(registered, statementOf(":test/copy", true));
    } catch (const std::invalid_argument &refusal) {
        refusedDisagreement =
            std::string_view(refusal.what()).find("panic") != std::string_view::npos;
    }
    check(
        refusedDisagreement,
        "a Camel declaration stating `panic = true` where the C++ entrance stated false is refused, "
        "naming the item");

    // ---- completion ------------------------------------------------------------------------
    const std::uint64_t before = registry.epoch();
    registry.complete(registered);
    check(registry.epoch() == before + 1, "complete publishes the record and moves the epoch");
    bool refusedAfterComplete = false;
    try {
        registry.declare(registered, statementOf(":test/copy", false));
    } catch (const std::invalid_argument &) {
        refusedAfterComplete = true;
    }
    check(refusedAfterComplete, "a completed record refuses a later statement");

    // ---- a key read back from outside the process ------------------------------------------
    // A cache written by an older process records the key it was computed for; this process
    // satisfies it while the major versions are equal and this process's minor is not lower.
    registry.intern(camel::core::sem::OperatorKey::of(":test/versioned", 1, 2));
    camel::core::sem::OperatorKey cached = camel::core::sem::OperatorKey::of(":test/versioned", 1, 0);
    check(
        registry.bind(cached).has_value(),
        "bind accepts a key of the same major version whose minor version this process has passed");
    cached.major = 2;
    check(
        !registry.bind(cached).has_value(),
        "bind refuses a key whose major version this process does not have");

    check(
        registry.find(":test/nobody") == nullptr && !registry.lookup(":test/nobody").has_value(),
        "an operator nobody interned is not found, by id or by URI");

    std::cout << "oprecord self-check: " << (failures == 0 ? "passed" : "failed") << "\n";
    return failures == 0 ? 0 : 1;
}

/// Splits "may_fault<TAB>uri".
bool splitRow(const std::string &line, bool &mayFault, std::string &uri) {
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) {
        return false;
    }
    const std::string_view left(line.data(), tab);
    if (left != "true" && left != "false") {
        return false;
    }
    mayFault = left == "true";
    uri.assign(line, tab + 1, std::string::npos);
    return !uri.empty();
}

int table() {
    Registry &registry = Registry::instance();
    std::string line;
    std::size_t total = 0;
    std::size_t same  = 0;
    std::size_t moved = 0;
    std::size_t unstatedDiffers = 0;
    std::set<std::string> seen;

    std::cout << "uri\ttable_may_fault\tunstated\tstated\tsame\n";
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        bool mayFault = false;
        std::string uri;
        if (!splitRow(line, mayFault, uri)) {
            std::cerr << "oprecord: not a \"may_fault<TAB>uri\" row: " << line << "\n";
            return 2;
        }
        ++total;
        const OperId id = registry.intern(uri);

        const Record *record = registry.find(id);
        if (record == nullptr) {
            std::cerr << "oprecord: intern did not answer a record for " << uri << "\n";
            return 2;
        }
        const bool unstated = record->mayFault();

        bool stated = unstated;
        if (seen.insert(uri).second) {
            registry.declare(id, statementOf(uri, mayFault));
            registry.complete(id);
            stated = registry.find(id)->mayFault();
        }

        const bool unchanged = stated == mayFault;
        same += unchanged ? 1 : 0;
        moved += unchanged ? 0 : 1;
        unstatedDiffers += unstated == mayFault ? 0 : 1;
        std::cout << uri << '\t' << (mayFault ? "true" : "false") << '\t'
                  << (unstated ? "true" : "false") << '\t' << (stated ? "true" : "false") << '\t'
                  << (unchanged ? "same" : "CHANGED") << '\n';
    }

    std::cout << "oprecord table: total=" << total << " same=" << same << " changed=" << moved
              << " unstated_differs=" << unstatedDiffers << "\n";
    return total > 0 ? 0 : 2;
}

} // namespace

int main(int argc, char **argv) {
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument = argv[i];
            if (argument == "--self-check") {
                return selfCheck();
            }
            if (argument == "--table") {
                return table();
            }
            std::cerr << "oprecord: unknown argument " << argument << "\n";
            return 2;
        }
        std::cerr << "usage: camel-oprecord --self-check | --table <may_fault<TAB>uri rows>\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "oprecord: " << error.what() << "\n";
        return 2;
    }
}