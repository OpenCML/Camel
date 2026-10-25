// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "globals.h"
#include "builtin/executors/builtin.h"
#include "builtin/operators/array.h"
#include "builtin/operators/collection.h"
#include "builtin/operators/derivative.h"
#include "builtin/operators/enum.h"
#include "builtin/operators/mem.h"
#include "builtin/operators/ops.h"
#include "builtin/operators/set_value.h"
#include "builtin/operators/tagged.h"
#include "camel/core/context/context.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/core/type/typevar.h"

#include "camel/utils/assert.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace std;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

/// Whether a value of type `type` exists at run time, so that an operator taking a value of
/// every type (`string(x)`) may take it: not `void`, not a type the compiler uses on its own
/// (a type variable, a union, a reference). This is what a parameter typed `any` used to accept.
bool isConcreteValueType(Type *type) {
    return type != nullptr && type->code() != TypeCode::Void && !isAuxiliary(type->code());
}

/// A type parameter of a signature: `binder` at its first occurrence, a use at every later one.
Type *typeParameter(const char *name, bool binder) { return TypeVarType::create(name, binder); }

/// The signature of `map`'s rule as a type: `<f: (item: T) => U> (collect: T[]) => U[]`. The
/// rule reports that declaration as spelling for reports; this is the same declaration with its
/// variables as types, so a lambda written in the `with` position takes its parameter type from
/// it before the call is resolved (design principles 5.9).
FunctionType *mapRuleSignature() {
    FunctionTypeFactory callback;
    callback.addNormArg("item", typeParameter("T", true), false);
    callback.setExitType(typeParameter("U", true));
    FunctionTypeFactory signature;
    signature.addWithArg("f", callback.buildOpen(), false);
    signature.addNormArg("xs", ArrayType::create(typeParameter("T", false)), false);
    signature.setExitType(ArrayType::create(typeParameter("U", false)));
    return signature.buildOpen();
}

/// The signature of `reduce`'s rule as a type:
/// `<f: (acc: U, item: T) => U, initial: U> (collect: T[]) => U`.
FunctionType *reduceRuleSignature() {
    FunctionTypeFactory callback;
    callback.addNormArg("acc", typeParameter("U", true), false);
    callback.addNormArg("item", typeParameter("T", true), false);
    callback.setExitType(typeParameter("U", false));
    FunctionTypeFactory signature;
    signature.addWithArg("f", callback.buildOpen(), false);
    signature.addWithArg("initial", typeParameter("U", false), false);
    signature.addNormArg("xs", ArrayType::create(typeParameter("T", false)), false);
    signature.setExitType(typeParameter("U", false));
    return signature.buildOpen();
}

/// The rule with that signature fixed: a rule whose result depends on its arguments can still
/// declare the parameter types every call is held to, so an argument that needs them (a lambda
/// with unwritten parameters) can be built before the call is settled.
resolver_ptr_t withFixedSignature(resolver_ptr_t resolver, FunctionType *signature) {
    auto *dynamic = dynamic_cast<DynamicFuncTypeResolver *>(resolver.get());
    ASSERT(dynamic != nullptr, "A fixed signature was given to a rule that is not dynamic.");
    if (dynamic != nullptr) {
        dynamic->declareFixedSignature(signature);
    }
    return resolver;
}

/// A rule of one ordinary parameter whose signature records the parameter's name: the name is
/// what a named argument binds to (`len(x: xs)`), so a rule and the declaration it is written
/// beside must give a position the same name (stdlib-conventions, "the same meaning at a position
/// shares its name"). `StaticFuncTypeResolver::create` takes types only, and a signature built
/// from types records no names at all, so the name is written here.
resolver_ptr_t namedStaticSignature(const char *name, Type *param, Type *exit) {
    FunctionTypeFactory factory;
    factory.addNormArg(name, param, false);
    factory.setExitType(exit);
    return StaticFuncTypeResolver::create(factory.build());
}

/// The two entries a group adds for the reduced-precision floating types, written where the
/// group's other entries are (a group is a braced list, and a `std::vector` cannot be one of its
/// elements, so a function call cannot stand in for the two entries). The URI suffix is `bf` for
/// bf16 and `f6` for f16 (`b` is already bool).
///
/// A binary operator of a group whose result is the operand type; both operands are the type.
#define CAMEL_HALF_BINARY(STEM)                                                                    \
    {":op/" STEM "_bf",                                                                            \
     StaticFuncTypeResolver::create(                                                               \
         {},                                                                                       \
         {{Type::BFloat16(), false}, {Type::BFloat16(), false}},                                   \
         Type::BFloat16())},                                                                       \
    {                                                                                              \
        ":op/" STEM "_f6", StaticFuncTypeResolver::create(                                         \
                               {},                                                                 \
                               {{Type::Float16(), false}, {Type::Float16(), false}},               \
                               Type::Float16())                                                    \
    }

/// A comparing operator of a group: the operands are the type, the result is bool.
#define CAMEL_HALF_COMPARE(STEM)                                                                   \
    {":op/" STEM "_bf",                                                                            \
     StaticFuncTypeResolver::create(                                                               \
         {},                                                                                       \
         {{Type::BFloat16(), false}, {Type::BFloat16(), false}},                                   \
         Type::Bool())},                                                                           \
    {                                                                                              \
        ":op/" STEM "_f6", StaticFuncTypeResolver::create(                                         \
                               {},                                                                 \
                               {{Type::Float16(), false}, {Type::Float16(), false}},               \
                               Type::Bool())                                                       \
    }

/// A compound assignment of a group: the target is the type and keeps it, the value is the type,
/// and what the write gives is the version of the variable: the cell itself.
#define CAMEL_HALF_ASSIGN(STEM)                                                                    \
    {":op/" STEM "_bf",                                                                            \
     StaticFuncTypeResolver::create(                                                               \
         {},                                                                                       \
         {{Type::BFloat16(), true}, {Type::BFloat16(), false}},                                    \
         TupleType::cell(Type::BFloat16()))},                                                      \
    {                                                                                              \
        ":op/" STEM "_f6", StaticFuncTypeResolver::create(                                         \
                               {},                                                                 \
                               {{Type::Float16(), true}, {Type::Float16(), false}},                \
                               TupleType::cell(Type::Float16()))                                   \
    }

