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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

namespace camel::core::error {

enum class SemanticDiag : uint32_t {
    // Errors
    UnknownSemanticError        = 0x000000,
    Redeclaration               = 0x000001,
    ModuleNotFound              = 0x000002,
    ReservedIdentifier          = 0x000003,
    TupleUnpackingCountMismatch = 0x000004,
    InvalidLiteral              = 0x000005,
    DuplicateDictKey            = 0x000006,
    ParamRefMustBeUnqualified   = 0x000007,
    ParamDataMustBeStatic       = 0x000008,
    DuplicateParameter          = 0x000009,
    UnresolvedTypeReference     = 0x00000A,
    UnresolvedReference         = 0x00000B,
    BranchReturnTypeMismatch    = 0x00000C,
    InvalidAccessIndex          = 0x00000D,
    TypeNotIndexable            = 0x00000E,
    NoMatchingFunction          = 0x00000F,
    ArgumentsMismatch           = 0x000010,
    ReturnTypeMismatch          = 0x000011,
    ElementTypeMismatch         = 0x000012,
    CallingIncompleteFunction   = 0x000013,
    // 0x000014 was ClosureCaptureAfterSelfCall, replaced by CaptureAfterRecursiveCall: with
    // function bodies built when first needed it is only met in recursion.
    ImportNameNotExported = 0x000015,
    // A literal the type it is given cannot hold: out of range, truncated (a decimal literal
    // in an integer type), or not a value of that type at all (a text that is not a number).
    // One diagnostic for all of them (handover H24): it says which literal, which type, and
    // why.
    LiteralNotRepresentable      = 0x000016,
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
    // 0x000023 was LiteralOutOfRange, merged into LiteralNotRepresentable (0x000016).
    // 0x000025 was ImportCycle: modules may import each other (design principles 10.1).
    // 0x000026 was ImportedFunctionUsesModuleValue: a function of another module may use the
    // top level of its own module since every module builds into one root graph.
    ImportNameConflict = 0x000027,
    // An error: the construct it reports is not compiled, so a program that uses it must not
    // run. It used to be a warning (0x100000), and the declaration that used the feature was
    // dropped while the rest of the program went on.
    FeatureNotSupported = 0x000040,
    // Generic functions and type functions (design principles 5.6).
    TypeVariableNotAllowed      = 0x000050,
    TypeVariableRedeclared      = 0x000051,
    TypeVariableNotBindable     = 0x000052,
    GenericArgumentsMismatch    = 0x000053,
    GenericFunctionAsValue      = 0x000054,
    GenericInstantiationTooDeep = 0x000055,
    TypeFunctionArguments       = 0x000056,
    GenericFunctionUnsupported  = 0x000057,
    // Functions as values.
    FunctionNameAsValue = 0x000060,
    // Nullable and fallible values (design principles 3.8;
    // docs/technical/29_nullable_and_fallible_values.md).
    ThrowOutsideFallible  = 0x000070,
    ThrowNotAnError       = 0x000071,
    TaggedTypeNesting     = 0x000072,
    TaggedValueNotOpened  = 0x000073,
    TryCatchNotInLanguage = 0x000074,
    TaggedOperandMismatch = 0x000075,
    TaggedMatchCases      = 0x000076,
    // Reporting several errors (0x80 and up, clear of the blocks above).
    ImportedModuleHasErrors = 0x000080,
    // Map and Set (design principles 11.3; docs/technical/31_map_and_set.md).
    CollectionKeyNotAllowed = 0x0000A0,
    DuplicateCollectionKey  = 0x0000A1,
    // Rulings of 2026-10-12 (design principles log 2.10): 0xB0 and up.
    MatchWithoutDefault        = 0x0000B0,
    IfWithoutElseAsValue       = 0x0000B1,
    EmptyArrayWithoutType      = 0x0000B2,
    PartialApplicationRefused  = 0x0000B3,
    ComprehensionSourceRefused = 0x0000B4,
    // Rulings of 2026-10-13 (design principles log 2.11): 0xC0 and up.
    LogicalOperandNotBool = 0x0000C0,
    // Modules and namespaces (design principles 10.1, 10.2): 0xD0 and up.
    ExportedVariable          = 0x0000D0,
    ModuleValueCycle          = 0x0000D1,
    CaptureAfterRecursiveCall = 0x0000D2,
    AmbiguousName             = 0x0000D3,
    // `any` (design principles 5.10), 0xE0 and up. 0xE0 (AnyUncheckedCast), 0xE1
    // (AnyBranchSubject) and 0xE2 (AnyNotAllowed) are retired: there is no `any` type, and
    // writing it is an ordinary unknown name. Their codes are not reused.
    // The condition and the cases of a branch (found with the `any` cases above).
    ConditionNotBool      = 0x0000E3,
    MatchCaseTypeMismatch = 0x0000E4,
    // Rulings of 2026-10-14 (design principles log 2.12) on operators and on collections that
    // are not changed in place: 0xF0 and up.
    UnusedPureResult         = 0x0000F0,
    OperatorNotOverloadable  = 0x0000F1,
    OperatorOverloadInvalid  = 0x0000F2,
    OperatorOverloadConflict = 0x0000F3,
    // A call (of a function, an operator or a partial application used as a value) that
    // more than one overload takes: several candidates in the pass being used, never
    // settled by declaration order (design principles 5.1). Renamed from
    // AmbiguousOperatorCall when it became the report for every call; the code is unchanged.
    AmbiguousCall = 0x0000F4,
    // Rulings of 2026-10-15 (design principles log 2.13) and the defects found with them: 0x110
    // and up.
    DeclaredTypeMismatch = 0x000110,
    ModuleTypeCycle      = 0x000111,
    InvalidAnnotation    = 0x000112,
    // The structured loop (design principles 3.10): 0x120 and up.
    LoopSequenceNotIterable = 0x000120,
    LoopBodyTypeMismatch    = 0x000121,
    LoopStateWithoutType    = 0x000122,
    LoopConditionNotBool    = 0x000123,
    LoopNameRepeated        = 0x000124,
    // Rulings of 2026-10-19 (design principles log 2.13): the nominal enum, 0x130 and up.
    DuplicateEnumVariant   = 0x000130,
    DuplicateEnumField     = 0x000131,
    EnumVariantNeedsFields = 0x000132,
    EnumVariantFieldCount  = 0x000133,
    EnumPatternMismatch    = 0x000134,
    EnumMatchNotExhaustive = 0x000135,
    EnumFieldAccess        = 0x000136,
    // 0x137 and 0x138 reported the retired forms `A | B` and `T ? U`; the parser no longer reads
    // either, so they are gone and their codes are not reused.
    EnumTypeArgumentNotInferred = 0x000139,
    EnumFieldTypeMismatch       = 0x00013A,
    EnumConstructionMismatch    = 0x00013B,
    // Named dimensions of type extents (design principles 5.4): 0x140 and up.
    ConstasNotAllowed         = 0x000140,
    DimensionNameNotBound     = 0x000141,
    DimensionNameAlreadyBound = 0x000142,
    DimensionMismatch         = 0x000143,
    InvalidExtentExpression   = 0x000144,
    // Automatic differentiation (design principles 9.9): 0x150 and up.
    NotDifferentiable = 0x000150,
    // Tags (design principles 6.2): 0x160 and up.
    TagUnknownPosition     = 0x000160,
    TagNotAtPosition       = 0x000161,
    TagUndeclared          = 0x000162,
    TagArgumentCount       = 0x000163,
    TagArgumentNotConstant = 0x000164,
    TagParameterMissing    = 0x000166,
    TagOnUnnamedField      = 0x000167,
    TagArgumentNamed       = 0x000168,
    TagArgumentUnsupported = 0x000169,
    TagArgumentType        = 0x00016A,
    TagNotAPermission      = 0x00016B,
    RuleTagNotRequires     = 0x00016C,
    TagParameterType       = 0x00016D,
    TagShadowsPermission   = 0x00016E,
    // Static expansion (design principles 5.5, handover H51): 0x170 and up.
    StaticMatchNotExhaustive  = 0x000170,
    StaticMatchUnreachableCase = 0x000171,
    StaticTypeNotConcrete     = 0x000172,
    StaticMatchTooDeep        = 0x000173,
    GenericInstantiationCycle = 0x000174,
    StaticForNotComposite       = 0x000175,
    StaticAccessNotInternal     = 0x000176,
    StaticAccessUnknownField    = 0x000177,
    StaticAccessIndexOutOfRange = 0x000178,
    StaticAccessNotConstant     = 0x000179,
    StaticComprehensionVoidElement = 0x00017A,

