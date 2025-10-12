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
 * Created: Sep. 11, 2025
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

enum class SemanticDiag : uint32_t {
    // Errors
    UnknownSemanticError = 0x000000,
    Redeclaration = 0x000001,
    ModuleNotFound = 0x000002,
    ReservedIdentifier = 0x000003,
    TupleUnpackingCountMismatch = 0x000004,
    InvalidLiteral = 0x000005,
    DuplicateDictKey = 0x000006,
    ParamRefMustBeUnqualified = 0x000007,
    ParamDataMustBeStatic = 0x000008,
    DuplicateParameter = 0x000009,
    UnresolvedTypeReference = 0x00000A,
    UnresolvedReference = 0x00000B,
    BranchReturnTypeMismatch = 0x00000C,
    InvalidAccessIndex = 0x00000D,
    TypeNotIndexable = 0x00000E,
    NoMatchingFunction = 0x00000F,
    ArgumentsMismatch = 0x000010,
    ReturnTypeMismatch = 0x000011,
    ElementTypeMismatch = 0x000012,
    CallingIncompleteFunction = 0x000013,

    // Warnings
    FeatureNotSupported = 0x100000,
    VarParamInAsyncFunction = 0x100001,
    IgnoredSideEffect = 0x100002,
};

constexpr DiagType diagTypeOf(SemanticDiag) { return DiagType::SemanticDiag; }

const std::unordered_map<SemanticDiag, DiagInfo> &getSemanticDiagInfoMap();
