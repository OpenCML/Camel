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
 * Created: Sep. 09, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"
#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/utils/log.h"

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

#include "../operators/cast.h"
#include "../operators/io.h"
#include "../operators/macro.h"
#include "../operators/ops.h"
#include "../operators/other.h"
#include "../operators/profiler.h"
#include "../operators/str.h"
#include "../operators/struct.h"

const std::unordered_map<std::string, operator_t> &getOpsImplMap() {
    static const std::unordered_map<std::string, operator_t> map = {
        {"not-impl", __not_implemented__},

        // Type conversions
        {"op/itoi", __itoi__},
        {"op/ltoi", __ltoi__},
        {"op/ftoi", __ftoi__},
        {"op/dtoi", __dtoi__},
        {"op/stoi", __stoi__},

        {"op/itol", __itol__},
        {"op/ltol", __ltol__},
        {"op/ftol", __ftol__},
        {"op/dtol", __dtol__},
        {"op/stol", __stol__},

        {"op/itof", __itof__},
        {"op/ltof", __ltof__},
        {"op/ftof", __ftof__},
        {"op/dtof", __dtof__},
        {"op/stof", __stof__},

        {"op/itod", __itod__},
        {"op/ltod", __ltod__},
        {"op/ftod", __ftod__},
        {"op/dtod", __dtod__},
        {"op/stod", __stod__},

        {"op/itos", __itos__},
        {"op/ltos", __ltos__},
        {"op/ftos", __ftos__},
        {"op/dtos", __dtos__},
        {"op/stos", __stos__},
        {"op/atos", __atos__},

        // ops
        {"op/assn_i", __builtin__assn__},
        {"op/assn_l", __builtin__assn__},
        {"op/assn_f", __builtin__assn__},
        {"op/assn_d", __builtin__assn__},
        {"op/assn_s", __builtin__assn__},
        {"op/assn_b", __builtin__assn__},
        {"op/assn", __builtin__assn__},

        {"op/assn_add_i", __builtin__assn_add_i__},
        {"op/assn_add_l", __builtin__assn_add_l__},
        {"op/assn_add_f", __builtin__assn_add_f__},
        {"op/assn_add_d", __builtin__assn_add_d__},
        {"op/assn_add_s", __not_implemented__},
        {"op/assn_add", __not_implemented__},

        {"op/assn_sub_i", __builtin__assn_sub_i__},
        {"op/assn_sub_l", __builtin__assn_sub_l__},
        {"op/assn_sub_f", __builtin__assn_sub_f__},
        {"op/assn_sub_d", __builtin__assn_sub_d__},
        {"op/assn_sub", __not_implemented__},

        {"op/assn_mul_i", __builtin__assn_mul_i__},
        {"op/assn_mul_l", __builtin__assn_mul_l__},
        {"op/assn_mul_f", __builtin__assn_mul_f__},
        {"op/assn_mul_d", __builtin__assn_mul_d__},
        {"op/assn_mul", __not_implemented__},

        {"op/assn_div_i", __builtin__assn_div_i__},
        {"op/assn_div_l", __builtin__assn_div_l__},
        {"op/assn_div_f", __builtin__assn_div_f__},
        {"op/assn_div_d", __builtin__assn_div_d__},
        {"op/assn_div", __not_implemented__},

        {"op/assn_mod_i", __builtin__assn_mod__},
        {"op/assn_mod_l", __builtin__assn_mod__},
        {"op/assn_mod", __builtin__assn_mod__},

        {"op/assn_mat", __builtin__assn_mat__},

        {"op/assn_pow", __builtin__assn_pow__},

        {"op/assn_and", __builtin__assn_and__},
        {"op/assn_or", __builtin__assn_or__},

        {"op/and", __builtin__and__},
        {"op/or", __builtin__or__},

        {"op/eq_i", __builtin__eq__},
        {"op/eq_l", __builtin__eq__},
        {"op/eq_f", __builtin__eq__},
        {"op/eq_d", __builtin__eq__},
        {"op/eq_b", __builtin__eq__},
        {"op/eq_s", __builtin__eq__},
        {"op/eq", __builtin__eq__},

        {"op/ne_i", __builtin__ne__},
        {"op/ne_l", __builtin__ne__},
        {"op/ne_f", __builtin__ne__},
        {"op/ne_d", __builtin__ne__},
        {"op/ne_b", __builtin__ne__},
        {"op/ne_s", __builtin__ne__},
        {"op/ne", __builtin__ne__},

        {"op/strict_eq", __builtin__strict_eq__},
        {"op/strict_ne", __builtin__strict_ne__},

        {"op/lt_i", __builtin__lt__<Int32>},
        {"op/lt_l", __builtin__lt__<Int64>},
        {"op/lt_f", __builtin__lt__<Float32>},
        {"op/lt_d", __builtin__lt__<Float64>},
        {"op/lt_s", __not_implemented__},
        {"op/lt", __not_implemented__},

        {"op/le_i", __builtin__le__<Int32>},
        {"op/le_l", __builtin__le__<Int64>},
        {"op/le_f", __builtin__le__<Float32>},
        {"op/le_d", __builtin__le__<Float64>},
        {"op/le_s", __not_implemented__},
        {"op/le", __not_implemented__},

        {"op/gt_i", __builtin__gt__<Int32>},
        {"op/gt_l", __builtin__gt__<Int64>},
        {"op/gt_f", __builtin__gt__<Float32>},
        {"op/gt_d", __builtin__gt__<Float64>},
        {"op/gt_s", __not_implemented__},
        {"op/gt", __not_implemented__},

        {"op/ge_i", __builtin__ge__<Int32>},
        {"op/ge_l", __builtin__ge__<Int64>},
        {"op/ge_f", __builtin__ge__<Float32>},
        {"op/ge_d", __builtin__ge__<Float64>},
        {"op/ge_s", __not_implemented__},
        {"op/ge", __not_implemented__},

        {"op/add_i", __builtin__add_ii__},
        {"op/add_l", __builtin__add_ll__},
        {"op/add_f", __builtin__add_ff__},
        {"op/add_d", __builtin__add_dd__},
        {"op/add_s", __builtin__add_ss__},

        {"op/sub_i", __builtin__sub_ii__},
        {"op/sub_l", __builtin__sub_ll__},
        {"op/sub_f", __builtin__sub_ff__},
        {"op/sub_d", __builtin__sub_dd__},

        {"op/mul_i", __builtin__mul__},
        {"op/mul_l", __builtin__mul__},
        {"op/mul_f", __builtin__mul__},
        {"op/mul_d", __builtin__mul__},
        {"op/mul", __builtin__mul__},

        {"op/div_i", __builtin__div__},
        {"op/div_l", __builtin__div__},
        {"op/div_f", __builtin__div__},
        {"op/div_d", __builtin__div__},
        {"op/div", __builtin__div__},

        {"op/mod_i", __builtin__mod__},
        {"op/mod_l", __builtin__mod__},
        {"op/mod", __builtin__mod__},

        {"op/pow_i", __builtin__pow__},
        {"op/pow_l", __builtin__pow__},
        {"op/pow_f", __builtin__pow__},
        {"op/pow_d", __builtin__pow__},
        {"op/pow", __builtin__pow__},

        {"op/mat", __builtin__mat__},

        {"op/idx_arr", __builtin__idx__},
        {"op/idx_str", __builtin__idx__},

        {"op/not", __builtin__not__},

        {"op/neg_i", __builtin__neg__},
        {"op/neg_l", __builtin__neg__},
        {"op/neg_f", __builtin__neg__},
        {"op/neg_d", __builtin__neg__},
        {"op/neg", __builtin__neg__},

        {"op/inv_i", __builtin__inv__},
        {"op/inv_l", __builtin__inv__},
        {"op/inv", __builtin__inv__},

        // IO (built-in input/print/println)
        {"io/input", __op_input__},
        {"io/print", __op_print__},
        {"io/println", __op_println__},

        // string
        {"str/format", __format__},

        {"str/join", __join__},

        // struct
        {"struct/len_str", __len_str__},
        {"struct/len_arr", __len_arr__},

        {"struct/zip", __zip__},

        {"struct/head_arr", __head_arr__},

        {"struct/tail_arr", __tail_arr__},

        {"struct/range", __range__},

        {"struct/slice_arr", __slice_arr__},

        {"struct/concat_arr", __concat_arr__},

        {"struct/append_arr", __append_arr__},

        {"struct/extend_arr", __extend_arr__},

        {"struct/contains_str", __not_implemented__},
        {"struct/contains_arr", __contains_arr__},

        // profiler
        {"profiler/begin", __profiler_begin__},
        {"profiler/end", __profiler_end__},
        {"profiler/instant", __profiler_instant__},
        {"profiler/enable", __profiler_enable__},

        // macro
        {"macro/cmp", __cmp__},
    };
    return map;
}

