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
 * Updated: Sep. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "range.h"

CharRange CharRange::fromTokens(antlr4::Token *start, antlr4::Token *end) {
    CharRange r{{0, 0}, {0, 0}};
    if (start) {
        r.start = {
            static_cast<size_t>(start->getLine() - 1),
            static_cast<size_t>(start->getCharPosInLine())};
    }
    if (end) {
        r.end = {
            static_cast<size_t>(end->getLine() - 1),
            static_cast<size_t>(end->getCharPosInLine() + end->getText().length())};
    } else if (start) {
        r.end = {
            static_cast<size_t>(start->getLine() - 1),
            static_cast<size_t>(start->getCharPosInLine() + start->getText().length())};
    }
    return r;
}

// ---- RangeConverter implementation ----
CharRange RangeConverter::conv(const TokenRange &tr) const {
    size_t startIdx = tr.start;
    size_t endIdx = tr.end;

    if (tokens_.empty())
        return {{0, 0}, {0, 0}};
    if (startIdx >= endIdx)
        return {{0, 0}, {0, 0}};

    if (startIdx >= tokens_.size())
        startIdx = tokens_.size() - 1;
    if (endIdx >= tokens_.size())
        endIdx = tokens_.size() - 1;

    return CharRange::fromTokens(tokens_[startIdx], tokens_[endIdx]);
}