/// A unary operator of a group (`-x`): the operand is the type and so is the result.
#define CAMEL_HALF_UNARY(STEM)                                                                     \
    {":op/" STEM "_bf",                                                                            \
     StaticFuncTypeResolver::create({}, {{Type::BFloat16(), false}}, Type::BFloat16())},           \
    {                                                                                              \
        ":op/" STEM "_f6",                                                                         \
            StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Float16())        \
    }

/**
 * The static checks of a subscript on an array (design principles 3.9 and 5.4; the "越界" rule of
 * docs/design-principles.cn.md: where the compiler can know the length — a fixed-length type is
 * the case it names — an index or a slice bound outside it is a compile-time error, not a silent
 * truncation and not something left to the run). An array whose type states a constant extent for
 * its first dimension has a length the compiler knows, so `t[i]` with a constant `i` outside it,
 * and `t[a:b]` with constant bounds outside it, are refused where they are written. An array whose
 * type states no extent (`int[]`) keeps the run-time fault: its length is not part of its type,
 * which is the case `feat.consistency.faults`' `fault.array_index_constant` records.
 *
 * The same static extent makes the slice a view sharing the block (5.4), so its first dimension
 * must be the only one and its step the constant 1: a slice of a rank-2 or deeper array, and a
 * step other than 1, are views with strides, which need the multidimensional array representation
 * of H17, and are refused at the call rather than lowered to a copy that would not share the
 * block. Where the extent is not in the type the operator is left to its kernel, which reads a
 * stepped slice as a copy — the H17 deviation `feat.linear.subscript`'s `subscript.step` records.
 *
 * These are the checks the tensor module's `indexInfer` and `sliceSubInfer` made for `t[i]` and
 * `t[a:b]` before `Tensor` became an array (H419): a `Tensor` is an array now, so the built-in
 * `[]` operators are the ones its subscripts reach, and the rule has to be here or it is nowhere.
 */

/// The constant extent the type states for axis 0: an array of a fixed rank whose first extent is
/// a constant. nullopt for `T[]` (one unknown extent), for an open-rank type (`T[..]`), and for a
/// type that is not an array.
std::optional<int64_t> staticFirstExtent(const Type *type) {
    const ArrayType *array = asArrayType(type);
    if (array == nullptr || array->anyRank() || array->rank() < 1) {
        return std::nullopt;
    }
    const ExtentForm &first = array->extents()[0];
    return first.isConstant() ? std::optional<int64_t>(first.constant()) : std::nullopt;
}

/// The value of an argument the compiler knows — a literal, or a static the graph builder supplies
/// for one — when it is one at all.
std::optional<int64_t> constantArg(static_args_t statics, size_t index) {
    if (statics.size() <= index || !statics[index].value.has_value()) {
        return std::nullopt;
    }
    return static_cast<int64_t>(*statics[index].value);
}

/// Why `t[i]` cannot be this call: the index and the extent are both constants and the index
/// leaves the extent. The wording is the one the tensor module's `indexInfer` used for `t[i]`
/// before the flip, so the report a program gets does not depend on which module declared the
/// array.
std::optional<std::string> staticIndexRejection(const type_vec_t &norm, static_args_t statics) {
    if (norm.size() != 2 || norm[1] == nullptr || norm[1]->code() != TypeCode::Int64) {
        return std::nullopt;
    }
    const std::optional<int64_t> extent = staticFirstExtent(norm[0]);
    const std::optional<int64_t> index  = constantArg(statics, 1);
    if (!extent || !index || (*index >= 0 && *index < *extent)) {
        return std::nullopt;
    }
    return std::format("index {} is outside axis 0 of extent {}", *index, *extent);
}

/// Why `t[a:b]` / `t[a:b:s]` cannot be this call: a constant bound leaves the statically known
/// first extent, or the result would not be a view of a rank-1 array.
std::optional<std::string> staticSliceRejection(const type_vec_t &norm, static_args_t statics) {
    if (norm.size() != 5) {
        return std::nullopt;
    }
    const ArrayType *array = asArrayType(norm[0]);
    if (array == nullptr) {
        return std::nullopt;
    }
    if (!array->anyRank() && array->rank() >= 2) {
        return std::string(
            "a slice of the first dimension of a tensor of rank 2 or more is a view with "
            "strides, which needs the multidimensional array representation of H17");
    }
    const std::optional<int64_t> extent = staticFirstExtent(norm[0]);
    if (!extent) {
        return std::nullopt; // `T[]`: the length is not in the type, and the kernel reads it
    }
    if (const std::optional<int64_t> step = constantArg(statics, 3); !step || *step != 1) {
        return std::string(
            "a slice step other than the constant 1 is a view with strides, which needs the "
            "multidimensional array representation of H17");
    }
    // The bounds as the slice reads them: a bound the program did not write is 0 for the start and
    // the end of the array for the end, which the two-bit mask the compiler writes with the call
    // says (`globals.cpp`, `__slice__`).
    const std::optional<int64_t> mask = constantArg(statics, 4);
    if (!mask) {
        return std::nullopt;
    }
    const std::optional<int64_t> from =
        (*mask & 2) != 0 ? constantArg(statics, 1) : std::optional<int64_t>(0);
    const std::optional<int64_t> to = (*mask & 1) != 0 ? constantArg(statics, 2) : extent;
    if (!from || !to) {
        return std::nullopt; // a bound the compiler does not know: the kernel reads it
    }
    if (*from < 0 || *from > *extent || *to < 0 || *to > *extent) {
        return std::format(
            "slice bounds {}..{} are outside the first dimension of extent {}",
            *from,
            *to,
            *extent);
    }
    return std::nullopt;
}

/// An operator's own rule plus the checks above: the call is refused where it is written when the
/// operator's constants and the argument types say it can never be right. The inner rule is
/// untouched, so a call whose constants are inside — and every call the compiler knows nothing
/// about — resolves exactly as it did.
class StaticCheckedResolver final : public FuncTypeResolver {
  public:
    using Check = std::optional<std::string> (*)(const type_vec_t &, static_args_t);

