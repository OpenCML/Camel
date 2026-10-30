// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The value of `--tensor-isa` (see camel/execute/cpu_isa.h), read and written by the driver.

#include "camel/execute/cpu_isa.h"

namespace camel::execute {
namespace {

/// The selection of the process. The driver sets it before the first kernel is chosen
/// (single-threaded); afterwards every module only reads it, so no lock is taken on the
/// execution path.
CpuIsa &selectionStorage() {
    static CpuIsa selection = CpuIsa::Native;
    return selection;
}

} // namespace

std::optional<CpuIsa> parseCpuIsa(std::string_view text, std::string &why) {
    if (text == "native") {
        return CpuIsa::Native;
    }
    if (text == "generic") {
        return CpuIsa::Generic;
    }
    why = "'" + std::string(text) + "' is not an instruction set; write native or generic";
    return std::nullopt;
}

void selectCpuIsa(CpuIsa isa) { selectionStorage() = isa; }

CpuIsa selectedCpuIsa() { return selectionStorage(); }

} // namespace camel::execute