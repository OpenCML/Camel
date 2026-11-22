// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/error/diagnostics/range.h"

#include <algorithm>

namespace camel::core::error {

// ---- RangeConverter implementation ----
CharRange RangeConverter::conv(const TokenRange &tr) const {
    size_t startIdx = tr.start;
    size_t endIdx   = tr.end;

    if (tokens_.size() == 0)
        return {{0, 0}, {0, 0}};
    if (startIdx >= endIdx)
        return {{0, 0}, {0, 0}};

    // The range is half-open: its last token is the one before `end`. (Taking tokens_[end]
    // made every range run on into the token after it, usually onto the next line.)
    if (startIdx >= tokens_.size())
        startIdx = tokens_.size() - 1;
    endIdx = std::max(std::min(endIdx, tokens_.size()) - 1, startIdx);

    return tokens_.charRange(startIdx, endIdx);
}

} // namespace camel::core::error