    StaticCheckedResolver(resolver_ptr_t inner, Check check)
        : inner_(std::move(inner)), check_(check) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        return inner_->resolve(with, norm, modifiers);
    }

    /// A rejected call is one whose arguments do not fit, so the value-carrying resolution the
    /// drafts use answers for it exactly as for any other mismatch.
    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        if (check_(norm, normStatics).has_value()) {
            return std::nullopt;
        }
        return inner_->resolveWith(with, norm, normStatics, modifiers);
    }

    /// The reason the call cannot be right, for the report the call gets (`ArgumentsRejected`).
    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        if (auto reason = check_(norm, normStatics)) {
            return reason;
        }
        return inner_->explainRejection(with, norm, normStatics, modifiers);
    }

    std::string signature() const override { return inner_->signature(); }
    FunctionType *fixedSignature() const override { return inner_->fixedSignature(); }
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        return inner_->parameterNames(with, norm);
    }
    bool varParameters(std::vector<bool> &with, std::vector<bool> &norm) const override {
        return inner_->varParameters(with, norm);
    }
    std::optional<camel::core::error::SemanticDiag> rejectionDiag() const override {
        return inner_->rejectionDiag();
    }

  private:
    resolver_ptr_t inner_;
    Check check_;
};

} // namespace

module_ptr_t GlobalsBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<GlobalsBuiltinModule>(ctx);
}

