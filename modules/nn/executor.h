/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Neural-network executor factory.
 *
 * The first nn module demo exposes graph-generating macro operators used by
 * std::macro to synthesize small training functions.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/execute/executor.h"

executor_ptr_t createNnExecutor(camel::core::context::context_ptr_t ctx);
