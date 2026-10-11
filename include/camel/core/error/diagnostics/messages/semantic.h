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
 * Updated: Apr. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

namespace camel::core::error {

enum class SemanticDiag : uint32_t {
    // Errors
    UnknownSemanticError         = 0x000000,
    Redeclaration                = 0x000001,
    ModuleNotFound               = 0x000002,
    ReservedIdentifier           = 0x000003,
    TupleUnpackingCountMismatch  = 0x000004,
    InvalidLiteral               = 0x000005,
    DuplicateDictKey             = 0x000006,
    ParamRefMustBeUnqualified    = 0x000007,
    ParamDataMustBeStatic        = 0x000008,
    DuplicateParameter           = 0x000009,
    UnresolvedTypeReference      = 0x00000A,
    UnresolvedReference          = 0x00000B,
    BranchReturnTypeMismatch     = 0x00000C,
    InvalidAccessIndex           = 0x00000D,
    TypeNotIndexable             = 0x00000E,
    NoMatchingFunction           = 0x00000F,
    ArgumentsMismatch            = 0x000010,
    ReturnTypeMismatch           = 0x000011,
    ElementTypeMismatch          = 0x000012,
    CallingIncompleteFunction    = 0x000013,
    ClosureCaptureAfterSelfCall  = 0x000014,
    ImportNameNotExported        = 0x000015,
    LiteralStaticCastFailed      = 0x000016,
    DynamicCastForbidden         = 0x000017,
    ModuleParseFailed            = 0x000018,
    InvalidOperatorURI           = 0x000019,
    UnrecognizedExecutorProtocol = 0x00001A,
    EntryModuleMissingMain       = 0x00001B,
    ArgumentsRejected            = 0x00001C,
    ImmutablePassedToVarParam    = 0x00001D,
    AssignmentTargetNotVariable  = 0x00001E,
    ConstructNotSupported        = 0x00001F,
    InvalidTypeArgument          = 0x000020,
    ArgumentNeedsConversion      = 0x000021,
    ReturnNeedsConversion        = 0x000022,
    // An error: the construct it reports is not compiled, so a program that uses it must not
    // run. It used to be a warning (0x100000), and the declaration that used the feature was
    // dropped while the rest of the program went on.
    FeatureNotSupported          = 0x000040,
    // Generic functions and type functions (design principles 2.9).
    TypeVariableNotAllowed       = 0x000050,
    TypeVariableRedeclared       = 0x000051,
    TypeVariableNotBindable      = 0x000052,
    GenericArgumentsMismatch     = 0x000053,
    GenericFunctionAsValue       = 0x000054,
    GenericInstantiationTooDeep  = 0x000055,
    TypeFunctionArguments        = 0x000056,
    GenericFunctionUnsupported   = 0x000057,

    // Warnings
    VarParamInAsyncFunction = 0x100001,
    IgnoredSideEffect       = 0x100002,
    CapturedWriteInAsyncFunction = 0x100003,
    EffectNotExecuted            = 0x100004,
};

constexpr DiagType diagTypeOf(SemanticDiag) { return DiagType::SemanticDiag; }

const std::unordered_map<SemanticDiag, DiagInfo> &getSemanticDiagInfoMap();

} // namespace camel::core::error