const std::vector<oper_group_ptr_t> &getGlobalOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        // ======= Type conversions =======
        OperatorGroup::create(
            "i32",
            {
                {
                    ":op/itoi",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/ltoi",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int32()),
                },
                {
                    ":op/ftoi",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int32()),
                },
                {
                    ":op/dtoi",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int32()),
                },
                {
                    ":op/stoi",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int32()),
                },
                {
                    ":op/bftoi",
                    StaticFuncTypeResolver::create({}, {{Type::BFloat16(), false}}, Type::Int32()),
                },
                {
                    ":op/f6toi",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Int32()),
                },
            }),
        OperatorGroup::create(
            "i64",
            {
                {
                    ":op/itol",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int64()),
                },
                {
                    ":op/ltol",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/ftol",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int64()),
                },
                {
                    ":op/dtol",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int64()),
                },
                {
                    ":op/stol",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64()),
                },
                {
                    ":op/bftol",
                    StaticFuncTypeResolver::create({}, {{Type::BFloat16(), false}}, Type::Int64()),
                },
                {
                    ":op/f6tol",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "int",
            {
                {
                    ":op/itol",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int64()),
                },
                {
                    ":op/ltol",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/ftol",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Int64()),
                },
                {
                    ":op/dtol",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Int64()),
                },
                {
                    ":op/stol",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Int64()),
                },
                {
                    ":op/bftol",
                    StaticFuncTypeResolver::create({}, {{Type::BFloat16(), false}}, Type::Int64()),
                },
                {
                    ":op/f6tol",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "f32",
            {
                {
                    ":op/itof",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float32()),
                },
                {
                    ":op/ltof",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float32()),
                },
                {
                    ":op/ftof",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":op/dtof",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float32()),
                },
                {
                    ":op/stof",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float32()),
                },
                {
                    ":op/bftof",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::BFloat16(), false}},
                        Type::Float32()),
                },
                {
                    ":op/f6tof",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Float32()),
                },
            }),
        OperatorGroup::create(
            "f64",
            {
                {
                    ":op/itod",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float64()),
                },
                {
                    ":op/ltod",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float64()),
                },
                {
                    ":op/ftod",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float64()),
                },
                {
                    ":op/dtod",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
                {
                    ":op/stod",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float64()),
                },
                {
                    ":op/bftod",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::BFloat16(), false}},
                        Type::Float64()),
                },
                {
                    ":op/f6tod",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "float",
            {
                {
                    ":op/itod",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float64()),
                },
                {
                    ":op/ltod",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float64()),
                },
                {
                    ":op/ftod",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float64()),
                },
                {
                    ":op/dtod",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
                {
                    ":op/stod",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float64()),
                },
                {
                    ":op/bftod",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::BFloat16(), false}},
                        Type::Float64()),
                },
                {
                    ":op/f6tod",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Float64()),
                },
            }),
        // bf16 and f16 are floating types a program writes and converts like f32 (design
        // principles 7.3): `bf16(x)` and `f16(x)` take every numeric type and a text, and round
        // to nearest with ties to even once. Writing the conversion is asking for that
        // precision, so no numeric permission is involved; `change_precision` governs a
        // computation the compiler moves to lower precision.
        OperatorGroup::create(
            "bf16",
            {
                {
                    ":op/itobf",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::BFloat16()),
                },
                {
                    ":op/ltobf",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::BFloat16()),
                },
                {
                    ":op/ftobf",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}},
                        Type::BFloat16()),
                },
                {
                    ":op/dtobf",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}},
                        Type::BFloat16()),
                },
                {
                    ":op/bftobf",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::BFloat16(), false}},
                        Type::BFloat16()),
                },
                {
                    ":op/stobf",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::BFloat16()),
                },
            }),
        OperatorGroup::create(
            "f16",
            {
                {
                    ":op/itof6",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Float16()),
                },
                {
                    ":op/ltof6",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Float16()),
                },
                {
                    ":op/ftof6",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float16()),
                },
                {
                    ":op/dtof6",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float16()),
                },
                {
                    ":op/f6tof6",
                    StaticFuncTypeResolver::create({}, {{Type::Float16(), false}}, Type::Float16()),
                },
                {
                    ":op/stof6",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::Float16()),
                },
            }),
        OperatorGroup::create(
            "c64",
            {
                {
                    ":op/itoc",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Complex64()),
                },
                {
                    ":op/ltoc",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Complex64()),
                },
                {
                    ":op/ftoc",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/dtoc",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/ctoc",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/ztoc",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}},
                        Type::Complex64()),
                },
            }),
        OperatorGroup::create(
            "c128",
            {
                {
                    ":op/itoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/ltoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/ftoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/dtoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/ctoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/ztoz",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}},
                        Type::Complex128()),
                },
            }),
        OperatorGroup::create(
            "string",
            {
                {
                    ":op/itos",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::String()),
                },
                {
                    ":op/ltos",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::String()),
                },
                {
                    ":op/ftos",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::String()),
                },
                {
                    ":op/dtos",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::String()),
                },
                {
                    ":op/stos",
                    StaticFuncTypeResolver::create({}, {{Type::String(), false}}, Type::String()),
                },
                {
                    ":op/ctos",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}},
                        Type::String()),
                },
                {
                    ":op/ztos",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}},
                        Type::String()),
                },
                {
                    // Every other type: the text println writes for the value. A type rule,
                    // not a parameter of type `any` (design principles 5.10): the operator
                    // reads the argument with the static type it has at the call.
                    ":op/atos",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(x: T) => string",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!isConcreteValueType(norm[0])) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
            }),

        // ======= Variables =======
        // A `var` variable is a cell (TupleType::cell). The compiler emits these two for a
        // declaration and for a read; assignment is the family below.
        OperatorGroup::create(
            "__var_new__",
            {
                {
                    ":var/new",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(value: T) => var T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> { return TupleType::cell(norm[0]->widened()); }),
                },
            }),
        OperatorGroup::create(
            "__var_get__",
            {
                {
                    ":var/get",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(variable: var T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (valueTypeOf(norm[0]) == norm[0]) {
                                return nullopt; // not a variable
                            }
                            return valueTypeOf(norm[0]);
                        }),
                },
            }),

        // A read of a variable that leaves its sole-reference mark set; the compiler writes it
        // in place of a `:var/get` whose value cannot be kept (Builder::markNonRetainingReads).
        OperatorGroup::create(
            "__var_peek__",
            {
                {
                    ":var/peek",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(variable: var T) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (valueTypeOf(norm[0]) == norm[0]) {
                                return nullopt; // not a variable
                            }
                            return valueTypeOf(norm[0]);
                        }),
                },
            }),

        // `xs = xs->sort` as a statement: the compiler writes these two groups around the
        // in-place form of the operator (Builder::inPlaceRebindingOf, __builtin__var_take__);
        // no program can name them. Arrays only: the mark they rely on is one of arrays.
        OperatorGroup::create(
            "__var_take__",
            {
                {
                    ":var/take",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {true}}},
                        "(var variable: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_owned__",
            {
                {
                    ":op/assn_owned",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: T[], value: T[]) => var T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array || !norm[0]->equals(norm[1]))
                                return nullopt;
                            return TupleType::cell(norm[0]);
                        }),
                },
            }),

        // ======= Assignment operators =======
        // `=` takes any type. A compound assignment takes exactly the types listed for it: a
        // combination without an implementation is rejected when the program is compiled.
        OperatorGroup::create(
            "__assn__",
            {
                {
                    ":op/assn_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        TupleType::cell(Type::String())),
                },
                {
                    ":op/assn_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        TupleType::cell(Type::Bool())),
                },
                {
                    ":op/assn",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {true, false}}},
                        "(var self: T, other: T) => var T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // The target keeps its declared type; the value only has to fit it.
                            if (!norm[0]->assignableFrom(norm[1])) {
                                return nullopt;
                            }
                            return TupleType::cell(norm[0]);
                        }),
                },
            }),
        OperatorGroup::create(
            "__assn_add__",
            {
                {
                    ":op/assn_add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), true}, {Type::String(), false}},
                        TupleType::cell(Type::String())),
                },
                {
                    ":op/assn_add_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_add_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                // bf16 and f16: `acc += x` on one of them (design principles 7.3).
                CAMEL_HALF_ASSIGN("assn_add"),
            }),
        OperatorGroup::create(
            "__assn_sub__",
            {
                {
                    ":op/assn_sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_sub_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_sub_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                CAMEL_HALF_ASSIGN("assn_sub"),
            }),
        OperatorGroup::create(
            "__assn_mul__",
            {
                {
                    ":op/assn_mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_mul_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_mul_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                CAMEL_HALF_ASSIGN("assn_mul"),
            }),
        OperatorGroup::create(
            "__assn_div__",
            {
                {
                    ":op/assn_div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_div_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_div_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                CAMEL_HALF_ASSIGN("assn_div"),
            }),
        OperatorGroup::create(
            "__assn_mod__",
            {
                {
                    ":op/assn_mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
            }),
        // No built-in type has `@`; a module that has it (tensor) provides `@=` or it is rejected.
        OperatorGroup::create("__assn_mat__", {}),
        OperatorGroup::create(
            "__assn_pow__",
            {
                {
                    ":op/assn_pow_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Int32())),
                },
                {
                    ":op/assn_pow_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Int64())),
                },
                {
                    ":op/assn_pow_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), true}, {Type::Float32(), false}},
                        TupleType::cell(Type::Float32())),
                },
                {
                    ":op/assn_pow_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), true}, {Type::Float64(), false}},
                        TupleType::cell(Type::Float64())),
                },
                {
                    ":op/assn_pow_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Complex64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_pow_c_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_pow_c_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Complex64())),
                },
                {
                    ":op/assn_pow_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Complex128(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                {
                    ":op/assn_pow_z_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Int32(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                {
                    ":op/assn_pow_z_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), true}, {Type::Int64(), false}},
                        TupleType::cell(Type::Complex128())),
                },
                CAMEL_HALF_ASSIGN("assn_pow"),
            }),
        OperatorGroup::create(
            "__assn_and__",
            {
                {
                    ":op/assn_and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        TupleType::cell(Type::Bool())),
                },
            }),
        OperatorGroup::create(
            "__assn_or__",
            {
                {
                    ":op/assn_or",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), true}, {Type::Bool(), false}},
                        TupleType::cell(Type::Bool())),
                },
            }),
        // ======= Logical operators =======
        OperatorGroup::create(
            "__and__",
            {
                {
                    ":op/and",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__or__",
            {
                {
                    ":op/or",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
            }),
        // ======= Comparison operators =======
        OperatorGroup::create(
            "__eq__",
            {
                {
                    ":op/eq_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Bool()),
                },
                {
                    ":op/eq",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: T, b: T) => bool, T an array, tuple or struct of comparable parts",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // By content (design principles 4.2): both sides of one type,
                            // an array, a tuple or a struct, every part of which is itself
                            // comparable (no function value, no tensor). Anything else has no
                            // overload, which is a located compile-time error.
                            const TypeCode code  = norm[0]->code();
                            const bool composite = code == TypeCode::Array ||
                                                   code == TypeCode::Struct ||
                                                   (code == TypeCode::Tuple &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            // An enum compares by its variant and then its fields.
                            const bool comparable = composite || asEnumType(norm[0]) != nullptr;
                            if (!comparable || !norm[0]->equals(norm[1]) ||
                                !isContentComparable(norm[0])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
                CAMEL_HALF_COMPARE("eq"),
            }),
        OperatorGroup::create(
            "__neq__",
            {
                {
                    ":op/ne_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_b",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Bool(), false}, {Type::Bool(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ne",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(a: T, b: T) => bool, T an array, tuple or struct of comparable parts",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // By content (design principles 4.2): both sides of one type,
                            // an array, a tuple or a struct, every part of which is itself
                            // comparable (no function value, no tensor). Anything else has no
                            // overload, which is a located compile-time error.
                            const TypeCode code  = norm[0]->code();
                            const bool composite = code == TypeCode::Array ||
                                                   code == TypeCode::Struct ||
                                                   (code == TypeCode::Tuple &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isCell() &&
                                                    !tt::as_ptr<TupleType>(norm[0])->isTagged());
                            // An enum compares by its variant and then its fields.
                            const bool comparable = composite || asEnumType(norm[0]) != nullptr;
                            if (!comparable || !norm[0]->equals(norm[1]) ||
                                !isContentComparable(norm[0])) {
                                return nullopt;
                            }
                            return Type::Bool();
                        }),
                },
                CAMEL_HALF_COMPARE("ne"),
            }),
        // `===` and `!==` are not supported for now (design principles 4.2): the front end
        // refuses them, so there is no operator behind them.
        // <、<=、>、>=
        OperatorGroup::create(
            "__lt__",
            {
                {
                    ":op/lt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/lt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                CAMEL_HALF_COMPARE("lt"),
            }),
        OperatorGroup::create(
            "__le__",
            {
                {
                    ":op/le_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/le_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                CAMEL_HALF_COMPARE("le"),
            }),
        OperatorGroup::create(
            "__gt__",
            {
                {
                    ":op/gt_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/gt_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                CAMEL_HALF_COMPARE("gt"),
            }),
        OperatorGroup::create(
            "__ge__",
            {
                {
                    ":op/ge_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Bool()),
                },
                {
                    ":op/ge_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::Bool()),
                },
                CAMEL_HALF_COMPARE("ge"),
            }),
        // ======= Arithmetic operators =======
        OperatorGroup::create(
            "__add__",
            {
                {
                    ":op/add_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/add_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/add_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/add_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/add_s",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::String(), false}, {Type::String(), false}},
                        Type::String()),
                },
                {
                    ":op/add_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/add_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_BINARY("add"),
            }),
        OperatorGroup::create(
            "__sub__",
            {
                {
                    ":op/sub_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/sub_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/sub_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/sub_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/sub_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/sub_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_BINARY("sub"),
            }),
        OperatorGroup::create(
            "__mul__",
            {
                {
                    ":op/mul_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mul_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/mul_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/mul_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/mul_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/mul_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_BINARY("mul"),
            }),
        OperatorGroup::create(
            "__div__",
            {
                {
                    ":op/div_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/div_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/div_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/div_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                {
                    ":op/div_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/div_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_BINARY("div"),
            }),
        OperatorGroup::create(
            "__mod__",
            {
                {
                    ":op/mod_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/mod_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "__pow__",
            {
                {
                    ":op/pow_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int32(), false}, {Type::Int32(), false}},
                        Type::Int32()),
                },
                {
                    ":op/pow_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        Type::Int64()),
                },
                {
                    ":op/pow_f",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float32(), false}, {Type::Float32(), false}},
                        Type::Float32()),
                },
                {
                    ":op/pow_d",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Float64(), false}, {Type::Float64(), false}},
                        Type::Float64()),
                },
                // A complex exponent is a complex number; an integer exponent is the repeated
                // squaring form (an integer literal exponent becomes a complex literal, and
                // the two give the same bits — see the runtime's complexPower).
                {
                    ":op/pow_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_c_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Int32(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_c_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}, {Type::Int64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/pow_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Complex128(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/pow_z_i",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Int32(), false}},
                        Type::Complex128()),
                },
                {
                    ":op/pow_z_l",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}, {Type::Int64(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_BINARY("pow"),
            }),
        OperatorGroup::create("__mat__", {}),
        OperatorGroup::create(
            "__idx__",
            {
                {
                    ":op/idx_arr",
std::make_shared<StaticCheckedResolver>(
                        DynamicFuncTypeResolver::create(
                            {{0, {}}, {2, {false, false}}},
                            "(arr: T[], index: int) => T",
                            [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                                -> optional<Type *> {
                                if (norm[0]->code() != TypeCode::Array ||
                                    norm[1]->code() != TypeCode::Int64)
                                    return nullopt;
                                Type *elem = tt::as_ptr<ArrayType>(norm[0])->elemType();
                                // An array whose element type is not fixed (`Tensor`) has no
                                // element type to name the value a subscript reads: this op
                                // answers with the element type, and the module that owns
                                // numeric arrays reads the element at the element type the
                                // value itself carries, which is what its own overload of
                                // `t[idx]` does (design principles 5.2). Declining here leaves
                                // the value to that overload.
                                if (elem == nullptr || elem->code() == TypeCode::Void) {
                                    return nullopt;
                                }
                                return elem;
                            }),
                        staticIndexRejection),
                },
                {
                    ":op/idx_take",
                    // `t[idx]` with an int array takes the rows the indices name, in that
                    // order (design principles 5.4; `take` in its function form).
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(arr: T[], indices: int[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array ||
                                norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &indices = tt::as_ptr<ArrayType>(norm[1]);
                            if (indices->elemType()->code() != TypeCode::Int64)
                                return nullopt;
                            // This operator takes the element at each index, one slot
                            // each, which is a rank-1 array's take. A numeric array is a
                            // tensor, and a tensor of another rank -- or of open rank
                            // (`Tensor<f64>` is `float[..]`, its runtime value deciding the
                            // rank) -- is taken along its first dimension, rows at a time:
                            // that is the tensor module's `tensor:idx_take`, which reads
                            // the result's shape from the indices and carries `take`'s
                            // adjoint. Declining those keeps the element take for the
                            // arrays no module operator serves -- `int[]`, `float[]`, an
                            // array of records -- and stops this operator from treating the
                            // rows of a rank-2 tensor's first dimension as its slots.
                            const auto &arr = tt::as_ptr<ArrayType>(norm[0]);
                            const bool rowTake =
                                isNumericArrayType(norm[0]) &&
                                (arr->anyRank() || arr->rank() != 1);
                            if (rowTake) {
                                return nullopt;
                            }
                            return norm[0];
                        }),
                },
                {
                    ":op/idx_str",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(str: string, index: int) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::String ||
                                norm[1]->code() != TypeCode::Int64)
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        // ======= Unary operators =======
        OperatorGroup::create(
            "__not__",
            {
                {
                    ":op/not",
                    StaticFuncTypeResolver::create({}, {{Type::Bool(), false}}, Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "__neg__",
            {
                {
                    ":op/neg_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/neg_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
                {
                    ":op/neg_f",
                    StaticFuncTypeResolver::create({}, {{Type::Float32(), false}}, Type::Float32()),
                },
                {
                    ":op/neg_d",
                    StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::Float64()),
                },
                {
                    ":op/neg_c",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex64(), false}},
                        Type::Complex64()),
                },
                {
                    ":op/neg_z",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Complex128(), false}},
                        Type::Complex128()),
                },
                CAMEL_HALF_UNARY("neg"),
            }),
        OperatorGroup::create(
            "__inv__",
            {
                {
                    ":op/inv_i",
                    StaticFuncTypeResolver::create({}, {{Type::Int32(), false}}, Type::Int32()),
                },
                {
                    ":op/inv_l",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Int64()),
                },
            }),
        // ======= IO (built-in input/print/println/eprintln) ======
        OperatorGroup::create(
            "input",
            {
                {
                    ":io/input",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(prompt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "print",
            {
                {
                    ":io/print",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "<fmt?: string> (x: T, ...rest: U...) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() > 0 && with[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            if (norm.size() < 1) {
                                return Type::Void();
                            }
                            return norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "println",
            {
                {
                    ":io/println",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "<fmt?: string> (x: T, ...rest: U...) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() > 0 && with[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            if (norm.size() < 1) {
                                return Type::Void();
                            }
                            return norm[0];
                        }),
                },
            }),
        // `println` on the standard error stream, with the same declaration shape (the author's
        // rule on question 2: it is in the prelude, so no import).
        OperatorGroup::create(
            "eprintln",
            {
                {
                    ":io/eprintln",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "<fmt?: string> (x: T, ...rest: U...) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() > 0 && with[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            if (norm.size() < 1) {
                                return Type::Void();
                            }
                            return norm[0];
                        }),
                },
            }),
        // ======= process (implemented in process.cmo) ======
        OperatorGroup::create(
            "sleep",
            {
                {
                    "process:sleep",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "exit",
            {
                {
                    "process:exit",
                    StaticFuncTypeResolver::create({}, {{Type::Int64(), false}}, Type::Void()),
                },
            }),
        // ======= string =======
        OperatorGroup::create(
            "format",
            {
                {
                    ":str/format",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {1, {false}}},
                        "<...args: T...> (fmt: string) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!norm[0]->equals(Type::String()))
                                return nullopt;
                            return Type::String();
                        }),
                },
            }),
        OperatorGroup::create(
            "join",
            {
                {
                    ":str/join",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<sep: string> (xs: string[]) => string",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!with[0]->equals(Type::String()))
                                return nullopt;
                            const auto &type = norm[0];
                            if (type->code() != TypeCode::Array) {
                                return nullopt;
                            }
                            const auto &arrType = tt::as_ptr<ArrayType>(type);
                            if (arrType->elemType()->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return Type::String();
                        }),
                },
            }),
        // ======= struct =======
        OperatorGroup::create(
            "len",
            {
                {
                    ":struct/len_str",
                    namedStaticSignature("x", Type::String(), Type::Int64()),
                },
                {
                    ":struct/len_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        // One position, one name: the collection rule below is `len(x: …)` too.
                        "<> (x: T[]) => int",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return Type::Int64();
                        }),
                },
            }),
        // The elements a `for` takes from a string (design principles 11.1): its code points,
        // each a one-character string. The loop builder converts a string to that array once,
        // before the first round; a program does not write `__iter__` itself. A map adds its
        // own overload (collection.cpp); the tensor module had one too, and it went with the P4
        // deletion step -- a numeric array is the core's to iterate (`t[i]` along the first
        // dimension), so no module takes part in a `for` over a tensor.
        OperatorGroup::create(
            "__iter__",
            {
                {
                    ":struct/iter_str",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (s: string) => string[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (!with.empty() || norm.size() != 1 ||
                                norm[0]->code() != TypeCode::String) {
                                return nullopt;
                            }
                            return ArrayType::create(Type::String());
                        }),
                },
            }),
        OperatorGroup::create(
            "zip",
            {
                {
                    ":struct/zip",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(...args: T[]) => (T, T, ...)[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm.size() < 2)
                                return nullopt;
                            for (const auto &t : norm) {
                                if (t->code() != TypeCode::Array)
                                    return nullopt;
                            }
                            type_vec_t tupleTypes;
                            for (size_t i = 0; i < norm.size(); i++) {
                                const auto &elemType = tt::as_ptr<ArrayType>(norm[i])->elemType();
                                tupleTypes.push_back(elemType);
                            }
                            return ArrayType::create(TupleType::create(std::move(tupleTypes)));
                        }),
                },
            }),
        OperatorGroup::create(
            "head",
            {
                {
                    ":struct/head_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (xs: T[]) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return arrType->elemType();
                        }),
                },
            }),
        OperatorGroup::create(
            "last",
            {
                {
                    ":struct/last_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (xs: T[]) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return arrType->elemType();
                        }),
                },
            }),
        OperatorGroup::create(
            "tail",
            {
                {
                    ":struct/tail_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "<> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return ArrayType::create(arrType->elemType());
                        }),
                },
            }),
        OperatorGroup::create(
            "range",
            {
                {
                    ":struct/range",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(start: int, end: int, step: int) => int[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            // `range(a, b)` is the interval of the integers from `a` to `b - 1`
                            // and `range(a, b, step)` is the same sequence with a step, as the
                            // language's `a..b` is the one without one (design principles 4.2;
                            // One rule for the two spellings:
                            // a second rule of the same URI would leave the table (which holds
                            // one resolver per URI) with the last one only, so every re-typing of
                            // the call would read the other spelling's arity.
                            if (norm.size() != 2 && norm.size() != 3) {
                                return nullopt;
                            }
                            for (Type *type : norm) {
                                if (type->code() != TypeCode::Int64) {
                                    return nullopt;
                                }
                            }
                            return ArrayType::create(Type::Int64());
                        }),
                },
            }),
        OperatorGroup::create(
            "slice",
            {
                {
                    ":struct/slice_arr",
                    DynamicFuncTypeResolver::create(
                        {{2, {false, false}}, {1, {false}}},
                        "<start: int, end: int> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Int64)
                                return nullopt;
                            if (with[1]->code() != TypeCode::Int64)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "__slice__",
            {
                {
                    ":op/slice_sub",
                    // `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]`: the slice of the first
                    // dimension a subscript writes (design principles 5.4). The last argument is
                    // a two-bit mask: bit 0 says whether the end was written (`t[a:]` runs to
                    // the end), bit 1 whether the start was (`xs[:: -1]` starts at the last
                    // element). A fixed-shape array's subscript is checked here, where its
                    // constants are (staticSliceRejection).
                    std::make_shared<StaticCheckedResolver>(
                        DynamicFuncTypeResolver::create(
                            {{0, {}}, {5, {false, false, false, false, false}}},
                            "(arr: T[], start: int, end: int, step: int, written: int) => T[]",
                            [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                                -> optional<Type *> {
                                if (norm[0]->code() != TypeCode::Array)
                                    return nullopt;
                                for (size_t i = 1; i < 5; ++i) {
                                    if (norm[i]->code() != TypeCode::Int64)
                                        return nullopt;
                                }
                                const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                                Type *elem         = arrType->elemType();
                                // An array whose element type is not fixed (`Tensor`) is sliced by
                                // the module that owns numeric arrays: it reads the elements at the
                                // element type the value carries, while the core's slice is the
                                // array of values whose elements the tangent space holds one slot
                                // each (design principles 5.4, 9.9). Declining here leaves the value
                                // to that module's own overload of `t[a:b]`.
                                if (elem == nullptr || elem->code() == TypeCode::Void) {
                                    return nullopt;
                                }
                                return norm[0];
                            }),
                        staticSliceRejection),
                },
            }),
        OperatorGroup::create(
            "concat",
            {
                {
                    ":struct/concat_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "<> (xs: T[], ys: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType      = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_ptr<ArrayType>(norm[1]);
                            if (!arrType->elemType()->equals(otherArrType->elemType()))
                                return nullopt;
                            return ArrayType::create(arrType->elemType());
                        }),
                },
            }),
        OperatorGroup::create(
            "append",
            {
                {
                    ":struct/append_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &eleType = arrType->elemType();
                            // A value of the element type, or an unnamed struct value whose
                            // fields are its fields in the same order (design principles 5.2):
                            // the append writes the slot as it is, and the layout agrees.
                            if (eleType != Type::Void() && !eleType->assignableFrom(with[0]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        // A private copy of an array: one of the states of a loop whose body only appends to it
        // (Builder::markLoopStateInPlace, which decides per state), so the literal the program
        // wrote stays a constant and the loop appends to its own array. Not a name a program can
        // reach for on its own.
        OperatorGroup::create(
            "__copy_arr__",
            {
                {
                    ":struct/copy_arr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(arr: T[]) => T[]",
                        [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            return norm[0];
                        }),
                },
            }),
        // `xs->append<v>` where the compiler proved the old array invisible: one state of a
        // loop body, with that state bound only to this call and its first value a fresh
        // literal (Builder::markLoopStateInPlace, per state). The append may then write into
        // the state's own Block (Array::canWriteInPlace) instead of copying the array; when
        // the array is not compact (an element that holds references) or the block is shared,
        // it behaves exactly as `append`. Not a name a program can reach for on its own.
        OperatorGroup::create(
            "__append_owned__",
            {
                {
                    ":struct/append_arr_owned",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &eleType = arrType->elemType();
                            if (eleType != Type::Void() && !eleType->assignableFrom(with[0]))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "extend",
            {
                {
                    ":struct/extend_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<other: T[]> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            if (with[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType      = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &otherArrType = tt::as_ptr<ArrayType>(with[0]);
                            if (!arrType->equals(otherArrType))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "contains",
            {
                {
                    ":struct/contains_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<value: T> (xs: T[]) => bool",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            Type *elem          = arrType->elemType();
                            // The value is a value of the element type (design principles 5.2):
                            // an unnamed struct value whose fields are the element type's
                            // fields, in the same order, is read as that type without moving
                            // anything. A value of another named type is not (an `as` says it);
                            // nor is one whose fields are written in another order, which the
                            // call would have to copy and this signature cannot ask for.
                            if (!elem->assignableFrom(with[0]))
                                return nullopt;
                            return Type::Bool();
                        }),
                },
                {
                    ":struct/contains_str",
                    // The names its declaration writes (`with <sub: string>`, `contains(s)`): the
                    // collection form works on an element, this one on a substring, so the two
                    // positions hold different things and are named accordingly. `contains` is a
                    // group whose rules therefore disagree about what position 0 is called, and a
                    // named argument is refused rather than bound to a position that means
                    // another thing in this rule (docs/stdlib-reference.md, "Named arguments").
                    [] {
                        FunctionTypeFactory factory;
                        factory.addWithArg("sub", Type::String(), false);
                        factory.addNormArg("s", Type::String(), false);
                        factory.setExitType(Type::Bool());
                        return StaticFuncTypeResolver::create(factory.build());
                    }(),
                },
            }),
        // ======= Marked Operators =======
        OperatorGroup::create(
            "map",
            {
                {
                    ":mark/map_arr",
                    withFixedSignature(
                        DynamicFuncTypeResolver::create(
                            {{1, {false}}, {1, {false}}},
                            "<f: (item: T) => U> (xs: T[]) => U[]",
                            [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                                -> optional<Type *> {
                                if (norm[0]->code() != TypeCode::Array)
                                    return nullopt;
                                const auto &arrType  = tt::as_ptr<ArrayType>(norm[0]);
                                const auto &elemType = arrType->elemType();
                                if (with[0]->code() != TypeCode::Function)
                                    return nullopt;
                                const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                                if (funcType->normTypesCount() != 1)
                                    return nullopt;
                                if (!funcType->normTypeAt(0)->equals(elemType))
                                    return nullopt;
                                ASSERT(funcType->hasExitType(), "Function must have exit type");
                                return ArrayType::create(funcType->exitType());
                            }),
                        mapRuleSignature()),
                },
            }),
        OperatorGroup::create(
            "apply",
            {
                {
                    ":mark/apply_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => T> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType  = tt::as_ptr<ArrayType>(norm[0]);
                            const auto &elemType = arrType->elemType();
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(elemType))
                                return nullopt;
                            if (!funcType->exitType()->equals(elemType))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "filter",
            {
                {
                    ":mark/filter_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => bool> (xs: T[]) => T[]",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            // The predicate is the one with-argument (it was read at
                            // index 1, past the end: every use of filter crashed).
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(arrType->elemType()))
                                return nullopt;
                            if (!funcType->hasExitType() ||
                                !funcType->exitType()->equals(Type::Bool()))
                                return nullopt;
                            return arrType;
                        }),
                },
            }),
        OperatorGroup::create(
            "reduce",
            {
                {
                    ":mark/reduce_arr",
                    withFixedSignature(
                        DynamicFuncTypeResolver::create(
                            {{2, {false, false}}, {1, {false}}},
                            "<f: (acc: U, item: T) => U, initial: U> (xs: T[]) => U",
                            [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                                -> optional<Type *> {
                                if (norm[0]->code() != TypeCode::Array)
                                    return nullopt;
                                const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                                if (with[0]->code() != TypeCode::Function)
                                    return nullopt;
                                const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                                if (funcType->normTypesCount() < 2)
                                    return nullopt;
                                // The accumulator's parameter type is the result type, and the
                                // elements, the initial value and the callback's result must all be
                                // values of it. An unnamed struct value whose fields are that
                                // type's fields in the same order is one (design principles 5.2);
                                // the call converts it, as `enterStructParameters` does for the
                                // initial value.
                                Type *acc = funcType->normTypeAt(0);
                                if (!funcType->normTypeAt(1)->assignableFrom(arrType->elemType()))
                                    return nullopt;
                                if (!acc->assignableFrom(with[1]))
                                    return nullopt;
                                if (!acc->assignableFrom(funcType->exitType()))
                                    return nullopt;
                                return acc;
                            }),
                        reduceRuleSignature()),
                },
            }),
        OperatorGroup::create(
            "foreach",
            {
                {
                    ":mark/foreach_arr",
                    DynamicFuncTypeResolver::create(
                        {{1, {false}}, {1, {false}}},
                        "<f: (item: T) => void> (xs: T[]) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Array)
                                return nullopt;
                            const auto &arrType = tt::as_ptr<ArrayType>(norm[0]);
                            if (with[0]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &funcType = tt::as_ptr<FunctionType>(with[0]);
                            if (funcType->normTypesCount() != 1)
                                return nullopt;
                            if (!funcType->normTypeAt(0)->equals(arrType->elemType()))
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        // ======= Diagnostics =======
        // `std::mem::blocks()`: the block counters as a struct, readable between two
        // operations (docs/design-principles 10.5; the allocation-count assertions). Declared
        // with effects below: the counters are not a function of the arguments.
        OperatorGroup::create(
            "std::mem::blocks",
            {{":op/mem_blocks", StaticFuncTypeResolver::create({}, {}, memBlocksStructType())}}),
        // ======= Macros =======
        OperatorGroup::create(
            "__cmp__",
            {
                {
                    ":macro/cmp",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(lhs: (arg1: T1, arg2: T2, ...) => R, rhs: (arg: R) => U) => U",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm[0]->code() != TypeCode::Function)
                                return nullopt;
                            if (norm[1]->code() != TypeCode::Function)
                                return nullopt;
                            const auto &lhsFuncType   = tt::as_ptr<FunctionType>(norm[0]);
                            const auto &rhsFuncType   = tt::as_ptr<FunctionType>(norm[1]);
                            const auto &lhsReturnType = lhsFuncType->exitType();
                            if (rhsFuncType->normTypesCount() != 1)
                                return nullopt;
                            if (!rhsFuncType->normTypeAt(0)->equals(lhsReturnType))
                                return nullopt;
                            return rhsFuncType->exitType();
                        }),
                },
            }),
    };
    return groups;
}

GlobalsBuiltinModule::GlobalsBuiltinModule(context_ptr_t ctx) : BuiltinModule("", ctx) {
    registerBuiltinOperatorTraits();
    registerBuiltinDerivatives();
    // The operators over Map and Set (design principles 11.3). Those named like a group of
    // this module (`len`, `contains`, `__idx__`, `__eq__`, `__neq__`) are further overloads
    // of it: one group is exported per name, the existing overloads first.
    std::vector<oper_group_ptr_t> collectionGroups = getCollectionOperatorGroups();
    for (const auto &group : getGlobalOperatorGroups()) {
        const auto extra = std::find_if(
            collectionGroups.begin(),
            collectionGroups.end(),
            [&](const oper_group_ptr_t &other) { return other->name() == group->name(); });
        if (extra == collectionGroups.end()) {
            exportEntity(group->name(), group);
            continue;
        }
        auto resolvers = group->resolvers();
        resolvers.insert(
            resolvers.end(),
            (*extra)->resolvers().begin(),
            (*extra)->resolvers().end());
        exportEntity(group->name(), OperatorGroup::create(group->name(), resolvers));
        collectionGroups.erase(extra);
    }
    for (const auto &group : collectionGroups) {
        exportEntity(group->name(), group);
    }
    // The operators over T? and T! (design principles 3.8).
    for (const auto &group : getTaggedOperatorGroups()) {
        exportEntity(group->name(), group);
    }
    // The lowering targets of the enum (design principles 5.3).
    for (const auto &group : getEnumOperatorGroups()) {
        exportEntity(group->name(), group);
    }
    // The `set` operators a path assignment lowers to (design principles 3.5).
    for (const auto &group : getSetValueOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool GlobalsBuiltinModule::load() {
    CAMEL_LOG_INFO_S("GlobalsBuiltinModule", "Loading basic built-in module.");
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("", [ctx = context_]() {
        return BasicBuiltinExecutor::create(ctx);
    });
    // The native operators of `array`, the library written in Camel (`stdlib/array.cml`). A
    // module that has bodies cannot also be a `.cmo` module of the same name: the compiler
    // would read `array.cml` as a declaration module and reject the bodies. So the kernels are
    // built into libcamel (`builtin/operators/array.cpp`), and this module answers them on the
    // `array` protocol; the body-less declarations in `array.cml` are their signatures.
    context_->registerExecutorFactory("array", [ctx = context_]() {
        return std::make_shared<Executor>(ctx, getArrayOpsMap());
    });
    declareArrayOperatorFacts();
    loaded_ = true;
    return true;
}
