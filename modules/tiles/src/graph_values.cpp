// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Preserve Camel FILL conventions: normal input is the template, with-inputs are fields.

#include "tiles/internal/graph_values.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "modules/tensor/type.h"
#include "tiles/device.h"
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace camel::tiles {
using namespace core::type;
namespace {
Type *buildResidentType(Type *type) {
    // A scalar leaf rises with the element type it was written with (design principles 7.3), so
    // both of the device's element types are resident, not only the f64 one.
    if (tensor::isTensorType(type) || deviceScalarDType(type))
        return DeviceType::get(type);
    if (type->code() == TypeCode::Tuple) {
        auto *tuple = static_cast<TupleType *>(type);
        type_vec_t fields;
        for (size_t i = 0; i < tuple->size(); ++i) {
            auto *field = buildResidentType(tuple->typeAt(i));
            if (!field)
                return nullptr;
            fields.push_back(field);
        }
        return TupleType::create(fields);
    }
    if (type->code() == TypeCode::Struct) {
        auto *object = static_cast<StructType *>(type);
        StructTypeFactory fields;
        for (size_t i = 0; i < object->size(); ++i) {
            auto *field = buildResidentType(object->typeAt(i));
            if (!field)
                return nullptr;
            fields.add(std::string(object->fieldName(i)), field);
        }
        return fields.build();
    }
    return nullptr;
}
} // namespace
Type *residentType(Type *type) {
    static std::mutex mutex;
    static std::unordered_map<Type *, Type *> types;
    std::lock_guard lock(mutex);
    if (auto it = types.find(type); it != types.end())
        return it->second;
    auto *result = buildResidentType(type);
    types.emplace(type, result);
    return result;
}
runtime::gc_node_ref_t packGraphAggregate(
    runtime::GraphDraft &draft, core::type::Type *type,
    std::span<const runtime::gc_node_ref_t> values) {
    const bool tuple = type->code() == TypeCode::Tuple;
    if (!tuple && type->code() != TypeCode::Struct)
        throw std::runtime_error("Tiles aggregate construction requires a tuple or struct");
    const size_t count =
        tuple ? static_cast<TupleType *>(type)->size() : static_cast<StructType *>(type)->size();
    if (values.size() != count)
        throw std::runtime_error("Tiles aggregate field arity mismatch");
    for (size_t i = 0; i < count; ++i) {
        auto *expected = tuple ? static_cast<TupleType *>(type)->typeAt(i)
                               : static_cast<StructType *>(type)->typeAt(i);
        if (draft.header(values[i])->dataType != expected)
            throw std::runtime_error("Tiles aggregate field type mismatch");
    }
    const auto empty = tuple ? core::rtdata::toSlot(::Tuple::create(count, core::mm::autoSpace()))
                             : core::rtdata::toSlot(::Struct::create(count, core::mm::autoSpace()));
    const auto base  = draft.materializeStaticValue(empty, type);
    std::vector<size_t> slots(count);
    std::iota(slots.begin(), slots.end(), 0);
    const auto output = draft.addFillNode(
        type,
        runtime::makeFillPayload(
            tuple ? runtime::GCFillKind::Tuple : runtime::GCFillKind::Struct,
            slots),
        draft.allocateRuntimeSlot(type));
    draft.setNormInputs(output, std::span(&base, 1));
    draft.setWithInputs(output, values);
    return output;
}
runtime::gc_node_ref_t
packGraphTuple(runtime::GraphDraft &draft, std::span<const runtime::gc_node_ref_t> values) {
    std::vector<core::type::Type *> types;
    for (auto value : values) {
        const auto *node = draft.header(value);
        if (!node || !node->dataType)
            throw std::runtime_error("Tiles tuple requires typed graph values");
        types.push_back(node->dataType);
    }
    return packGraphAggregate(draft, core::type::TupleType::create(types), values);
}
} // namespace camel::tiles
