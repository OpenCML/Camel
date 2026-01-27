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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"

// Phot configuration
slot_t __phot_config__(ArgsView &with, ArgsView &norm, Context &ctx);

// Phot signal generation and processing
slot_t __phot_gen_bits__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __phot_modulation__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __phot_up_sample__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __phot_pulse_shaper__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __phot_constellation_diagram__(ArgsView &with, ArgsView &norm, Context &ctx);
