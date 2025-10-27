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
 * Updated: Oct. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "builtin.h"
#include "compile/gir.h"
#include "utils/log.h"

#include "../operators/io.h"
#include "../operators/macro.h"
#include "../operators/math.h"
#include "../operators/ops.h"
#include "../operators/os.h"
#include "../operators/other.h"
#include "../operators/profiler.h"
#include "../operators/str.h"
#include "../operators/struct.h"
#include "../operators/tensor.h"
#include "../operators/this.h"
#include "../operators/time.h"

const std::unordered_map<std::string, operator_t> &getOpsImplMap() {
    static const std::unordered_map<std::string, operator_t> map = {
        {"not-impl", __not_implemented__},

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

        {"op/lt_i", __builtin__lt__},
        {"op/lt_l", __builtin__lt__},
        {"op/lt_f", __builtin__lt__},
        {"op/lt_d", __builtin__lt__},
        {"op/lt_s", __builtin__lt__},
        {"op/lt", __builtin__lt__},

        {"op/le_i", __builtin__le__},
        {"op/le_l", __builtin__le__},
        {"op/le_f", __builtin__le__},
        {"op/le_d", __builtin__le__},
        {"op/le_s", __builtin__le__},
        {"op/le", __builtin__le__},

        {"op/gt_i", __builtin__gt__},
        {"op/gt_l", __builtin__gt__},
        {"op/gt_f", __builtin__gt__},
        {"op/gt_d", __builtin__gt__},
        {"op/gt_s", __builtin__gt__},
        {"op/gt", __builtin__gt__},

        {"op/ge_i", __builtin__ge__},
        {"op/ge_l", __builtin__ge__},
        {"op/ge_f", __builtin__ge__},
        {"op/ge_d", __builtin__ge__},
        {"op/ge_s", __builtin__ge__},
        {"op/ge", __builtin__ge__},

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

        {"io/input", __input__},
        {"io/print", __print__},
        {"io/println", __println__},

        // tensor
        {"tensor/eye", __eye__},
        {"tensor/zeros", __zeros__},
        {"tensor/ones", __ones__},
        {"tensor/diag", __diag__},
        {"tensor/linspace", __linspace__},
        {"tensor/arange", __arange__},
        {"tensor/shape", __shape__},

        {"tensor/add", __tensor_add__},
        {"tensor/subtract", __tensor_subtract__},
        {"tensor/multiply", __tensor_multiply__},
        {"tensor/matmul", __tensor_matmul__},
        {"tensor/divide", __tensor_divide__},

        {"tensor/reshape", __tensor_reshape__},
        {"tensor/transpose", __tensor_transpose__},
        {"tensor/flatten", __tensor_flatten__},

        {"tensor/concat", __tensor_concat__},
        {"tensor/stack", __tensor_stack__},

        {"tensor/sum", __tensor_sum__},
        {"tensor/mean", __tensor_mean__},
        {"tensor/min", __tensor_min__},
        {"tensor/max", __tensor_max__},
        {"tensor/argmin", __tensor_argmin__},
        {"tensor/argmax", __tensor_argmax__},
        {"tensor/std", __tensor_std__},

        {"tensor/norm_l1", __tensor_norm_l1__},
        {"tensor/norm_l2", __tensor_norm_l2__},
        {"tensor/norm_squared_l2", __tensor_norm_squared_l2__},

        {"tensor/sin", __tensor_sin__},
        {"tensor/cos", __tensor_cos__},
        {"tensor/exp", __tensor_exp__},
        {"tensor/log", __tensor_log__},
        {"tensor/sqrt", __tensor_sqrt__},
        {"tensor/matpow", __tensor_matpow__},
        {"tensor/pow", __tensor_pow__},

        {"tensor/sinh", __tensor_sinh__},
        {"tensor/cosh", __tensor_cosh__},
        {"tensor/tanh", __tensor_tanh__},

        {"tensor/show", __tensor_show__},

        // os
        {"os/exit", __exit__},
        {"os/sleep", __sleep__},
        {"os/whoami", __whoami__},
        {"os/set_terminal_raw_mode", __set_terminal_raw_mode__},
        {"os/has_input", __has_input__},
        {"os/get_char", __get_char__},
        {"os/get_chars", __get_chars__},
        {"os/clear_input_buffer", __clear_input_buffer__},

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

        // math
        {"math/abs_i", __abs__},
        {"math/abs_l", __abs__},
        {"math/abs_f", __abs__},
        {"math/abs_d", __abs__},

        {"math/exp_f", __exp__},
        {"math/exp_d", __exp__},

        {"math/round_f", __round__},
        {"math/round_d", __round__},

        {"math/ceil_f", __ceil__},
        {"math/ceil_d", __ceil__},

        {"math/floor_f", __floor__},
        {"math/floor_d", __floor__},

        {"math/bin_i", __bin__},
        {"math/bin_l", __bin__},

        {"math/oct_i", __oct__},
        {"math/oct_l", __oct__},

        {"math/hex_i", __hex__},
        {"math/hex_l", __hex__},

        // time
        {"time/now", __now__},
        {"time/strftime", __strftime__},
        {"time/strptime", __strptime__},

        // profiler
        {"profiler/begin", __profiler_begin__},
        {"profiler/end", __profiler_end__},
        {"profiler/instant", __profiler_instant__},
        {"profiler/enable", __profiler_enable__},

        // this
        {"zen", __zen__},

        // macro
        {"macro/cmp", __cmp__},
    };
    return map;
}

BasicBuiltinExecutor::BasicBuiltinExecutor(context_ptr_t ctx) : Executor(ctx, getOpsImplMap()) {};

executor_ptr_t BasicBuiltinExecutor::create(context_ptr_t ctx) {
    return std::make_shared<BasicBuiltinExecutor>(ctx);
}

void BasicBuiltinExecutor::eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) {
    EXEC_WHEN_DEBUG(l.in("BasicExec").debug("Evaluating operator of URI: {}", uri));
    auto it = opsMap_.find(uri);
    if (it == opsMap_.end()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidURI,
            std::format("Invalid URI: {}", uri));
    }
    std::vector<GraphIR::data_idx_t> inputIndices;
    for (const auto &in : self->normInputs()) {
        inputIndices.push_back(in->index());
    }
    for (const auto &in : self->withInputs()) {
        inputIndices.push_back(in->index());
    }
    return it->second(
        self->index(),
        data_arr_t{
            inputIndices.data(),
            self->normInputs().size(),
        },
        data_arr_t{
            inputIndices.data() + self->normInputs().size(),
            self->withInputs().size(),
        },
        frame,
        *context_);
};
