/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Portable SGEMM instantiation: 4-wide vectors (SSE2 / NEON baseline), 4 x 8 tiles.
 */

#define CAMEL_GEMM_VEC_WIDTH 4
#define CAMEL_GEMM_MR 4
#define CAMEL_GEMM_NR 8
#define CAMEL_GEMM_ENTRY sgemmGeneric
#include "gemm_blocked.inl"
