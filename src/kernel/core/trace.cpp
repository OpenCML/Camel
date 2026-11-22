// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/trace.h"

#include <iostream>
#include <streambuf>

/// The switch, the registry and the sink. One translation unit, so that `camel.exe` setting an area
/// and `libcamel` reading it are the same variable: an inline `static` in the header would give each
/// module its own copy and the command line would move nothing.
namespace camel::core {
namespace {

const TraceAreaEntry kTraceAreas[] = {
    {"transforms", TraceArea::Transforms},
    {"plan", TraceArea::Plan},
    {"effects", TraceArea::Effects},
    {"fold", TraceArea::Fold},
};

unsigned gTraceMask = 0;

/// Discards everything written to it: where a reading goes when the run did not ask for that area.
class Discard : public std::streambuf {
  protected:
    int overflow(int ch) override { return traits_type::not_eof(ch); }
};

Discard &discard() {
    static Discard sink;
    return sink;
}

/// The stream a reading goes to when the run did not ask: it swallows everything.
std::ostream &discarded() {
    static std::ostream stream(&discard());
    return stream;
}

} // namespace

const TraceAreaEntry *traceAreas(std::size_t &count) {
    count = sizeof(kTraceAreas) / sizeof(kTraceAreas[0]);
    return kTraceAreas;
}

bool traceEnabled(TraceArea area) {
    return (gTraceMask & (1u << static_cast<unsigned>(area))) != 0;
}

std::string traceAreaList() {
    std::size_t count             = 0;
    const TraceAreaEntry *entries = traceAreas(count);
    std::string list;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            list += ", ";
        }
        list += entries[i].name;
    }
    return list;
}

std::string enableTraceAreas(std::string_view spec) {
    std::size_t count             = 0;
    const TraceAreaEntry *entries = traceAreas(count);
    std::size_t begin             = 0;
    while (begin <= spec.size()) {
        std::size_t end = spec.find(',', begin);
        if (end == std::string_view::npos) {
            end = spec.size();
        }
        std::string_view name = spec.substr(begin, end - begin);
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
            name.remove_prefix(1);
        }
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
            name.remove_suffix(1);
        }
        if (!name.empty()) {
            bool known = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (name == entries[i].name) {
                    gTraceMask |= 1u << static_cast<unsigned>(entries[i].area);
                    known = true;
                    break;
                }
            }
            if (!known) {
                return std::string(name);
            }
        }
        if (end == spec.size()) {
            break;
        }
        begin = end + 1;
    }
    return std::string();
}

std::ostream &trace(TraceArea area) {
    return traceEnabled(area) ? std::cerr : discarded();
}

} // namespace camel::core