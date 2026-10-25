// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/array_type_selftest.h"

#include "camel/core/data/composite/array.h"
#include "camel/core/data/primary.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace camel::core::type {

namespace {

[[noreturn]] void fail(const std::string &what) {
    throw std::runtime_error("array type self-test: " + what);
}

void require(bool condition, const std::string &what) {
    if (!condition) {
        fail(what);
    }
}

std::vector<ExtentForm> constants(std::initializer_list<int64_t> values) {
    std::vector<ExtentForm> forms;
    forms.reserve(values.size());
    for (int64_t value : values) {
        forms.push_back(ExtentForm::constant(value));
    }
    return forms;
}

std::vector<int64_t> shapeOf(std::initializer_list<int64_t> values) {
    return std::vector<int64_t>(values);
}

ExtentForm named(const char *name) { return ExtentForm::linear(0, {{name, 1}}); }

/// The formatted runtime diagnostic of a conversion that must fail.
std::string castFailure(const Type *target, slot_t value, Type *source) {
    try {
        (void)target->castSlotFrom(value, source);
    } catch (const camel::core::error::RuntimeFault &fault) {
        return camel::core::error::runtimeDiagMessage(fault.diag(), fault.formatArgs());
    }
    fail("a conversion that must fail at run time succeeded: " + target->toString());
}

} // namespace

