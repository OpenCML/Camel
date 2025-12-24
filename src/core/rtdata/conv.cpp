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
 * Updated: Dec. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "conv.h"
#include "./data.h"
#include "core/data/data.h"

slot_t makeSlotFromPrimitiveData(const data_ptr_t &data) {
    ASSERT(data != nullptr, "Data is null.");

    TypeCode typeCode = data->type()->code();
    ASSERT(isPrimitive(typeCode), "Type is not primitive.");

    switch (typeCode) {
    case TypeCode::Int32: {
        auto intData = tt::as_shared<IntData>(data);
        return static_cast<slot_t>(intData->data());
    }

    case TypeCode::Int64: {
        auto longData = tt::as_shared<LongData>(data);
        return static_cast<slot_t>(longData->data());
    }

    case TypeCode::Float32: {
        auto floatData = tt::as_shared<FloatData>(data);
        float f        = floatData->data();
        slot_t result;
        std::memcpy(&result, &f, sizeof(float));
        return result;
    }

    case TypeCode::Float64: {
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
        Array *gcArray        = Array::create(arrayType->layout(), allocator);
        for (const auto &elem : arrayData->raw()) {
            if (elem->type()->isGCTraced()) {
                Object *elemRef = makeGCRefFromGCTracedData(elem, allocator);
                gcArray->append<Object *>(elemRef);
            } else if (elem->type()->isPrimitive()) {
                slot_t slot = makeSlotFromPrimitiveData(elem);
                gcArray->append<slot_t>(slot);
            } else {
                gcArray->append<slot_t>(NullSlot);
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
                gcTuple->set<slot_t>(i, NullSlot);
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
                gcStruct->set<slot_t>(name, NullSlot);
            }
        }
        return gcStruct;
    }

    case TypeCode::Function: {
        auto funcData    = tt::as_shared<FunctionData>(data);
        auto &graph      = funcData->graph();
        Function *gcFunc = Function::create(&graph, graph.closureType()->layout(), allocator);
        Tuple *gcTuple   = gcFunc->tuple();

        if (gcTuple->size() == 0) {
            return gcFunc;
        }

        // 说明函数包含闭包
        // 下面填充已捕获的闭包值
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
            // 没有已捕获的闭包值，则填充空值
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
