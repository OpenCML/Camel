// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/conv.h"
#include "camel/core/data.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/func.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/graph.h"

using namespace camel::core::data;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

/// The shape an array type states as constants, when it states all of it and that shape holds
/// as many elements as the data has (`f32[2, 2]` for a nested literal of four); empty when the
/// shape is not stated in full (`f32[]`, `f32[3]`, `f32[2, ?]`), where the length is the one the
/// elements give the array.
std::vector<int64_t> statedExtents(const ArrayType *arrayType, size_t elements) {
    const std::span<const ExtentForm> extents = arrayType->extents();
    if (extents.empty()) {
        return {};
    }
    std::vector<int64_t> stated;
    stated.reserve(extents.size());
    int64_t numel = 1;
    for (const ExtentForm &extent : extents) {
        if (!extent.isConstant() || extent.constant() < 0) {
            return {};
        }
        stated.push_back(extent.constant());
        numel *= extent.constant();
    }
    if (static_cast<size_t>(numel) != elements) {
        return {};
    }
    return stated;
}

} // namespace

slot_t makeSlotFromPrimitiveData(const data_ptr_t &data) {
    ASSERT(data != nullptr, "Data is null.");

    TypeCode typeCode = data->type()->code();
    ASSERT(camel::core::type::isPrimitive(typeCode), "Type is not primitive.");

    switch (typeCode) {
    case TypeCode::Int32: {
        auto intData = tt::as_shared<camel::core::data::IntData>(data);
        return static_cast<slot_t>(intData->data());
    }

    case TypeCode::Int64: {
        auto longData = tt::as_shared<camel::core::data::LongData>(data);
        return static_cast<slot_t>(longData->data());
    }

    case TypeCode::Float32: {
        auto floatData = tt::as_shared<camel::core::data::FloatData>(data);
        float f        = floatData->data();
        // An f32 uses the low four bytes of the slot. The rest is not part of the value, and a
        // rule compiles a constant from a slot: if this half were left as it was on the stack, two
        // readings of the same f32 constant could compare unequal, and whether they did would
        // depend on the build. Zero it, as toSlot does for every value narrower than a slot.
        slot_t result{};
        std::memcpy(&result, &f, sizeof(float));
        return result;
    }

    case TypeCode::Float64: {
        auto doubleData = tt::as_shared<camel::core::data::DoubleData>(data);
        double d        = doubleData->data();
        slot_t result;
        std::memcpy(&result, &d, sizeof(double));
        return result;
    }

    case TypeCode::BFloat16: {
        auto halfData = tt::as_shared<camel::core::data::HalfData>(data);
        // A bf16 or f16 value occupies the low two bytes of the slot, the 16-bit pattern of the
        // value zero-extended, which is the same two bytes a compact array of the type holds
        // (src/core/rtdata/array.cpp, readLeafSlow/writeLeafSlow).
        return halfSlotOfBits(halfData->bits());
    }

    case TypeCode::Float16: {
        auto halfData = tt::as_shared<camel::core::data::HalfData>(data);
        return halfSlotOfBits(halfData->bits());
    }

    case TypeCode::Bool: {
        auto boolData = tt::as_shared<camel::core::data::BoolData>(data);
        return static_cast<slot_t>(boolData->data() ? 1 : 0);
    }

    case TypeCode::Void: {
        return 0;
    }

    default:
        ASSERT(false, "Unsupported primitive type conversion.");
        return 0;
    }
}

