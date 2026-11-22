// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file checkreport.cpp
 * @brief The fixed rendering of the table of a check (checkreport.h holds what a row is and why).
 */

#include "checkreport.h"

namespace camel::rewritemod {

std::string CheckRow::render() const {
    std::string line = verdict + " " + qualified() + ": proposition " + proposition;
    line += ", evidence " + evidence;
    if (!reason.empty()) {
        line += ", reason " + reason;
    }
    if (!detail.empty()) {
        line += ", " + detail;
    }
    if (!coverage.empty()) {
        line += ", coverage " + coverage;
    }
    line += "\n";
    return line;
}

std::string renderRows(const std::vector<CheckRow> &rows) {
    std::string text;
    for (const CheckRow &row : rows) {
        text += row.render();
    }
    return text;
}

} // namespace camel::rewritemod
