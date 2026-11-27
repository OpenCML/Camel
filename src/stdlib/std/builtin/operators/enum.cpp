// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "enum.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/assert.h"

using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace mm = camel::core::mm;

namespace {

constexpr Int64 literalOf(const StaticArg &arg) { return static_cast<Int64>(*arg.value); }

/**
 * The three calls of an enum value (design principles 5.3). Their types are decided by the
 * enum type, which the graph builder supplies: a value being built carries the instance as its
 * explicit type argument, and the fields are read with the variant they belong to, so the type
 * of the field is that of the position in that variant and no other.
 */
class EnumOpTypeResolver final : public FuncTypeResolver {
  public:
    enum class Kind { Make, Tag, Field };

    EnumOpTypeResolver(Kind kind, std::string signature, size_t normCount)
        : kind_(kind), signature_(std::move(signature)), normCount_(normCount) {}

    std::optional<FunctionType *>
    resolve(const type_vec_t &with, const type_vec_t &norm) const override {
        return decide({}, with, norm, {});
    }

    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics) const override {
        return decide({}, with, norm, normStatics);
    }

    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expectedResult, const type_vec_t &with,
        const type_vec_t &norm, static_args_t normStatics) const override {
        (void)expectedResult;
        return decide(types, with, norm, normStatics);
    }

    std::string signature() const override { return signature_; }

  private:
    std::optional<FunctionType *> decide(
        const type_vec_t &types, const type_vec_t &with, const type_vec_t &norm,
        static_args_t normStatics) const {
        if (!with.empty() || (normCount_ == 0 ? norm.empty() : norm.size() != normCount_)) {
            return std::nullopt;
        }
        Type *result = nullptr;
        switch (kind_) {
        case Kind::Make: {
            if (types.size() != 1 || norm[0] == nullptr || norm[0]->code() != TypeCode::Int64) {
                return std::nullopt;
            }
            EnumType *built = asEnumType(types[0]);
            if (built == nullptr || !built->complete()) {
                return std::nullopt;
            }
            result = built;
            break;
        }
        case Kind::Tag: {
            if (!types.empty() || asEnumType(norm[0]) == nullptr) {
                return std::nullopt;
            }
            result = Type::Int64();
            break;
        }
        case Kind::Field: {
            if (!types.empty() || norm.size() != 3 || norm[0] == nullptr ||
                norm[0]->code() != TypeCode::Int64 || norm[1] == nullptr ||
                norm[1]->code() != TypeCode::Int64 || normStatics.size() < 2 ||
                !normStatics[0].value.has_value() || !normStatics[1].value.has_value()) {
                return std::nullopt;
            }
            EnumType *value = asEnumType(norm[2]);
            if (value == nullptr) {
                return std::nullopt;
            }
            const auto tag   = static_cast<size_t>(literalOf(normStatics[0]));
            const auto index = static_cast<size_t>(literalOf(normStatics[1]));
            if (tag >= value->variantCount() || index >= value->variant(tag).fieldCount()) {
                return std::nullopt;
            }
            result = value->fieldType(tag, index);
            break;
        }
        }
        if (result == nullptr) {
            return std::nullopt;
        }
        FunctionTypeFactory factory;
        factory.setExitType(result);
        for (size_t i = 0; i < norm.size(); ++i) {
            factory.addNormArg(std::format("__n{}", i), norm[i], false);
        }
        return factory.build();
    }

    Kind kind_;
    std::string signature_;
    size_t normCount_;
};

} // namespace

slot_t __enum_make__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const size_t fieldCount = norm.size() > 0 ? norm.size() - 1 : 0;
    Variant *value =
        Variant::create(static_cast<uint32_t>(norm.get<Int64>(0)), fieldCount, mm::autoSpace());
    for (size_t i = 0; i < fieldCount; ++i) {
        value->set(i, norm.slot(i + 1));
    }
    return toSlot(value);
}

slot_t __enum_tag__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Variant *value = fromSlot<const Variant *>(norm.slot(0));
    if (value == nullptr) {
        // The value a tag is read from is null: it came from the program and the operator cannot
        // take it. A branch rather than an assert, because an assert's premise is not held in a
        // release build (an assert that guards a dereference is
        // no guard at all there).
        throwRuntimeFault(
            camel::core::error::RuntimeDiag::InvalidArgument,
            "__enum_tag__",
            "the value is null, and a tag is taken from an enum value");
    }
    return toSlot<Int64>(static_cast<Int64>(value->tag()));
}

slot_t __enum_field__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const size_t tag     = static_cast<size_t>(norm.get<Int64>(0));
    const size_t index   = static_cast<size_t>(norm.get<Int64>(1));
    const Variant *value = fromSlot<const Variant *>(norm.slot(2));
    ASSERT(value != nullptr, "A field of an enum value is taken from a value that is null.");
    // The two cases are told apart here, and the one this node does not select reads as no value:
    // a value whose tag is not the one the node was written for, or whose variant has no field at
    // `index`, is the case of another arm. Returning the zero slot is the answer every lowering
    // that takes a part out of a tagged value already gives (src/builtin/operators/tagged.cpp,
    // "applied to the other case they return the zero slot"), and the pass relies on it: a node
    // that cannot be replaced is still evaluated, to fault where it is written if it can fault
    // (generic.cpp, `foldDraft`), so an arm the constant's tag does not select is executed with a
    // value of the other variant in hand. An `ASSERT` would not do here -- it is
    // nothing in a release build -- and `Variant::get` would then read an index the value does not
    // have ("reading field 0 of a Variant whose tag 1 declares 0 field(s)",
    // include/camel/core/rtdata/variant.h).
    if (value->tag() != tag || index >= value->fieldCount()) {
        return NullSlot;
    }
    return value->get(index);
}

std::vector<oper_group_ptr_t> getEnumOperatorGroups() {
    const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "__enum_make__",
            {{":enum/make",
              std::make_shared<EnumOpTypeResolver>(
                  EnumOpTypeResolver::Kind::Make,
                  "(tag: int, ...fields: T) => E",
                  0)}}),
        OperatorGroup::create(
            "__enum_tag__",
            {{":enum/tag",
              std::make_shared<EnumOpTypeResolver>(
                  EnumOpTypeResolver::Kind::Tag,
                  "(value: E) => int",
                  1)}}),
        OperatorGroup::create(
            "__enum_field__",
            {{":enum/field",
              std::make_shared<EnumOpTypeResolver>(
                  EnumOpTypeResolver::Kind::Field,
                  "(tag: int, index: int, value: E) => T",
                  3)}}),
    };
    return groups;
}
