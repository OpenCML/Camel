// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * One table for the causes a transform refuses by, and the half of the reading a plan reads.
 *
 * A transform says what it built (or why it built nothing) on `readings()`, one line per step:
 * `REINTERP jvp function=f skip=body`. The line is for a reader, and it was also the only thing a
 * plan could assert, so the wording of a refusal became the criterion -- a word renamed in
 * `split.cpp` turned a green case red without anything about the transform changing (2026-11-04:
 * the transposed leg's `skip=` words). The same defect had already been fixed once on the other
 * side: `transpose` names its refusals with `TransposeRefusalCause` and spells them from one table
 * (`transpose.cpp#transposeRefusalWord`), so a rename is one edit and the caller reads the cause
 * instead of the text (`test/tools/transpose_refusal/check.py`).
 *
 * This is the same fact for the readings, and it adds the other half:
 *
 * * `RefusalCause` -- the causes a `REINTERP` line may refuse by, declared once;
 * * `refusalCauseName` -- the one place a cause is spelled, and therefore the only spelling the
 *   printed line has (`skip=` + this word);
 * * `RefusalReading` -- the cause *and the place it names* (the operator's URI, the rule, the callee,
 *   the construct) as a value, written by `noteRefusal` as one machine-readable line
 *   (`READING_REFUSAL {json}`, the shape `src/passes/trans/stats/stats.cpp` established with
 *   `GRAPH_STATS`).
 *
 * The printed line stays: it is what a person reads, and it carries the prose a value does not. The
 * record is what a plan asserts -- cause plus place, never the sentence -- so a case pins the
 * transform's decision and not its wording
 * (`test/plans/feat/compile/transforms.plan.toml#reading_refusal_is_one_cause_in_one_table`,
 * `test/tools/reading_refusal/check.py`).
 *
 * A record is written by the site that writes the line, from the same values. Nothing parses the
 * log back: the defect the transpose change removed was exactly that second reading
 * (`docs/handover/reviews/questions-2026-10-29.md`, A3).
 */

#pragma once

#include "readings.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace camel::transforms {

/// Why a transform built no graph. `None` is "no refusal recorded", and it has no word: a line that
/// names no cause must not be able to spell one.
enum class RefusalCause {
    None,
    Arm,
    Backward,
    Body,
    Branch,
    Callee,
    Exit,
    ExitType,
    Forward,
    Loop,
    Ports,
    Residual,
    Rule,
    Signature,
    Tangent,
    Tape,
    Value,
};

/// The log's spelling of a cause, and the only place any of these words is written: a site records
/// the cause and this spells it, so the line a reader sees and the record a plan reads cannot say
/// two things (`test/tools/reading_refusal/check.py` holds that as a relation over the sources).
inline const char *refusalCauseName(RefusalCause cause) {
    switch (cause) {
    case RefusalCause::None:
        return nullptr;
    case RefusalCause::Arm:
        return "arm";
    case RefusalCause::Backward:
        return "backward";
    case RefusalCause::Body:
        return "body";
    case RefusalCause::Branch:
        return "branch";
    case RefusalCause::Callee:
        return "callee";
    case RefusalCause::Exit:
        return "exit";
    case RefusalCause::ExitType:
        return "exit-type";
    case RefusalCause::Forward:
        return "forward";
    case RefusalCause::Loop:
        return "loop";
    case RefusalCause::Ports:
        return "ports";
    case RefusalCause::Residual:
        return "residual";
    case RefusalCause::Rule:
        return "rule";
    case RefusalCause::Signature:
        return "signature";
    case RefusalCause::Tangent:
        return "tangent";
    case RefusalCause::Tape:
        return "tape";
    case RefusalCause::Value:
        return "value";
    }
    return nullptr;
}

/// A refusal as a value: the cause, and the place of the program it names it at. The place fields
/// are the ones a `REINTERP` line states about a refusal -- the operator's URI, the rule's name, the
/// callee's name, the construct the plumbing could not copy -- and a site fills the ones its line
/// states. A reading that names a node or a parameter one day adds that field then: a field nothing
/// records is a promise nothing keeps.
struct RefusalReading {
    std::string_view transform; // "jvp" | "split" | "vmap"
    std::string_view function;
    RefusalCause cause = RefusalCause::None;
    std::string_view uri;
    std::string_view rule;
    std::string_view callee;
    std::string_view construct;

    RefusalReading() = default;
    /// A refusal of `cause` in `function`. The place it names -- if it names one -- is added with
    /// the `at*` chain below, so a site states exactly what its line states and no field is a
    /// positional default a reader has to count (`-Wextra` asks for every member otherwise, and a
    /// list of empty braces is not a reading).
    RefusalReading(std::string_view transform_, std::string_view function_, RefusalCause cause_)
        : transform(transform_), function(function_), cause(cause_) {}

    /// The operator the refusal names (`uri=:op/ltod`).
    RefusalReading &atUri(std::string_view value) {
        uri = value;
        return *this;
    }
    /// The rule the refusal names (`rule=matmul_t`).
    RefusalReading &atRule(std::string_view value) {
        rule = value;
        return *this;
    }
    /// The callee the refusal names (`callee=mk`).
    RefusalReading &atCallee(std::string_view value) {
        callee = value;
        return *this;
    }
    /// The construct the cut does not copy (`construct=a port`).
    RefusalReading &atConstruct(std::string_view value) {
        construct = value;
        return *this;
    }
};

namespace detail {

inline void appendJsonString(std::string &out, std::string_view text) {
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
}

inline void appendJsonMember(std::string &out, const char *name, std::string_view value) {
    out.push_back(',');
    appendJsonString(out, name);
    out.push_back(':');
    appendJsonString(out, value);
}

} // namespace detail

/// The machine-readable half of a refusal, on the diagnostic side and beside the line that says it
/// in words. One line per refusal, `READING_REFUSAL {json}`: a plan reads the cause and the place it
/// names out of this and never the sentence (`test/tools/reading-refusal.mjs`, the `expect_refusal`
/// assertions in the plans).
///
/// It always writes to `readings()` and not to the stream the caller handed the transform. A
/// transform's readings are the diagnostic channel even when a caller collects the line elsewhere
/// (`modules/transforms/module.cpp`'s `reinterp` pass hands `apply`'s stream down), so the record has
/// to be readable in one place; a plan reads stderr, and a record that followed the line to whatever
/// stream that caller had would be a record no plan can find.
inline void noteRefusal(const RefusalReading &reading) {
    std::string json = "{";
    json += "\"transform\":";
    detail::appendJsonString(json, reading.transform);
    json += ",\"function\":";
    detail::appendJsonString(json, reading.function);
    json += ",\"cause\":";
    const char *word = refusalCauseName(reading.cause);
    detail::appendJsonString(json, word != nullptr ? std::string_view(word) : std::string_view("none"));
    if (!reading.uri.empty()) {
        detail::appendJsonMember(json, "uri", reading.uri);
    }
    if (!reading.rule.empty()) {
        detail::appendJsonMember(json, "rule", reading.rule);
    }
    if (!reading.callee.empty()) {
        detail::appendJsonMember(json, "callee", reading.callee);
    }
    if (!reading.construct.empty()) {
        detail::appendJsonMember(json, "construct", reading.construct);
    }
    json += "}";
    readings() << "READING_REFUSAL " << json << '\n';
}

} // namespace camel::transforms