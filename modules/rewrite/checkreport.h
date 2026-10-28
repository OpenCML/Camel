// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file checkreport.h
 * @brief What `check` answers: the verdict table (docs/design/rewrite/plan-verification.cn.md,
 * S0; the rule and on
 * `verification-licence-to-precondition`).
 *
 * The verdict vocabulary is closed and has three words -- `proved`, `refuted`, `sampled` -- and
 * every line of the report wears one of them as its first word. The fourth word the check used to
 * write, `unchecked`, is not a verdict: a rule nothing could be examined on is `sampled` with the
 * reason written in its own column, kept apart from the `sampled` that says samples were drawn
 * (`verification-licence-to-precondition`, licence 3, option A: `sampled` and "not examined (why)"
 * are written separately and never mixed).
 *
 * Each row carries the five things the rule asked a row to carry, plus the three a reader needs to
 * reproduce it: the `rule`, the `instance` (a generic law's type tuple, empty when the rule is not
 * generic), the `verdict`, the `proposition` (what the verdict is about: `equality`,
 * `real equality + IEEE on special values` or `error bound`), the `evidence` (`solver`,
 * `samples <n>`, `counterexample` or `none`), the `reason` (why nothing was examined, or why a
 * rule that is not a counterexample is still `refuted`), the `detail` (the rest of the line:
 * how often the engine fired, the worst error ratio, the values a counterexample has), and the
 * `coverage`.
 *
 * The record is declared here and not in module.cpp so the format has one owner: the module builds
 * it and `checkreport.cpp` prints it.
 */

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace camel::rewritemod {

/// The proposition kinds a verdict can be about: what the rule says about its two sides.
inline constexpr const char *kPropositionEquality = "equality";
inline constexpr const char *kPropositionRealEquality =
    "real equality + IEEE on special values";
inline constexpr const char *kPropositionErrorBound = "error bound";

/// The verdict words. The first word of a row is one of these and nothing else.
inline constexpr const char *kVerdictProved  = "proved";
inline constexpr const char *kVerdictRefuted = "refuted";
inline constexpr const char *kVerdictSampled = "sampled";

/// One row of the table: `rule`, `instance`, `verdict`, `proposition`, `evidence`, `reason`, the
/// free `detail` the fixed rendering puts last, and the `coverage` of the translation.
struct CheckRow {
    std::string rule;
    std::string instance;
    std::string verdict;
    std::string proposition;
    std::string evidence;
    std::string reason;
    std::string detail;
    std::string coverage;

    /// The rule as the report names it: `name` for a rule of its own, `name<types>` for one
    /// instance of a generic law.
    std::string qualified() const {
        return instance.empty() ? rule : rule + "<" + instance + ">";
    }
    /// The fixed rendering of this row, ending in a newline. This text is the acceptance surface:
    /// a reader is meant to read the verdict, the proposition and the evidence off it, and a test
    /// is meant to assert against it.
    std::string render() const;
};

/// The whole table, one row per line.
std::string renderRows(const std::vector<CheckRow> &rows);

} // namespace camel::rewritemod