    // `indices` (H51 step 6). 0x17B-0x17D are the body-less declaration diagnostics below, so
    // this one takes the next free code after them.
    StaticIndicesNotTuple = 0x000182,

    // Rest parameters (design principles 5.5, H51 step 6): 0x18A and up.
    /// A rest parameter is not the last of the ordinary parameters.
    RestParameterNotLast = 0x00018A,
    /// A type was written after `..`: a rest parameter is untyped.
    RestParameterTypeNotSupported = 0x00018B,
    /// A `..e` argument is followed by another argument.
    RestArgumentNotLast = 0x00018D,
    /// The operand of `..e` is not a compile-time tuple.
    RestArgumentNotTuple = 0x00018E,
    /// A rest function was used where a function value is needed.
    RestFunctionNeedsExpansion = 0x00018F,
    /// A call passes fewer arguments than the fixed parameters of a rest function.
    RestArgumentsMismatch = 0x000190,
    /// A rest parameter's build-time tuple was used where a runtime value is needed.
    StaticTupleEscapes = 0x00019C,

    // `constas` value parameters (design principles 5.4): 0x191 and up. 0x18C stays free.
    /// A `constas` parameter is not the first ordinary parameter.
    ConstasParameterNotFirst = 0x000191,
    /// A declaration has more than one `constas` parameter.
    ConstasParameterOnlyOne = 0x000192,
    /// A `constas` parameter writes no type.
    ConstasTypeRequired = 0x000193,
    /// A `constas` parameter has a type other than `int`.
    ConstasTypeNotInt = 0x000194,
    /// The argument bound to a `constas` parameter is not a compile-time constant.
    ConstasValueNotConstant = 0x000195,
    /// The constant bound to a `constas` parameter is not an `int`.
    ConstasValueNotInt = 0x000196,
    /// The value of a `constas` parameter is not used by any type of the declaration.
    ConstasNotUsedInType = 0x000197,
    /// A `constas` parameter is combined with a rest parameter.
    ConstasWithRest = 0x000198,
    /// A `constas` declaration was used where a function value is needed.
    ConstasFunctionNeedsCall = 0x000199,
    /// A `constas` declaration is a body-less declaration of a C++ operator.
    ConstasInNativeDeclaration = 0x00019A,
    /// A call to a `constas` declaration gives no value for the parameter.
    ConstasArgumentMissing = 0x00019B,
    /// A tag gives a fact one value where the node already has another.
    TagValueConflict = 0x00019D,

