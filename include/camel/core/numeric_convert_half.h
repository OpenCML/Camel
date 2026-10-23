// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The written conversions into the two reduced-precision floating types, `bf16(x)` and `f16(x)`,
 * and the `as` form of the wider direction (a bf16 or f16 read as a wider number, which is exact;
 * camel/core/rtdata/half.h holds the bit-level rounding itself).
 *
 * A conversion into one of them rounds to nearest with ties to even exactly once -- from the value
 * the source type holds, brought through f32, which is the compute precision of the element type.
 * Writing the conversion is asking for that precision, so no numeric permission is involved
 * (design principles 7.2-7.3: `change_precision` governs a computation the compiler moves to lower
 * precision). A text is read first as one whole number (design principles 5.1) and then rounded,
 * and a text beyond what the element type can hold is the fault a text beyond the range of f32 is:
 * the range of the type is what the written value is measured against.
 */

#pragma once

#include "camel/core/numeric_convert.h"
#include "camel/core/rtdata/half.h"

#include <format>
#include <string>
#include <string_view>
#include <type_traits>

namespace camel::core {

/// A text read as a bf16 or f16 (`target` is the name in the message): the whole text must be one
/// number, and one beyond the range of the element type is ConversionOutOfRange.
template <typename Half> slot_t halfFromText(std::string_view text, const char *target) {
    const float asFloat = camel::core::floatFromText<float>(text, target);
    if (camel::core::rtdata::halfOverflows<Half>(asFloat)) {
        error::throwRuntimeFault(
            error::RuntimeDiag::ConversionOutOfRange,
            "'" + std::string(text) + "'",
            target,
            std::format(
                "the value is outside the range of {} ({})",
                target,
                camel::utils::halfRangeText(std::is_same_v<Half, camel::core::rtdata::BFloat16>)));
    }
    return camel::core::rtdata::halfFromFloat<Half>(asFloat);
}

} // namespace camel::core