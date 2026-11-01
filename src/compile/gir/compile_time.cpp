// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The table of value-level transforms the front end answers while it builds (compile_time.h).
 *
 * The table is process-wide and a module fills it when it loads, which is before any program is
 * built: a program never registers a transform, and a module's entry is its own.
 */

#include "camel/compile/gir/compile_time.h"

#include <map>

namespace camel::compile::gir {

namespace {

std::map<std::string, CompileTimeTransform, std::less<>> &transforms() {
    static std::map<std::string, CompileTimeTransform, std::less<>> table;
    return table;
}

size_t instantiated = 0;

} // namespace

void registerCompileTimeTransform(std::string uri, CompileTimeTransform transform) {
    GUARD(!uri.empty(), "A compile-time transform needs the operator it answers.");
    GUARD(transform != nullptr, "A compile-time transform needs something to run.");
    auto &table = transforms();
    GUARD(
        table.find(uri) == table.end(),
        "A compile-time transform is already registered for this operator.");
    table.emplace(std::move(uri), std::move(transform));
}

const CompileTimeTransform *compileTimeTransformOf(std::string_view uri) {
    const auto &table = transforms();
    const auto found  = table.find(uri);
    return found == table.end() ? nullptr : &found->second;
}

void resetCompileTimeInstantiations() { instantiated = 0; }

size_t compileTimeInstantiationCount() { return instantiated; }

} // namespace camel::compile::gir