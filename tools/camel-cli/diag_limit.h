// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/userdef.h"

#include <ostream>
#include <span>
#include <vector>

/// What every handler of a diagnostics limit does after it has dumped the diagnostics it holds.
///
/// The one requirement is that a run which failed says why. A limit makes the containers give up
/// while the run is still going, so the handler catches the exception, dumps what the containers
/// hold, and then has to decide whether the reason still has to be said. It does not when the dump
/// already carried it -- the diagnostic the limit stopped on is the last one the container took,
/// and a dump that reached it has shown it in full -- and it does when the dump did not.
///
/// The question is therefore about one diagnostic, not about "does this container hold any error":
/// a container can hold errors that are not the one the limit stopped on, and then the answer
/// "already told" would be given for a diagnostic nobody has shown. The answer is read from the
/// very containers that were dumped, with `Diagnostics::holds`.
///
/// Where it is said: standard error, so that a diagnostics array on standard output stays an array.
/// How it is said: the same way the diagnostics of the dump were written -- under JSON one more
/// array of that form, so that the whole of standard error stays a valid JSON array, and under
/// text the rendered diagnostic, so that a reader sees the same shape twice.
namespace camel_cli {

/// A container that was dumped, with the token stream of the file its diagnostics were made in:
/// what `Diagnostics::holds` needs to place a diagnostic whose range is still a token range. The
/// token stream belongs to the module and outlives this record.
struct DumpedSink {
    const camel::core::error::Diagnostics *diags        = nullptr;
    const camel::core::error::TokenPositions *tokens    = nullptr;
};

inline void reportLimitLine(
    const camel::core::error::DiagnosticsLimitExceededBaseException &limit,
    std::span<const DumpedSink> dumped,
    std::ostream &err,
    bool json) {
    using namespace camel::core::error;
    const Diagnostic &reason = limit.diagnostic();
    for (const DumpedSink &sink : dumped) {
        if (sink.diags == nullptr) {
            continue;
        }
        if (sink.tokens != nullptr) {
            const RangeConverter converter(*sink.tokens);
            if (sink.diags->holds(reason, &converter)) {
                // The dump has just shown this diagnostic: the reason has been told, and this run
                // is not one that failed with nothing said.
                return;
            }
        } else if (sink.diags->holds(reason)) {
            return;
        }
    }
    if (json) {
        // An array holding one object: the dump above wrote its own array on the same stream, and
        // this one stays a valid JSON array too. A bare object would not, and a line of text
        // would make the whole of standard error unparseable.
        err << "[\n" << reason.toJson() << "\n]";
    } else {
        // The diagnostic's own text, as the warnings a run prints go to standard error: the same
        // destination decides the colours.
        err << reason.toText(err);
    }
    err << std::endl;
}

/// The sink of a container with no token stream of its own: the run-time sink, whose diagnostics
/// the builders place by span or origin id.
inline DumpedSink dumpedSink(const camel::core::error::Diagnostics *diags) {
    return DumpedSink{diags, nullptr};
}

/// The sinks of `modules`, in the order given: what a command dumps through
/// `Context::dumpModuleDiagnostics`, and therefore what it has to ask about the limit's diagnostic.
/// A module that is not written in Camel has no diagnostics of its own.
inline std::vector<DumpedSink>
moduleDiagSinks(const std::vector<camel::core::module::module_ptr_t> &modules) {
    std::vector<DumpedSink> sinks;
    for (const auto &module : modules) {
        auto userModule = std::dynamic_pointer_cast<camel::core::module::UserDefinedModule>(module);
        if (!userModule || !userModule->diagnostics()) {
            continue;
        }
        sinks.push_back(DumpedSink{userModule->diagnostics().get(),
                                   userModule->parser() ? &userModule->parser()->tokenPositions()
                                                        : nullptr});
    }
    return sinks;
}

} // namespace camel_cli