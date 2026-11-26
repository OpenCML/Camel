// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which law answered, as a value.
 *
 * `TRANSPOSE function=… rhs=add_t,mul_t` and `REINTERP jvp … rules=jvp_pow` say what a walk used,
 * but they are a sentence per member: a reader who wants the *set* of laws a run applied has to
 * split those lines, and a set read out of prose is a second reading of the log. So the same fact is
 * written once, when the law is applied, in a shape a program can read: one line per (member, law),
 *
 *     LAW_APPLIED {"transform":"transpose","function":"split_of_jvp_of_f_backward",
 *                  "law":"conv2d_t","uri":"nn:conv2d"}
 *
 * The fields:
 *
 *   transform  the walk that applied it -- `transpose`, `jvp` or `vmap`
 *   function   the member the law answered in, as the walks name it
 *   law        the law's own symbol, as `src/stdlib/rulesets/*.cml` declares it (`conv2d_t`)
 *   uri        the operator the step was asked about (`nn:conv2d`); a call-rule of `vmap` is asked
 *              by a head rather than a URI and states the head here
 *
 * It goes to `readings()` and therefore only out of a run that asked with `--trace transforms`: a
 * default run prints nothing new, so this record cannot disturb a program's own output or a plan
 * that pins it. It is not a refusal -- a refusal's reason is owed to a reader who never asked for a
 * trace, and this is the opposite: a reading of a run, wanted by whoever counts coverage.
 *
 * Nothing reads these lines inside the tree yet; the counter is `scripts/vjp-parity.mjs --walk`,
 * which runs the corpus once with the trace on and turns the set into the table
 * `docs/design/program-transforms/law-coverage.tsv`. That is why the line is one per application and
 * not a summary: the caller that cares is the one that keeps a set, and a walk that applied the same
 * law in two members must still say it applied it.
 */

#pragma once

#include "readings.h"

#include <string>
#include <string_view>

namespace camel::transforms {

/// One law application, as the reading states it. `uri` is empty for a step that has none.
struct LawReading {
    std::string_view transform; // "transpose" | "jvp" | "vmap"
    std::string_view function;
    std::string_view law;
    std::string_view uri;
};

/// Write `LAW_APPLIED {json}` for one application. The stream is `readings()`, so a run that did not
/// ask for `--trace transforms` writes it into the discarded stream and pays one format call.
inline void noteLawApplied(const LawReading &reading) {
    std::string json = "{";
    json += "\"transform\":";
    detail::appendJsonString(json, reading.transform);
    detail::appendJsonMember(json, "function", reading.function);
    detail::appendJsonMember(json, "law", reading.law);
    if (!reading.uri.empty()) {
        detail::appendJsonMember(json, "uri", reading.uri);
    }
    json += "}";
    readings() << "LAW_APPLIED " << json << '\n';
}

} // namespace camel::transforms