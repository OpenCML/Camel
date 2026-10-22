// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Warnings a pass raises about the program while it rewrites it.
 *
 * A pass that meets something worth telling the user, but no reason to stop (a constant
 * expression that can only fault, which fold leaves for the run), raises a warning here. The
 * warnings are kept apart from the run-time error sink: the pass driver hands them to the
 * installed handler once, right after the pass that raised them, and a later error of the run is
 * reported without them. A tool that installs no handler prints none.
 */

#pragma once

#include <string>
#include <vector>

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"

/**
 * Raises the warning `diag` (a RuntimeDiag of severity Warn whose message takes one argument,
 * `detail`) at `origin`, or without a position when `origin` is the invalid origin. The same
 * warning at the same place is raised once per pipeline: passes revisit a node (fold iterates,
 * simplify runs fold again every round) and copies of a node keep its origin.
 */
void raisePassWarning(
    camel::core::context::Context &ctx, camel::core::error::RuntimeDiag diag,
    camel::source::origin_id_t origin, const std::string &detail);

/// Receives the warnings one pass raised, in the order they were raised; `passName` is the pass
/// as written on the command line.
using PassWarningHandler = void (*)(
    const char *passName, const std::vector<camel::core::error::Diagnostic> &warnings,
    void *userData);

/// Install (or clear, with nullptr) the handler applyPassesDetailed calls after a pass that
/// raised warnings. Without a handler the warnings are dropped.
void setPassWarningHandler(PassWarningHandler handler, void *userData);
