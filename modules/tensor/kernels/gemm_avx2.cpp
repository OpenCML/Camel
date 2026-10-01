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
 * AVX2 + FMA SGEMM instantiation: 8-wide vectors, 6 x 16 tiles. This unit is
 * compiled with AVX2/FMA flags and only called after a runtime CPU check.
 */

#define CAMEL_GEMM_VEC_WIDTH 8
#define CAMEL_GEMM_MR 6
#define CAMEL_GEMM_NR 16
#define CAMEL_GEMM_ENTRY sgemmAvx2
#include "gemm_blocked.inl"