    // `std::cml`, the source printer (handover H240, step S1). 0x19E and up.
    /// A graph holds a construct the printer does not turn into source (yet).
    CmlUnsupportedConstruct = 0x00019E,
    /// A constant is larger than the printer's max_literal_bytes.
    CmlBigConstant = 0x00019F,

    // Body-less declarations, the declarations of a C++ module's operators (design principles
    // 6.4; handover H19, step 1). 0x17B and up.
    NativeDeclarationIncomplete = 0x00017B,
    NativeDeclarationNotLoaded  = 0x00017C,
    DeclarationModuleHasBody    = 0x00017D,
    // Handover H94 (the author's rulings on body-less declarations): 0x17E and up.
    /// Two generic declarations of one name can take the same call (ruling 2).
    NativeDeclarationOverlap = 0x000180,
    /// A declaration names an operator of another module (ruling 5).
    NativeDeclarationForeignOperator = 0x000181,

    // A lambda's parameter that writes no type and that the function type of its position does
    // not determine (design principles 5.9). 0x17E and up.
    LambdaParameterWithoutType = 0x00017E,

    // Warnings
    VarParamInAsyncFunction      = 0x100001,
    IgnoredSideEffect            = 0x100002,
    CapturedWriteInAsyncFunction = 0x100003,
    EffectNotExecuted            = 0x100004,
    // 0x90 and up, clear of what other work adds after EffectNotExecuted.
    ExitInUnusedBranch = 0x100090,
    // Rulings of 2026-10-13 (design principles log 2.11): 0xC0 and up.
    UnusedFallibleValue = 0x1000C0,
    // Rulings of 2026-10-15 (design principles log 2.13): 0x110 and up.
    NullTestNeverNull = 0x100110,
    KnownNotNull      = 0x100111,
    // Rulings of 2026-10-19 (design principles log 2.13): the enum, 0x130 and up.
    EnumVariantCaseRepeated = 0x100130,
    MatchCaseAfterWildcard  = 0x100131,
    // `case _` after every variant is matched: the wildcard can never be taken.
    MatchWildcardRedundant = 0x100132,
    // Tags (design principles 6.2): the tag is declared for no pass in the pipeline, so nothing
    // reads what was written.
    TagHasNoReader = 0x100160,
};

constexpr DiagType diagTypeOf(SemanticDiag) { return DiagType::SemanticDiag; }

const std::unordered_map<SemanticDiag, DiagInfo> &getSemanticDiagInfoMap();

} // namespace camel::core::error
