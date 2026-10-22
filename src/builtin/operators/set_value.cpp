// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "set_value.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/assert.h"
#include "camel/utils/type.h"

#include <format>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

/// The written value a constant argument carries, if it is one: an `int` literal arrives as a
/// slot holding the number itself (StaticArg; the enum operators read their tag the same way).
std::optional<Int64> constantOf(static_args_t statics, size_t index) {
    if (statics.size() <= index || !statics[index].value.has_value()) {
        return std::nullopt;
    }
    return static_cast<Int64>(*statics[index].value);
}

/**
 * `(aggregate: A, field: int, value: V) => A`, where `field` is a compile-time position: the
 * type of the result is the aggregate type and the value must enter the field's type. The
 * position is a constant, so a resolver that only sees types cannot decide it; this rule reads
 * the constant (resolveWith), which the graph builder supplies for a literal argument.
 */
class SetFieldTypeResolver final : public FuncTypeResolver {
  public:
    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        return decide(with, norm, {}, modifiers);
    }

    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        return decide(with, norm, normStatics, modifiers);
    }

    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expectedResult, const type_vec_t &with,
        const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        (void)expectedResult;
        return types.empty() ? decide(with, norm, normStatics, modifiers) : std::nullopt;
    }

    std::string signature() const override { return "(aggregate: A, field: int, value: V) => A"; }

  private:
    std::optional<FunctionType *> decide(
        const type_vec_t &with, const type_vec_t &norm, static_args_t statics,
        const ModifierSet &modifiers) const {
        if (!with.empty() || norm.size() != 3 || norm[0] == nullptr || norm[1] == nullptr ||
            norm[1]->code() != TypeCode::Int64) {
            return std::nullopt;
        }
        const std::optional<Int64> raw = constantOf(statics, 1);
        if (!raw.has_value() || *raw < 0) {
            return std::nullopt;
        }
        const size_t index = static_cast<size_t>(*raw);
        Type *result       = nullptr;
        if (norm[0]->code() == TypeCode::Struct) {
            const auto *structType = tt::as_ptr<StructType>(norm[0]);
            if (index >= structType->size() ||
                !entersImplicitly(structType->typeAt(index), norm[2])) {
                return std::nullopt;
            }
            result = norm[0];
        } else if (norm[0]->code() == TypeCode::Tuple) {
            const auto *tupleType = tt::as_ptr<TupleType>(norm[0]);
            if (tupleType->isCell() || index >= tupleType->size() ||
                !entersImplicitly(tupleType->typeAt(index), norm[2])) {
                return std::nullopt;
            }
            result = norm[0];
        } else {
            return std::nullopt;
        }
        FunctionTypeFactory factory;
        factory.setModifiers(modifiers);
        factory.setExitType(result);
        for (size_t i = 0; i < norm.size(); ++i) {
            factory.addNormArg(std::format("__n{}", i), norm[i], false);
        }
        return factory.build();
    }
};

} // namespace

// `set_field(a, i, v)`: a new struct or tuple, every field of `a` and `v` at position `i`.
slot_t __builtin__set_field__(ArgsView &with, ArgsView &norm, Context &) {
    (void)with;
    const Int64 raw  = norm.get<Int64>(1);
    const slot_t val = norm.slot(2);
    if (norm.code(0) == TypeCode::Struct) {
        const auto *structType = tt::as_ptr<StructType>(norm.type(0));
        const auto *source     = norm.get<Struct *>(0);
        if (raw < 0 || static_cast<size_t>(raw) >= structType->size()) {
            throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "a struct", structType->size());
        }
        auto *copy = static_cast<Struct *>(source->clone(mm::autoSpace(), structType, false));
        copy->set<slot_t>(static_cast<size_t>(raw), val, structType);
        return toSlot(copy);
    }
    ASSERT(norm.code(0) == TypeCode::Tuple, "set_field takes a struct or a tuple.");
    const auto *tupleType = tt::as_ptr<TupleType>(norm.type(0));
    const auto *source    = norm.get<Tuple *>(0);
    if (raw < 0 || static_cast<size_t>(raw) >= tupleType->size()) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "a tuple", tupleType->size());
    }
    auto *copy = static_cast<Tuple *>(source->clone(mm::autoSpace(), tupleType, false));
    copy->set<slot_t>(static_cast<size_t>(raw), val, tupleType);
    return toSlot(copy);
}

namespace {

void checkElementIndex(const Array *array, Int64 raw) {
    if (raw < 0 || static_cast<size_t>(raw) >= array->size()) {
        throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "an array", array->size());
    }
}

} // namespace

// `set_index(xs, i, v)`: a new array, the elements of `xs` and `v` at `i`. `xs` is unchanged.
slot_t __builtin__set_index__(ArgsView &with, ArgsView &norm, Context &) {
    (void)with;
    const auto *source     = norm.get<Array *>(0);
    const auto *arrayType  = tt::as_ptr<ArrayType>(norm.type(0));
    const Int64 raw        = norm.get<Int64>(1);
    const slot_t val       = norm.slot(2);
    checkElementIndex(source, raw);
    auto *copy = static_cast<Array *>(source->clone(mm::autoSpace(), arrayType, false));
    copy->setSlot(static_cast<size_t>(raw), val, arrayType);
    return toSlot(copy);
}

// The in-place form: the array is written where it can be (the lowering hands it the result of
// `:var/take`, so no other reference to it exists), and copied where the storage is shared.
slot_t __builtin__set_index_in_place__(ArgsView &with, ArgsView &norm, Context &) {
    (void)with;
    auto *array           = norm.get<Array *>(0);
    const auto *arrayType = tt::as_ptr<ArrayType>(norm.type(0));
    const Int64 raw       = norm.get<Int64>(1);
    const slot_t val      = norm.slot(2);
    checkElementIndex(array, raw);
    const size_t index = static_cast<size_t>(raw);
    if (array->isSlots() || array->canWriteInPlace()) {
        array->setSlot(index, val, arrayType);
        return toSlot(array);
    }
    auto *copy = static_cast<Array *>(array->clone(mm::autoSpace(), arrayType, false));
    copy->setSlot(index, val, arrayType);
    return toSlot(copy);
}

const std::vector<oper_group_ptr_t> &getSetValueOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "__set_field__",
            {{":op/set_field", std::make_shared<SetFieldTypeResolver>()}}),
        // The in-place form (:op/set_index_in_place) has no rule of its own: the operator table
        // knows it reuses the operand of the plain form (declareReusesOperand), which is where
        // its type comes from.
        OperatorGroup::create(
            "__set_index__",
            {{":op/set_index",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {3, {false, false, false}}},
                  "(array: T[], index: int, value: T) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm.size() != 3 || norm[0] == nullptr ||
                          norm[0]->code() != TypeCode::Array || norm[1] == nullptr ||
                          norm[1]->code() != TypeCode::Int64) {
                          return std::nullopt;
                      }
                      Type *elem = tt::as_ptr<ArrayType>(norm[0])->elemType();
                      if (!entersImplicitly(elem, norm[2])) {
                          return std::nullopt;
                      }
                      return norm[0];
                  })}}),
    };
    return groups;
}
