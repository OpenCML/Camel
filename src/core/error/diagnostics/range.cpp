/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 06, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/diagnostics/range.h"

#include <algorithm>

namespace camel::core::error {

CharRange CharRange::fromTokens(antlr4::Token *start, antlr4::Token *end) {
    CharRange r{{0, 0}, {0, 0}};
    if (start) {
        r.start = {
            static_cast<size_t>(start->getLine() - 1),
            static_cast<size_t>(start->getCharPositionInLine())};
    }
    if (end) {
        r.end = {
            static_cast<size_t>(end->getLine() - 1),
            static_cast<size_t>(end->getCharPositionInLine() + end->getText().length())};
    } else if (start) {
        r.end = {
            static_cast<size_t>(start->getLine() - 1),
            static_cast<size_t>(start->getCharPositionInLine() + start->getText().length())};
    }
    return r;
}

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
