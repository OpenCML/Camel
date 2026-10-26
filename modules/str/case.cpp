// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "case_tables.h"
#include "camel/utils/utf8.h"
#include <algorithm>

namespace camel::str {

namespace {

std::string convert(
    std::string_view text, const CaseEntry *table, size_t count, char asciiFrom, char asciiTo) {
    std::string out(text);
    if (camel::utf8::isAscii(text)) {
        // The common case: only a..z (or A..Z) change, one byte to one byte.
        for (char &c : out) {
            if (c >= asciiFrom && c <= asciiTo)
                c = static_cast<char>(c ^ 0x20);
        }
        return out;
    }
    out.clear();
    out.reserve(text.size());
    const uint32_t *sequences = caseSequences();
    size_t pos                = 0;
    while (pos < text.size()) {
        const uint32_t cp = camel::utf8::decodeOne(text, pos);
        const CaseEntry *end = table + count;
        const CaseEntry *at  = std::lower_bound(
            table, end, cp, [](const CaseEntry &e, uint32_t v) { return e.cp < v; });
        if (at == end || at->cp != cp) {
            camel::utf8::encodeOne(cp, out);
        } else if ((at->to & 0x80000000u) == 0) {
            camel::utf8::encodeOne(at->to, out);
        } else {
            const size_t length = (at->to >> 24) & 0x7;
            const size_t offset = at->to & 0xFFFFFF;
            for (size_t i = 0; i < length; ++i)
                camel::utf8::encodeOne(sequences[offset + i], out);
        }
    }
    return out;
}

} // namespace

std::string toUpperCase(std::string_view text) {
    size_t count = 0;
    const CaseEntry *table = upperTable(count);
    return convert(text, table, count, 'a', 'z');
}

std::string toLowerCase(std::string_view text) {
    size_t count = 0;
    const CaseEntry *table = lowerTable(count);
    return convert(text, table, count, 'A', 'Z');
}

} // namespace camel::str
