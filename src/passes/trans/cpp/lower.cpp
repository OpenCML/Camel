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
 * Created: Mar. 11, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "lower.h"

#include <array>

namespace {

using Spec = CppOperatorSpec;

constexpr std::array kOperatorSpecs = {
    Spec{":op/add_i", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_l", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_f", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/add_d", CppOperatorEmitKind::InlineBinary, 2, 0, "+"},
    Spec{":op/sub_i", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_l", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_f", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/sub_d", CppOperatorEmitKind::InlineBinary, 2, 0, "-"},
    Spec{":op/mul_i", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_l", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_f", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/mul_d", CppOperatorEmitKind::InlineBinary, 2, 0, "*"},
    Spec{":op/div_i", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_l", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_f", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/div_d", CppOperatorEmitKind::InlineBinary, 2, 0, "/"},
    Spec{":op/lt_i", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_l", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_f", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/lt_d", CppOperatorEmitKind::InlineBinary, 2, 0, "<"},
    Spec{":op/gt_i", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_l", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_f", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/gt_d", CppOperatorEmitKind::InlineBinary, 2, 0, ">"},
    Spec{":op/le_i", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_l", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_f", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/le_d", CppOperatorEmitKind::InlineBinary, 2, 0, "<="},
    Spec{":op/ge_i", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_l", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_f", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/ge_d", CppOperatorEmitKind::InlineBinary, 2, 0, ">="},
    Spec{":op/eq_i", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_l", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_f", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_d", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/eq_b", CppOperatorEmitKind::InlineBinary, 2, 0, "=="},
    Spec{":op/ne_i", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_l", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_f", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_d", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/ne_b", CppOperatorEmitKind::InlineBinary, 2, 0, "!="},
    Spec{":op/not", CppOperatorEmitKind::InlineUnary, 1, 0, "!"},
    Spec{":op/neg_i", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_l", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_f", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/neg_d", CppOperatorEmitKind::InlineUnary, 1, 0, "-"},
    Spec{":op/inv_i", CppOperatorEmitKind::InlineUnary, 1, 0, "~"},
    Spec{":op/inv_l", CppOperatorEmitKind::InlineUnary, 1, 0, "~"},
    Spec{":op/itoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtoi", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtol", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtof", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/itod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ltod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/ftod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
    Spec{":op/dtod", CppOperatorEmitKind::NumericCast, 1, 0, ""},
};

} // namespace

const CppOperatorSpec *findCppOperatorSpec(std::string_view uri) {
    for (const auto &spec : kOperatorSpecs) {
        if (spec.uri == uri) {
            return &spec;
        }
    }
    return nullptr;
}
