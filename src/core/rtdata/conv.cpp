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
 * Created: Dec. 07, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/conv.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/func.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"

using namespace camel::core::data;
using namespace camel::core::type;
using namespace camel::core::rtdata;

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
        slot_t result;
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

    case TypeCode::Bool: {
        auto boolData = tt::as_shared<camel::core::data::BoolData>(data);
        return static_cast<slot_t>(boolData->data() ? 1 : 0);
    }

    case TypeCode::Byte: {
        auto byteData = tt::as_shared<camel::core::data::ByteData>(data);
        return static_cast<slot_t>(byteData->data());
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
    case TypeCode::String: {
        auto strData = tt::as_shared<camel::core::data::StringData>(data);
        return String::from(strData->data(), allocator);
    }

    case TypeCode::Array: {
        auto arrayData = tt::as_shared<camel::core::data::ArrayData>(data);
        Array *gcArray = Array::create(allocator, 0);
        for (const auto &elem : arrayData->raw()) {
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcArray->append<Object *>(elemRef);
            } else if (camel::core::type::isPrimitive(elem->type()->code())) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcArray->append<slot_t>(slot);
            } else {
                gcArray->append<slot_t>(NullSlot);
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
                gcTuple->set<Object *>(i, elemRef);
            } else if (elem->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcTuple->set<slot_t>(i, slot);
            } else {
                gcTuple->set<slot_t>(i, NullSlot);
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
        auto funcData    = tt::as_shared<camel::core::data::FunctionData>(data);
        auto &graph      = funcData->graph();
        Function *gcFunc = Function::create(&graph, graph.closureType(), allocator);
        Tuple *gcTuple   = gcFunc->tuple();

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
                    gcTuple->set<Object *>(i, elemRef);
                } else if (elem->type()->isPrimitive()) {
                    slot_t slot = makeSlotFromPrimitiveData(elem);
                    gcTuple->set<slot_t>(i, slot);
                } else {
                    gcTuple->set<slot_t>(i, NullSlot);
                }
            }
        } else {
            // No closure values were captured, so fill with nulls.
            for (size_t i = 0; i < gcTuple->size(); ++i) {
                gcTuple->set<slot_t>(i, NullSlot);
            }
        }

        return gcFunc;
    }

    default:
        ASSERT(false, "Unsupported GC traced type conversion.");
        return NullRef;
    }
}
