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
 * Updated: Dec. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "conv.h"
#include "./data.h"
#include "core/data/data.h"

slot_t makeSlotFromPrimitiveData(const data_ptr_t &data) {
    ASSERT(data != nullptr, "Data is null.");

    TypeCode typeCode = data->type()->code();
    ASSERT(isPrimitive(typeCode), "Type is not primitive.");

    switch (typeCode) {
    case TypeCode::Int: {
        auto intData = tt::as_shared<IntData>(data);
        return static_cast<slot_t>(intData->data());
    }

    case TypeCode::Long: {
        auto longData = tt::as_shared<LongData>(data);
        return static_cast<slot_t>(longData->data());
    }

    case TypeCode::Float: {
        auto floatData = tt::as_shared<FloatData>(data);
        float f        = floatData->data();
        slot_t result;
        std::memcpy(&result, &f, sizeof(float));
        return result;
    }

    case TypeCode::Double: {
        auto doubleData = tt::as_shared<DoubleData>(data);
        double d        = doubleData->data();
        slot_t result;
        std::memcpy(&result, &d, sizeof(double));
        return result;
    }

    case TypeCode::Bool: {
        auto boolData = tt::as_shared<BoolData>(data);
        return static_cast<slot_t>(boolData->data() ? 1 : 0);
    }

    case TypeCode::Byte: {
        auto byteData = tt::as_shared<ByteData>(data);
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

Object *makeGCRefFromGCTracedData(const data_ptr_t &data, IAllocator &allocator) {
    ASSERT(data != nullptr, "Data is null.");

    TypeCode typeCode = data->type()->code();
    ASSERT(isGCTraced(typeCode), "Type is not GC traced.");

    switch (typeCode) {
    case TypeCode::String: {
        auto strData = tt::as_shared<StringData>(data);
        return String::from(strData->data(), allocator);
    }

    case TypeCode::Array: {
        auto arrayData        = tt::as_shared<ArrayData>(data);
        const auto &arrayType = tt::as_shared<ArrayType>(arrayData->type());
        Array *gcArray        = Array::create(arrayType->elemType()->code(), allocator);
        for (const auto &elem : arrayData->raw()) {
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcArray->append<Object *>(elemRef);
            } else if (elem->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcArray->append<slot_t>(slot);
            } else {
                ASSERT(false, "Unsupported array element type conversion.");
            }
        }
        return gcArray;
    }

    case TypeCode::Tuple: {
        auto tupleData        = tt::as_shared<TupleData>(data);
        const auto &tupleType = tt::as_shared<TupleType>(tupleData->type());
        Tuple *gcTuple        = Tuple::create(tupleType->layout(), allocator);
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
                ASSERT(false, "Unsupported tuple element type conversion.");
            }
        }
        return gcTuple;
    }

    case TypeCode::Struct: {
        auto structData        = tt::as_shared<StructData>(data);
        const auto &structType = tt::as_shared<StructType>(structData->type());
        Struct *gcStruct       = Struct::create(structType->layout(), allocator);
        for (const auto &[name, data] : structData->raw()) {
            if (data->type()->isGCTraced()) {
                Object *fieldRef = makeGCRefFromGCTracedData(data, allocator);
                gcStruct->set<Object *>(name, fieldRef);
            } else if (data->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(data);
                gcStruct->set<slot_t>(name, slot);
            } else {
                ASSERT(false, "Unsupported struct field type conversion.");
            }
        }
        return gcStruct;
    }

    case TypeCode::Function: {
        auto funcData    = tt::as_shared<FunctionData>(data);
        auto &graph      = funcData->graph();
        Function *gcFunc = Function::create(&graph, graph.closureType()->layout(), allocator);
        Tuple *gcTuple   = gcFunc->tuple();
        const auto &closureData = funcData->closure();
        for (size_t i = 0; i < closureData.size(); ++i) {
            const auto &elem = closureData[i];
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcTuple->set<Object *>(i, elemRef);
            } else if (elem->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcTuple->set<slot_t>(i, slot);
            } else {
                ASSERT(false, "Unsupported function closure element type conversion.");
            }
        }
        return gcFunc;
    }

    default:
        ASSERT(false, "Unsupported GC traced type conversion.");
        return NullRef;
    }
}

Tuple *makeStaticDataOfGraph(const GraphIR::Graph &graph, IAllocator &allocator) {
    Tuple *gcTuple = Tuple::create(graph.staticDataType()->layout(), allocator);

    const auto &staticDataArr = graph.staticDataArr();
    // 从 1 开始，0 号位置保留为空
    for (size_t i = 1; i < staticDataArr.size(); ++i) {
        const auto &data = staticDataArr[i];
        if (data->type()->isGCTraced()) {
            Object *dataRef = makeGCRefFromGCTracedData(data, allocator);
            gcTuple->set<Object *>(i, dataRef);
        } else if (data->type()->isPrimitive()) {
            slot_t slot = makeSlotFromPrimitiveData(data);
            gcTuple->set<slot_t>(i, slot);
        } else {
            ASSERT(false, "Unsupported static data type conversion.");
        }
    }

    return gcTuple;
}
