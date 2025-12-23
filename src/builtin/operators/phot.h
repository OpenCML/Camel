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
 * Author: Yuxuan Zheng
 * Created: Dec. 22, 2025
 * Updated: Dec. 22, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/context/frame.h"
#include "core/operator.h"

// Phot configuration
void __phot_config__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);

// Phot signal generation and processing
void __phot_gen_bits__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);
void __phot_modulation__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);
void __phot_up_sample__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);
void __phot_pulse_shaper__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);
void __phot_constellation_diagram__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx);

