// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The instruction set a run may use (design principles 13.4).
 *
 * Which unit a kernel calls is run-time configuration, like where computation runs: one binary
 * carries a portable unit and the wider ones, and a run that must not use a wider unit says so on
 * its command line instead of moving to another machine. `--tensor-isa` is that configuration.
 * This is the one place it is stored: the driver sets it before anything runs, and the module
 * that has kernels per ISA reads it (the tensor module hides every detected feature from its
 * kernel selection).
 *
 * The text of `--tensor-isa`:
 *
 *   native   what the processor reports, which is what a run does with no option: the wider
 *            units run where both the CPU and the OS support them.
 *   generic  the portable units only: no detected feature is used, so a kernel computes without
 *            AVX2/FMA or AVX-512 whatever the processor has. The bits a kernel gives can differ
 *            between a portable and a wider unit (a fused multiply-add rounds once), so this is
 *            also how a run fixes the portable unit's bits on every machine (design principles
 *            7.1).
 *
 * Without the option there is no selection: the processor decides. With it the selection stays
 * for the rest of the process, because the kernels are chosen from it once.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace camel::execute {

/// What `--tensor-isa` selected.
enum class CpuIsa {
    /// The processor decides which unit a kernel calls.
    Native,
    /// The portable units only: no detected feature is used.
    Generic,
};

/// Reads the value of `--tensor-isa`: `native` or `generic`. Returns nullopt and sets `why` to a
/// sentence naming what is wrong when the text names neither.
std::optional<CpuIsa> parseCpuIsa(std::string_view text, std::string &why);

/// Applies `--tensor-isa`, before anything runs. The selection stays for the rest of the process.
void selectCpuIsa(CpuIsa isa);

/// The selection in effect; the default (no `--tensor-isa`) is `CpuIsa::Native`.
CpuIsa selectedCpuIsa();

} // namespace camel::execute