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
    // 0x000014 was ClosureCaptureAfterSelfCall, replaced by CaptureAfterRecursiveCall: with
    // function bodies built when first needed it is only met in recursion.
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
    LiteralOutOfRange            = 0x000023,
    // 0x000025 was ImportCycle: modules may import each other (design principles 2.11).
    // 0x000026 was ImportedFunctionUsesModuleValue: a function of another module may use the
    // top level of its own module since every module builds into one root graph.
    ImportNameConflict           = 0x000027,
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
    // Functions as values.
    FunctionNameAsValue          = 0x000060,
    // Nullable and fallible values (design principles 2.9;
    // docs/technical/29_nullable_and_fallible_values.md).
    ThrowOutsideFallible         = 0x000070,
    ThrowNotAnError              = 0x000071,
    TaggedTypeNesting            = 0x000072,
    TaggedValueNotOpened         = 0x000073,
    TryCatchNotInLanguage        = 0x000074,
    TaggedOperandMismatch        = 0x000075,
    TaggedMatchCases             = 0x000076,
    // Reporting several errors (0x80 and up, clear of the blocks above).
    ImportedModuleHasErrors      = 0x000080,
    // Map and Set (design principles 2.9; docs/technical/31_map_and_set.md).
    CollectionKeyNotAllowed      = 0x0000A0,
    DuplicateCollectionKey       = 0x0000A1,
    // Rulings of 2026-10-12 (design principles 2.10): 0xB0 and up.
    MatchWithoutDefault          = 0x0000B0,
    IfWithoutElseAsValue         = 0x0000B1,
    EmptyArrayWithoutType        = 0x0000B2,
    PartialApplicationRefused    = 0x0000B3,
    ComprehensionSourceRefused   = 0x0000B4,
    // Rulings of 2026-10-13 (design principles 2.11): 0xC0 and up.
    LogicalOperandNotBool        = 0x0000C0,
    // Modules and namespaces (design principles 2.11, 2.12): 0xD0 and up.
    ExportedVariable             = 0x0000D0,
    ModuleValueCycle             = 0x0000D1,
    CaptureAfterRecursiveCall    = 0x0000D2,
    AmbiguousName                = 0x0000D3,
    // `any` (design principles 2.11, 2.12), 0xE0 and up. 0xE0 (AnyUncheckedCast) and 0xE1
    // (AnyBranchSubject) reported reading a slot typed `any`; with the type gone they are
    // retired, and their codes are not reused.
    AnyNotAllowed                = 0x0000E2,
    // The condition and the cases of a branch (found with the `any` cases above).
    ConditionNotBool             = 0x0000E3,
    MatchCaseTypeMismatch        = 0x0000E4,
    // Rulings of 2026-10-14 (design principles 2.12) on operators and on collections that
    // are not changed in place: 0xF0 and up.
    UnusedPureResult             = 0x0000F0,
    OperatorNotOverloadable      = 0x0000F1,
    OperatorOverloadInvalid      = 0x0000F2,
    OperatorOverloadConflict     = 0x0000F3,
    AmbiguousOperatorCall        = 0x0000F4,
    // Rulings of 2026-10-15 (design principles 2.13) and the defects found with them: 0x110
    // and up.
    DeclaredTypeMismatch         = 0x000110,
    ModuleTypeCycle              = 0x000111,
    InvalidAnnotation            = 0x000112,

    // Warnings
    VarParamInAsyncFunction = 0x100001,
    IgnoredSideEffect       = 0x100002,
    CapturedWriteInAsyncFunction = 0x100003,
    EffectNotExecuted            = 0x100004,
    // 0x90 and up, clear of what other work adds after EffectNotExecuted.
    ExitInUnusedBranch           = 0x100090,
    IntegerLiteralTruncated      = 0x1000A0,
    // Rulings of 2026-10-13 (design principles 2.11): 0xC0 and up.
    UnusedFallibleValue          = 0x1000C0,
    // Rulings of 2026-10-15 (design principles 2.13): 0x110 and up.
    NullTestNeverNull            = 0x100110,
    KnownNotNull                 = 0x100111,
};

constexpr DiagType diagTypeOf(SemanticDiag) { return DiagType::SemanticDiag; }

const std::unordered_map<SemanticDiag, DiagInfo> &getSemanticDiagInfoMap();

} // namespace camel::core::error