BasicBuiltinExecutor::BasicBuiltinExecutor(context_ptr_t ctx) : Executor(ctx, getOpsImplMap()) {};

executor_ptr_t BasicBuiltinExecutor::create(context_ptr_t ctx) {
    return std::make_shared<BasicBuiltinExecutor>(ctx);
}

void BasicBuiltinExecutor::eval(
    std::string uri, GIR::Node *self, camel::core::context::Frame &frame) {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("BasicExec", "Evaluating operator of URI: {}", uri));
    auto it = opsMap_.find(uri);
    if (it == opsMap_.end()) {
        throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
    }
    std::vector<GIR::data_idx_t> normIndices;
    for (const auto &in : self->normInputs()) {
        normIndices.push_back(in->index());
    }
    std::vector<GIR::data_idx_t> withIndices;
    for (const auto &in : self->withInputs()) {
        withIndices.push_back(in->index());
    }

    data_arr_t nargs = data_arr_t{
        normIndices.data(),
        static_cast<GIR::arr_size_t>(normIndices.size()),
    };
    data_arr_t wargs = data_arr_t{
        withIndices.data(),
        static_cast<GIR::arr_size_t>(withIndices.size()),
    };

    FrameArgsView withView(frame, wargs);
    FrameArgsView normView(frame, nargs);
    slot_t result = it->second(withView, normView, *context_);
    frame.set(self->index(), result);
};