Object *makeGCRefFromGCTracedData(const data_ptr_t &data, camel::core::mm::IAllocator &allocator) {
    ASSERT(data != nullptr, "Data is null.");

    TypeCode typeCode = data->type()->code();
    ASSERT(camel::core::type::isGCTraced(typeCode), "Type is not GC traced.");

    switch (typeCode) {
    case TypeCode::Complex64:
    case TypeCode::Complex128: {
        auto complexData = tt::as_shared<camel::core::data::ComplexData>(data);
        return Complex::create(complexData->real(), complexData->imaginary(), typeCode, allocator);
    }

    case TypeCode::String: {
        auto strData = tt::as_shared<camel::core::data::StringData>(data);
        return String::from(strData->data(), allocator);
    }

    case TypeCode::Array: {
        auto arrayData        = tt::as_shared<camel::core::data::ArrayData>(data);
        const auto *arrayType = tt::as_ptr<camel::core::type::ArrayType>(arrayData->type());
        // The shape a type states as constants is the value's shape (design principles 5.4): a
        // rank-2 constant written as a nested literal is one array of that shape, not a flat run
        // of its elements. A type that states none (`f32[]`, `f32[n]`) keeps the length the
        // elements give it, which is what the append below grows to.
        const std::vector<int64_t> stated = statedExtents(arrayType, arrayData->raw().size());
        if (stated.empty()) {
            Array *gcArray = Array::create(allocator, arrayType, 0);
            for (const auto &elem : arrayData->raw()) {
                if (elem->type()->isGCTraced()) {
                    Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                    gcArray->append<Object *>(elemRef, arrayType);
                } else if (camel::core::type::isPrimitive(elem->type()->code())) {
                    slot_t slot = makeSlotFromPrimitiveData(elem);
                    gcArray->append<slot_t>(slot, arrayType);
                } else {
                    gcArray->append<slot_t>(NullSlot, arrayType);
                }
            }
            return gcArray;
        }
        // The elements are written into the shape the type states, by flat index: the shape is
        // the value's, so it must not be rewritten to the length the elements happen to have.
        Array *gcArray = Array::create(allocator, arrayType, stated, false);
        const auto &elems = arrayData->raw();
        for (size_t i = 0; i < elems.size(); ++i) {
            const auto &elem = elems[i];
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcArray->set<Object *>(i, elemRef, arrayType);
            } else if (camel::core::type::isPrimitive(elem->type()->code())) {
                gcArray->set<slot_t>(i, makeSlotFromPrimitiveData(elem), arrayType);
            } else {
                gcArray->set<slot_t>(i, NullSlot, arrayType);
            }
        }
        return gcArray;
    }

    case TypeCode::Tuple: {
        auto tupleData        = tt::as_shared<camel::core::data::TupleData>(data);
        const auto *tupleType = tt::as_ptr<camel::core::type::TupleType>(tupleData->type());
        Tuple *gcTuple        = Tuple::create(tupleType->size(), allocator);
        const auto &elems     = tupleData->raw();
        for (size_t i = 0; i < tupleData->size(); ++i) {
            const auto &elem = elems[i];
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcTuple->set<Object *>(i, elemRef, tupleType);
            } else if (elem->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcTuple->set<slot_t>(i, slot, tupleType);
            } else {
                gcTuple->set<slot_t>(i, NullSlot, tupleType);
            }
        }
        return gcTuple;
    }

    case TypeCode::Struct: {
        auto structData        = tt::as_shared<camel::core::data::StructData>(data);
        const auto *structType = tt::as_ptr<camel::core::type::StructType>(structData->type());
        Struct *gcStruct       = Struct::create(structType->size(), allocator);
        for (const auto &[name, data] : structData->raw()) {
            if (data->type()->isGCTraced()) {
                Object *fieldRef = makeGCRefFromGCTracedData(data, allocator);
                gcStruct->set<Object *>(name, fieldRef, structType);
            } else if (data->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(data);
                gcStruct->set<slot_t>(name, slot, structType);
            } else {
                gcStruct->set<slot_t>(name, NullSlot, structType);
            }
        }
        return gcStruct;
    }

    case TypeCode::Function: {
        auto funcData = tt::as_shared<camel::core::data::FunctionData>(data);
        auto *graph   = funcData->graph();
        ASSERT(graph != nullptr, "FunctionData must carry a runtime graph.");
        auto *gcFunc = ::Function::create(
            funcData->program(),
            funcData->graphId(),
            graph->closureType(),
            allocator);
        Tuple *gcTuple          = gcFunc->tuple();
        const auto *closureType = graph->closureType();

        if (gcTuple->size() == 0) {
            return gcFunc;
        }

        // The function carries closure data.
        // Fill in the captured closure values below.
        const auto &closureData = funcData->closure();

        if (closureData.size() > 0) {
            ASSERT(
                closureData.size() == gcTuple->size(),
                std::format(
                    "Closure data size mismatch in FunctionData. Expected: {}, Actual: {}",
                    gcTuple->size(),
                    closureData.size()));
            for (size_t i = 0; i < closureData.size(); ++i) {
                const auto &elem = closureData[i];
                if (elem->type()->isGCTraced()) {
                    Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                    gcTuple->set<Object *>(i, elemRef, closureType);
                } else if (elem->type()->isPrimitive()) {
                    slot_t slot = makeSlotFromPrimitiveData(elem);
                    gcTuple->set<slot_t>(i, slot, closureType);
                } else {
                    gcTuple->set<slot_t>(i, NullSlot, closureType);
                }
            }
        } else {
            // No closure values were captured, so fill with nulls.
            for (size_t i = 0; i < gcTuple->size(); ++i) {
                gcTuple->set<slot_t>(i, NullSlot, closureType);
            }
        }

        return gcFunc;
    }

    default:
        ASSERT(false, "Unsupported GC traced type conversion.");
        return NullRef;
    }
}