std::string debugRunArrayTypeSelfTest() {
    using camel::core::error::RuntimeFault;

    Type *i64 = Type::Int64();
    Type *f32 = Type::Float32();
    Type *str = Type::String();

    // ---- One interned object per shape, and the two spellings of one shape ----------------
    ArrayType *empty   = ArrayType::create(i64);
    ArrayType *unknown = ArrayType::create(i64, std::vector<ExtentForm>{ExtentForm::unknown()});
    require(empty == unknown, "int[] and int[?] are not the same interned type");
    require(empty == ArrayType::create(i64, std::vector<int64_t>{}), "int[] from an empty list");
    ArrayType *fromInts = ArrayType::create(f32, shapeOf({3, 4}));
    ArrayType *fromForms = ArrayType::create(f32, constants({3, 4}));
    require(fromInts == fromForms, "int[3, 4] written two ways is not one interned type");
    require(fromInts != ArrayType::create(f32, shapeOf({3, 5})), "f32[3, 4] equals f32[3, 5]");
    require(
        fromInts != ArrayType::create(f32, shapeOf({3})), "f32[3, 4] equals f32[3] (rank)");
    require(ArrayType::create(f32, shapeOf({3})) != ArrayType::create(f32, shapeOf({4})), "f32[3] equals f32[4]");
    require(
        ArrayType::create(f32, std::vector<int64_t>{}) == ArrayType::create(f32),
        "f32[] from an empty list is not f32[]");
    require(fromInts->rank() == 2 && !fromInts->anyRank(), "rank of f32[3, 4]");
    // The name that awaits its element type is one interned object, and it is not an array that
    // states a shape: `Tensor` alone and `f32[3]` are different objects.
    require(
        ArrayType::elementTypeOpen() == ArrayType::elementTypeOpen(),
        "the name awaiting an element type is not interned");
    require(
        ArrayType::elementTypeOpen() != ArrayType::create(f32, shapeOf({3})),
        "the name awaiting an element type equals f32[3]");
    // A shape that cannot be normalized is refused, not silently kept.
    bool refused = false;
    try {
        (void)ArrayType::create(f32, shapeOf({-1}));
    } catch (const std::invalid_argument &) {
        refused = true;
    }
    require(refused, "a negative extent was accepted");

    // ---- Printing and mangling ------------------------------------------------------------
    require(empty->toString() == "int[]", "int[] prints as '" + empty->toString() + "'");
    require(fromInts->toString() == "f32[3, 4]", "f32[3, 4] prints as '" + fromInts->toString() + "'");
    require(
        ArrayType::create(f32, std::vector<ExtentForm>{ExtentForm::unknown(), ExtentForm::constant(3)})
                ->toString() == "f32[?, 3]",
        "an unknown extent does not print as '?' beside a constant");
    require(
        ArrayType::create(f32, constants({3}), "rest")->toString() == "f32[3, ..rest]",
        "an open rank prints its pack");
    require(
        ArrayType::openRank(f32)->toString() == "f32[..]", "a bare open rank prints as f32[..]");
    require(
        ArrayType::elementTypeOpen()->toString() == "number[..]",
        "the name awaiting its element type prints as '" + ArrayType::elementTypeOpen()->toString() +
            "'");
    require(
        ArrayType::elementTypeOpen()->mangle() == "Array_open_any",
        "the mangle of the name awaiting its element type changed: " +
            ArrayType::elementTypeOpen()->mangle());
    // The element type nobody stated is `Void`, and it is not the type of an array literal: the
    // literal states one unknown dimension, this name states an open rank.
    require(
        isUntypedArrayType(ArrayType::elementTypeOpen()) &&
            isElementTypeOpenArray(ArrayType::elementTypeOpen()) &&
            !isElementTypeOpenArray(ArrayType::create(nullptr)),
        "the name awaiting an element type is not told from the empty literal's type");
    require(empty->mangle() == "Array_i_1_?", "the int[] mangle changed: " + empty->mangle());
    require(
        fromInts->mangle() == "Array_f32_2_3,4",
        "the f32[3, 4] mangle changed: " + fromInts->mangle());

    // ---- Equality ignores dimension names (as the tensor type's did) ----------------------
    ArrayType *namedShape =
        ArrayType::create(f32, std::vector<ExtentForm>{named("n"), ExtentForm::constant(3)});
    ArrayType *plainShape =
        ArrayType::create(f32, std::vector<ExtentForm>{ExtentForm::unknown(), ExtentForm::constant(3)});
    require(namedShape != plainShape, "f32[n, 3] and f32[?, 3] are the same object");
    require(namedShape->equals(plainShape), "f32[n, 3] does not equal f32[?, 3]");
    require(namedShape->mangle() == plainShape->mangle(), "a dimension name leaks into the mangle");
    require(!namedShape->equals(ArrayType::create(f32, shapeOf({3, 4}))), "f32[n, 3] equals f32[3, 4]");
    require(
        ArrayType::create(f32, constants({3}), "rest")
            ->equals(ArrayType::openRank(f32, constants({3}))),
        "a pack name is part of equality");
    require(!empty->equals(ArrayType::openRank(i64)), "int[] equals int[..]");
    require(!empty->equals(nullptr), "int[] equals null");

    // ---- Assignability: what is implicit, and the falsifying refusals ---------------------
    ArrayType *anyF32  = ArrayType::create(f32);
    ArrayType *fixed4  = ArrayType::create(f32, shapeOf({4}));
    require(anyF32->assignableFrom(fixed4), "f32[] does not accept f32[4] (unknown extent)");
    require(!anyF32->assignableFrom(fromInts), "f32[] accepts f32[3, 4] (rank differs)");
    require(!anyF32->assignableFrom(ArrayType::create(i64, shapeOf({4}))), "f32[] accepts int[4]");
    require(!fromInts->assignableFrom(anyF32), "f32[3, 4] accepts f32[]");
    require(fromInts->assignableFrom(ArrayType::create(f32, shapeOf({3, 4}))), "f32[3, 4] for itself");
    require(!fromInts->assignableFrom(ArrayType::create(f32, shapeOf({3, 5}))), "f32[3, 4] accepts f32[3, 5]");
    require(!fromInts->assignableFrom(ArrayType::create(f32, shapeOf({3}))), "f32[3, 4] accepts f32[3]");
    ArrayType *openF32 = ArrayType::openRank(f32);
    require(openF32->assignableFrom(fromInts), "f32[..] does not accept f32[3, 4]");
    require(openF32->assignableFrom(fixed4), "f32[..] does not accept f32[4]");
    ArrayType *open3 = ArrayType::openRank(f32, constants({3}));
    require(open3->assignableFrom(fromInts), "f32[3, ..] does not accept f32[3, 4]");
    require(!open3->assignableFrom(ArrayType::create(f32, shapeOf({4, 5}))), "f32[3, ..] accepts f32[4, 5]");
    require(!open3->assignableFrom(anyF32), "f32[3, ..] accepts an argument of unknown extents");
    // The name that awaits its element type takes no value: it is not a type of anything, and the
    // array whose element type nobody stated does not flow into it either.
    require(
        !ArrayType::elementTypeOpen()->assignableFrom(ArrayType::create(f32, shapeOf({3}))),
        "the name awaiting an element type accepts f32[3]");
    require(
        ArrayType::elementTypeOpen()->castSafetyFrom(ArrayType::create(f32, shapeOf({3}))) ==
            CastSafety::Forbidden,
        "the name awaiting an element type is not Forbidden as a conversion target");

    // ---- The conversion: Safe, Unsafe (checked at run time), Forbidden --------------------
    require(anyF32->castSafetyFrom(fixed4) == CastSafety::Safe, "f32[] from f32[4] is not Safe");
    require(fixed4->castSafetyFrom(anyF32) == CastSafety::Unsafe, "f32[4] from f32[] is not Unsafe");
    require(openF32->castSafetyFrom(fromInts) == CastSafety::Safe, "f32[..] from f32[3, 4] is not Safe");
    require(open3->castSafetyFrom(fromInts) == CastSafety::Safe, "f32[3, ..] from f32[3, 4] is not Safe");
    require(
        fromInts->castSafetyFrom(anyF32) == CastSafety::Forbidden,
        "f32[3, 4] from f32[] is not Forbidden (rank)");
    require(
        fromInts->castSafetyFrom(ArrayType::create(f32, shapeOf({3}))) == CastSafety::Forbidden,
        "f32[3, 4] from f32[3] is not Forbidden");
    require(
        fromInts->castSafetyFrom(ArrayType::create(f32, shapeOf({3, 5}))) == CastSafety::Forbidden,
        "f32[3, 4] from f32[3, 5] is not Forbidden");
    require(
        fromInts->castSafetyFrom(ArrayType::create(i64, shapeOf({3, 4}))) == CastSafety::Forbidden,
        "f32[3, 4] from int[3, 4] is not Forbidden");
    require(
        fromInts->castSafetyFrom(ArrayType::create(f32, shapeOf({3, 4}))) == CastSafety::Safe,
        "f32[3, 4] from itself is not Safe");

    // ---- The checked conversion at run time, including the moved shape check --------------
    Array* value6 = Array::create(mm::autoSpace(), ArrayType::create(f32, shapeOf({6})), shapeOf({6}), true);
    slot_t slot6 = camel::core::rtdata::toSlot(value6);
    ArrayType *wanted4 = ArrayType::create(f32, shapeOf({4}));
    Array* value4 = Array::create(mm::autoSpace(), wanted4, shapeOf({4}), true);
    slot_t slot4 = camel::core::rtdata::toSlot(value4);
    require(
        wanted4->castSlotFrom(slot4, anyF32) == slot4,
        "a value of the right shape was not passed through");
    const std::string movedCheck = castFailure(wanted4, slot6, anyF32);
    require(
        movedCheck.find("conversion to 'f32[4]' failed: the array has shape [6]") !=
            std::string::npos,
        "the moved shape check says: " + movedCheck);
    // A conversion of the right rank but wrong dims, and one of the wrong rank.
    Array *value23 = Array::create(
        mm::autoSpace(), ArrayType::create(f32, shapeOf({2, 3})), shapeOf({2, 3}), true);
    const std::string rankCheck = castFailure(wanted4, camel::core::rtdata::toSlot(value23), anyF32);
    require(
        rankCheck.find("conversion to 'f32[4]' failed: the array has shape [2, 3]") !=
            std::string::npos,
        "the runtime rank check says: " + rankCheck);
    // A target that states an element type checks the value's own storage: the static type of an
    // unsafe conversion says the value may fit, and what it holds is what settles it.
    Array *intValue = Array::create(mm::autoSpace(), ArrayType::create(i64), shapeOf({4}), true);
    const std::string elementCheck =
        castFailure(wanted4, camel::core::rtdata::toSlot(intValue), anyF32);
    require(
        elementCheck.find("conversion to 'f32[4]' failed: the array has element type") !=
            std::string::npos,
        "the runtime element check says: " + elementCheck);

    // ---- Unification and widening ---------------------------------------------------------
    Type *joined = ArrayType::create(i64, shapeOf({3}))->unify(ArrayType::create(i64, shapeOf({4})));
    require(joined != nullptr && joined->toString() == "int[]", "int[3] + int[4] does not unify to int[?]");
    require(
        ArrayType::create(i64, shapeOf({3}))->unify(ArrayType::create(i64, shapeOf({3}))) ==
            ArrayType::create(i64, shapeOf({3})),
        "equal types do not unify to themselves");
    // Two element types with no common type have no common array either: the element type of a
    // value is in its type, so there is no wider array for the branch to join on (design
    // principles 5.1). The join is refused rather than answered with an array whose element type
    // nobody states.
    Type *numericJoin = ArrayType::create(i64, shapeOf({3}))->unify(ArrayType::create(f32, shapeOf({3})));
    require(
        numericJoin == nullptr,
        "int[3] + f32[3] has a common type: " +
            (numericJoin ? numericJoin->toString() : std::string("<null>")));
    Type *rankJoin = ArrayType::create(i64, shapeOf({3}))->unify(ArrayType::create(i64, shapeOf({3, 4})));
    require(
        rankJoin != nullptr && rankJoin->toString() == "int[..]",
        "int[3] + int[3, 4] does not unify to an open rank: " +
            (rankJoin ? rankJoin->toString() : std::string("<null>")));
    require(ArrayType::create(i64)->unify(i64) == nullptr, "int[] unified with int");
    require(
        ArrayType::elementTypeOpen()->unify(ArrayType::create(i64, shapeOf({3}))) == nullptr,
        "the name awaiting an element type unified with int[3]");

    Type *widened23 = ArrayType::create(f32, shapeOf({3, 4}))->widened();
    require(
        widened23->toString() == "f32[?, ?]",
        "widening f32[3, 4] gives '" + widened23->toString() + "'");
    require(
        static_cast<ArrayType *>(widened23)->rank() == 2 && !static_cast<ArrayType *>(widened23)->anyRank(),
        "widening f32[3, 4] changed the rank");
    require(widened23->widened() == widened23, "widening an already widened array changes it");
    require(
        namedShape->widened()->toString() == "f32[?, ?]", "widening keeps a dimension name");
    require(
        ArrayType::openRank(f32, constants({3}))->widened()->toString() == "f32[?, ..]",
        "widening an open rank drops it");

    // ---- Call-site binding: names and packs, and a name fixed by a later argument ----------
    ExtentValues values;
    ExtentPacks packs;
    std::string mismatch;
    const ArrayType *paramRows =
        ArrayType::create(f32, std::vector<ExtentForm>{named("n")}, "rest");
    const ArrayType *paramRow = ArrayType::create(f32, std::vector<ExtentForm>{named("n")});
    require(
        bindExtents(paramRows, fromInts, values, packs, mismatch) == ExtentBinding::Bound,
        "f32[n, ..rest] did not bind from f32[3, 4]");
    require(values["n"] == 3, "n did not bind to 3");
    require(
        packs["rest"].size() == 1 && packs["rest"][0].constant() == 4,
        "the pack did not bind the argument's remaining dimensions");
    require(
        bindExtents(paramRow, ArrayType::create(f32, shapeOf({3})), values, packs, mismatch) ==
            ExtentBinding::Bound,
        "a second argument mentioning n did not bind");
    require(
        bindExtents(paramRow, ArrayType::create(f32, shapeOf({4})), values, packs, mismatch) ==
            ExtentBinding::Mismatch,
        "n = 3 and an argument f32[4] were not a mismatch");
    require(mismatch == "4", "the mismatch does not report the argument's extent: " + mismatch);
    require(
        bindExtents(paramRow, anyF32, values, packs, mismatch) == ExtentBinding::Bound,
        "an unknown argument extent is a mismatch");
    require(
        ArrayType::create(f32, std::vector<ExtentForm>{named("n")}, "rest")
                ->withExtentValues(values, packs)
                ->toString() == "f32[3, 4]",
        "the declared f32[n, ..rest] did not refine to f32[3, 4]");
    // A bare pack bound to no dimensions denotes the element type (Q1's scalar fold). The
    // binding records it as an empty list: an argument of exactly the prefix's rank.
    ExtentValues foldValues;
    ExtentPacks foldPacks;
    require(
        bindExtents(paramRows, ArrayType::create(f32, shapeOf({3})), foldValues, foldPacks, mismatch) ==
            ExtentBinding::Bound,
        "an argument of the prefix's rank did not bind");
    require(
        foldPacks.count("rest") == 1 && foldPacks["rest"].empty(),
        "the pack of a shorter argument is absent instead of empty");
    require(
        ArrayType::create(f32, std::vector<ExtentForm>{}, "rest")
                ->withExtentValues(foldValues, foldPacks) == f32,
        "a bare pack bound to no dimensions is not the element type");
    // A pack is not bound when the argument's own rank is open; the conversion checks it.
    ExtentPacks openPacks;
    require(
        bindExtents(paramRows, ArrayType::openRank(f32, constants({3})), values, openPacks, mismatch) ==
            ExtentBinding::Bound,
        "an open-rank argument did not bind its prefix");
    require(openPacks.empty(), "a pack was bound from an argument whose rank is open");
    // A parameter without names binds nothing: the ordinary argument check stays in charge.
    require(
        bindExtents(anyF32, fromInts, values, packs, mismatch) == ExtentBinding::NotDimensioned,
        "a parameter without names was treated as dimensioned");

    // The dimension comes from another argument: `f(a: f32[?, n], b: f32[n])` with an `a`
    // whose second dimension the run knows and a `b` that fixes n. `a` is then Unsafe, not
    // implicit, and the conversion checks the dimension the run has.
    ExtentValues later;
    ExtentPacks laterPacks;
    const ArrayType *paramA =
        ArrayType::create(f32, std::vector<ExtentForm>{ExtentForm::unknown(), named("n")});
    ArrayType *argA =
        ArrayType::create(f32, std::vector<ExtentForm>{ExtentForm::unknown(), ExtentForm::unknown()});
    require(
        bindExtents(paramA, argA, later, laterPacks, mismatch) == ExtentBinding::Bound &&
            later.empty(),
        "an unknown argument dimension bound a name");
    require(
        bindExtents(paramRow, ArrayType::create(f32, shapeOf({7})), later, laterPacks, mismatch) ==
            ExtentBinding::Bound &&
            later["n"] == 7,
        "the second argument did not fix n");
    const Type *refinedA = paramA->withExtentValues(later, laterPacks);
    require(refinedA->toString() == "f32[?, 7]", "the refined type is '" + refinedA->toString() + "'");
    require(!refinedA->assignableFrom(argA), "a dimension fixed by another argument was implicit");
    require(refinedA->castSafetyFrom(argA) == CastSafety::Unsafe, "the conversion is not checked");
    Array* value47 = Array::create(
        mm::autoSpace(), ArrayType::create(f32, shapeOf({4, 7})), shapeOf({4, 7}), true);
    require(
        refinedA->castSlotFrom(camel::core::rtdata::toSlot(value47), argA) ==
            camel::core::rtdata::toSlot(value47),
        "the refined f32[?, 7] refused a f32[4, 7] value");
    Array* value46 = Array::create(
        mm::autoSpace(), ArrayType::create(f32, shapeOf({4, 6})), shapeOf({4, 6}), true);
    const std::string laterCheck = castFailure(refinedA, camel::core::rtdata::toSlot(value46), argA);
    require(
        laterCheck.find("conversion to 'f32[?, 7]' failed: the array has shape [4, 6]") !=
            std::string::npos,
        "the check of a dimension from another argument says: " + laterCheck);

    // ---- The array that awaits its element type: what a name that requires one denotes --------
    // That array is the open-rank array whose element type nobody stated, and its arguments are the
    // array type's own: one element type, then one shape. Every spelling it accepts denotes the
    // interned array object the equivalent array spelling gives, and the element type is required
    // (docs/design/arrays-memory/plan.cn.md section 5.1a).
    ArrayType *alias = ArrayType::elementTypeOpen();
    std::string argumentError;
    const auto applyOne = [&](const TypeArgument &argument) {
        return alias->applyArguments(std::span<const TypeArgument>(&argument, 1), argumentError);
    };
    const auto element = [&](Type *type) {
        return applyOne(TypeArgument{.type = type, .data = nullptr, .extents = nullptr});
    };
    const auto both = [&](Type *type, std::initializer_list<int64_t> values) {
        std::vector<TypeArgument> arguments{
            TypeArgument{.type = type, .data = nullptr, .extents = nullptr},
            TypeArgument{
                .type = nullptr,
                .data = nullptr,
                .extents = std::make_shared<const std::vector<ExtentForm>>(constants(values))}};
        return alias->applyArguments(std::span<const TypeArgument>(arguments), argumentError);
    };
    require(element(f32) == ArrayType::openRank(f32), "f32[..] is not f32[..]");
    require(
        element(f32)->toString() == "f32[..]",
        "f32[..] prints as '" + element(f32)->toString() + "'");
    require(element(i64) == ArrayType::openRank(i64), "int[..] is not int[..]");
    // A type variable is the element type a generic signature writes: `func f::<T>(x: T[..])`
    // is `T[..]`, and the call's argument fixes `T` (design principles 5.5).
    require(element(TypeVarType::create("T", false)) == ArrayType::openRank(TypeVarType::create("T", false)),
            "T[..] is not T[..]");
    require(
        both(f32, {2, 3}) == ArrayType::create(f32, shapeOf({2, 3})),
        "f32[2, 3] is not f32[2, 3]");
    require(
        static_cast<const ArrayType *>(both(f32, {2, 3}))->rank() == 2,
        "f32[2, 3] did not keep its rank");
    // Every dimension removed: the type is its element type (the rank-zero rule in
    // docs/handover/reviews/questions-2026-10-23.md, section rank-zero-tensor): `f32`
    // is `f32`, and not a rank-zero array.
    require(both(f32, {}) == f32, "f32 is not f32");
    require(!element(f32)->equals(f32), "f32[..] with no shape is the scalar");
    // A shape written over dimension names keeps them, and they do not take part in equality.
    std::vector<TypeArgument> namedArguments{
        TypeArgument{.type = f32, .data = nullptr, .extents = nullptr},
        TypeArgument{
            .type = nullptr,
            .data = nullptr,
            .extents = std::make_shared<const std::vector<ExtentForm>>(
                std::vector<ExtentForm>{named("n"), ExtentForm::constant(784)})}};
    const ArrayType *namedTensor = static_cast<const ArrayType *>(
        alias->applyArguments(std::span<const TypeArgument>(namedArguments), argumentError));
    std::vector<ExtentForm> keptForms;
    require(
        namedTensor->rank() == 2 && namedTensor->extentForms(keptForms) &&
            keptForms.size() == 2 && keptForms[0].hasNames() &&
            keptForms[0].terms().front().first == "n" && keptForms[1].constant() == 784,
        "f32[n, 784] did not keep the dimension name");
    require(
        namedTensor->equals(ArrayType::create(
            f32, std::vector<ExtentForm>{ExtentForm::unknown(), ExtentForm::constant(784)})),
        "a dimension name takes part in the equality of an array");
    // A constant shape, the spelling the tensor shape had before the array type took shapes
    // over: an integer list, where a negative extent is the unknown dimension it always was.
    const std::shared_ptr<camel::core::data::ArrayData> constShapeData =
        camel::core::data::ArrayData::create(
            Type::Int32(),
            {std::make_shared<camel::core::data::IntData>(3),
             std::make_shared<camel::core::data::IntData>(-1)});
    std::vector<TypeArgument> constShape{
        TypeArgument{.type = f32, .data = nullptr, .extents = nullptr},
        TypeArgument{.type = nullptr, .data = constShapeData, .extents = nullptr}};
    require(
        alias->applyArguments(std::span<const TypeArgument>(constShape), argumentError) ==
            ArrayType::create(
                f32,
                std::vector<ExtentForm>{ExtentForm::constant(3), ExtentForm::unknown()}),
        "the constant shape [3, -1] is not f32[3, ?]");

    // What the name refuses: a spelling with no element type at all, an element type that is not
    // a numeric scalar, a second element type, a second shape, and an element after the shape.
    const auto refuses = [&](std::span<const TypeArgument> arguments, const std::string &what) {
        argumentError.clear();
        const Type *result = alias->applyArguments(arguments, argumentError);
        require(result == nullptr, what + " was accepted");
        require(!argumentError.empty(), what + " was refused without a reason");
    };
    std::vector<TypeArgument> notNumeric{
        TypeArgument{.type = str, .data = nullptr, .extents = nullptr}};
    refuses(notNumeric, "string[..]");
    require(
        argumentError.find("numeric scalar") != std::string::npos,
        "the refusal of string[..] says: " + argumentError);
    std::vector<TypeArgument> twoElements{
        TypeArgument{.type = f32, .data = nullptr, .extents = nullptr},
        TypeArgument{.type = f32, .data = nullptr, .extents = nullptr}};
    refuses(twoElements, "two element types");
    std::vector<TypeArgument> elementAfterShape{
        TypeArgument{
            .type = nullptr,
            .data = nullptr,
            .extents = std::make_shared<const std::vector<ExtentForm>>(constants({3}))},
        TypeArgument{.type = f32, .data = nullptr, .extents = nullptr}};
    refuses(elementAfterShape, "an element type after the shape");
    std::vector<TypeArgument> twoShapes{
        TypeArgument{
            .type = nullptr,
            .data = nullptr,
            .extents = std::make_shared<const std::vector<ExtentForm>>(constants({3}))},
        TypeArgument{
            .type = nullptr,
            .data = nullptr,
            .extents = std::make_shared<const std::vector<ExtentForm>>(constants({4}))}};
    refuses(twoShapes, "two shapes");
    // A shape with no element type: the question the name exists to answer is left open, and the
    // reason says which spellings answer it.
    std::vector<TypeArgument> shapeOnly{
        TypeArgument{
            .type = nullptr,
            .data = nullptr,
            .extents = std::make_shared<const std::vector<ExtentForm>>(constants({3}))}};
    refuses(shapeOnly, "a shape with no element type");
    require(
        argumentError.find("needs its element type") != std::string::npos,
        "the refusal of f32[3] says: " + argumentError);
    std::vector<TypeArgument> noElement;
    refuses(noElement, "no argument at all");
    require(
        argumentError.find("needs its element type") != std::string::npos,
        "the refusal of no argument at all says: " + argumentError);
    // An array that states its element type reads no type arguments of its own: the arguments of
    // the name belong to the name, not to every array.
    argumentError.clear();
    require(
        ArrayType::create(f32, shapeOf({3}))->applyArguments(
            std::span<const TypeArgument>(notNumeric), argumentError) == nullptr &&
            argumentError.find("states its element type") != std::string::npos,
        "f32[3] accepted a type argument: " + argumentError);

    return "shapes interned, relations checked, binding checked, alias arguments checked, moved "
           "check says: " +
           movedCheck;
}

} // namespace camel::core::type