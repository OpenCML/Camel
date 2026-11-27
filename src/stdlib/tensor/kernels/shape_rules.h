// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The shape rules a rank-1 span carries against a value of rank 3 or more, written once.
 *
 * `sum_to` takes its target as the extents themselves, and `broadcast` spreads an operand over a
 * shape; both are right-aligned against the other shape (the suffix reading), and both meet the
 * same question: a span of one extent does not say *which* axis it names when the other shape
 * holds that extent on more than one axis. `[4]` on `[4, 4, 2, 4]` is the last axis by the suffix
 * reading and axis 0 by another, and the numbers differ. The question is answered here so that
 * the kernel and the declaration (`index_semantics`' instance for the operator) cannot drift
 * apart: the kernel throws the sentence, the declaration answers "no instance" from the same
 * result.
 *
 * The ambiguity is not every rank-1 span. A span of more than one extent keeps the suffix
 * reading; an extent of 1 is a lane every axis can take; and a rank-2 value's last axis is its
 * channel axis, which is what a rank-1 span means there, so the rule starts at rank 3.
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace camel::tensor {

/**
 * The axes of `shape` that hold `span`'s one extent, joined for the refusal sentence ("0, 2 or
 * 3"), or an empty string when there is no ambiguity: `span` is not of one extent, that extent is
 * 1, `shape` has fewer than three axes, or the extent sits on one axis only.
 */
inline std::string ambiguousRankOneAxes(std::span<const int64_t> span, std::span<const int64_t> shape) {
    if (span.size() != 1 || span[0] == 1 || shape.size() < 3) {
        return std::string();
    }
    std::string fits;
    size_t candidates = 0;
    for (size_t d = 0; d < shape.size(); ++d) {
        if (shape[d] != span[0]) {
            continue;
        }
        if (candidates != 0) {
            fits += (candidates + 1 == shape.size()) ? " or " : ", ";
        }
        fits += std::to_string(d);
        ++candidates;
    }
    return candidates > 1 ? fits : std::string();
}

} // namespace camel::tensor