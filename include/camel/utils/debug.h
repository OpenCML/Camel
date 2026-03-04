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
 * Created: Sep. 04, 2025
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#ifndef NDEBUG
#define EXEC_WHEN_DEBUG(code)                                                                      \
    do {                                                                                           \
        code;                                                                                      \
    } while (0)
#else
#define EXEC_WHEN_DEBUG(code)
#endif
