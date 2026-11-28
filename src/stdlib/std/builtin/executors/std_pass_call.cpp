// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "builtin/executors/std_pass_call.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"

using namespace camel::core::error;
using namespace camel::core::context;

namespace {

/// The pass-as-function call path is not implemented yet
/// (internal/plans/2026-11-26-pass-as-function, step 3): a program that names one of the C++
/// optimization passes now reaches the declaration, the type checker accepts the call and the graph
/// holds an operator node, so the one thing missing is the path that runs a pass from a function
/// value. It says so, naming the pass it was asked for, and throws. It never returns a value: a
/// call that is resolved but not implemented must not be mistaken for a pass that ran and did
/// nothing.
[[noreturn]] void passCallNotImplemented(const char *name) {
    throw DiagnosticBuilder::of(RuntimeDiag::PassInternalError)
        .commit(name, "the pass-as-function call path is not implemented yet (plan step 3)");
}

slot_t __pass_inline__(ArgsView &, ArgsView &, Context &) {
    passCallNotImplemented("std::cxx::inline");
}
slot_t __pass_fold__(ArgsView &, ArgsView &, Context &) {
    passCallNotImplemented("std::cxx::fold");
}
slot_t __pass_algebra__(ArgsView &, ArgsView &, Context &) {
    passCallNotImplemented("std::cxx::algebra");
}
slot_t __pass_saturate__(ArgsView &, ArgsView &, Context &) {
    passCallNotImplemented("std::cxx::saturate");
}
slot_t __pass_cse__(ArgsView &, ArgsView &, Context &) { passCallNotImplemented("std::cxx::cse"); }
slot_t __pass_dce__(ArgsView &, ArgsView &, Context &) { passCallNotImplemented("std::cxx::dce"); }
slot_t __pass_order__(ArgsView &, ArgsView &, Context &) {
    passCallNotImplemented("std::cxx::order");
}

/// The seven URIs, in one place: the map below and the traits both read it, so the two cannot name
/// different passes. These are the operators' URIs (`#op('std:cxx::cse')`), not the source names a
/// pipeline writes (`std::cxx::cse`); the declaration is what connects the two.
constexpr const char *kPassCallUris[] = {
    "std:cxx::inline",
    "std:cxx::fold",
    "std:cxx::algebra",
    "std:cxx::saturate",
    "std:cxx::cse",
    "std:cxx::dce",
    "std:cxx::order",
};

} // namespace

const std::unordered_map<std::string, operator_t> &getStdProtocolOpsMap() {
    // The executor is handed the operator after the protocol's colon: for `std:cxx::cse` that is
    // `cxx::cse` (ExecutorManager::find strips the protocol and one character).
    static const std::unordered_map<std::string, operator_t> map = {
        {"cxx::inline", __pass_inline__},
        {"cxx::fold", __pass_fold__},
        {"cxx::algebra", __pass_algebra__},
        {"cxx::saturate", __pass_saturate__},
        {"cxx::cse", __pass_cse__},
        {"cxx::dce", __pass_dce__},
        {"cxx::order", __pass_order__},
    };
    return map;
}

void declareStdPassCallTraits() {
    auto &table = camel::core::OperatorTable::instance();
    for (const char *uri : kPassCallUris) {
        table.set(uri, {.pure = true});
    }
}