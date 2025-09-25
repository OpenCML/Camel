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
 * Created: Sep. 25, 2025
 * Updated: Sep. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

OperatorReturnCode __contains__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withs = self->withInputs();
    if (withs.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<contains> operator requires one with argument: (collection)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    const auto &norms = self->normInputs();
    if (norms.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<contains> operator requires one norm argument: (target)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &collection = frame.get(withs[0]);
    const data_ptr_t &target = frame.get(norms[0]);

    bool found = false;

    auto checkInListLike = [&](const auto &container) {
        for (const auto &item : container->raw()) {
            if (item->equals(target)) {
                found = true;
                break;
            }
        }
    };

    switch (collection->type()->code()) {
    case TypeCode::List:
        checkInListLike(tt::as_shared<ListData>(collection));
        break;
    case TypeCode::Array:
        checkInListLike(tt::as_shared<ArrayData>(collection));
        break;
    case TypeCode::Vector:
        checkInListLike(tt::as_shared<VectorData>(collection));
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<contains> not supported for type: " + collection->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self, std::make_shared<BoolData>(found));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __range__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (ins.size() < 2 || ins.size() > 3) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<range> requires 2 or 3 integer arguments: start, stop, [step]");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &startData = frame.get(ins[0]);
    const data_ptr_t &stopData = frame.get(ins[1]);
    data_ptr_t stepData = nullptr;

    if (ins.size() == 3) {
        stepData = frame.get(ins[2]);
    }

    auto extractInt64 = [&](const data_ptr_t &d) -> std::optional<int64_t> {
        if (d->type() == Type::Int32()) {
            return static_cast<int64_t>(d->as<Int32Data>(Type::Int32())->data());
        } else if (d->type() == Type::Int64()) {
            return d->as<Int64Data>(Type::Int64())->data();
        }
        return std::nullopt;
    };

    auto startOpt = extractInt64(startData);
    auto stopOpt = extractInt64(stopData);
    auto stepOpt = stepData ? extractInt64(stepData) : std::optional<int64_t>(1);

    if (!startOpt || !stopOpt || !stepOpt) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<range> arguments must be Int32 or Int64");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    int64_t start = *startOpt;
    int64_t stop = *stopOpt;
    int64_t step = *stepOpt;

    if (step == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<range> step cannot be zero");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    data_vec_t values;

    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        for (int64_t i = start; (step > 0 ? i < stop : i > stop); i += step) {
            values.push_back(std::make_shared<Int64Data>(i));
        }
    }

    auto arrayType = std::make_shared<ArrayType>(Type::Int64(), values.size());
    auto result = ArrayData::create(arrayType, std::move(values));

    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __len__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (ins.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<len> operator requires exactly one argument");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &arg = frame.get(ins[0]);

    int32_t len = 0;

    switch (arg->type()->code()) {
    case TypeCode::List:
        len = static_cast<int32_t>(tt::as_shared<ListData>(arg)->raw().size());
        break;
    case TypeCode::Array:
        len = static_cast<int32_t>(tt::as_shared<ArrayData>(arg)->raw().size());
        break;
    case TypeCode::Vector:
        len = static_cast<int32_t>(tt::as_shared<VectorData>(arg)->raw().size());
        break;
    case TypeCode::Tuple:
        len = static_cast<int32_t>(tt::as_shared<TupleData>(arg)->raw().size());
        break;
    case TypeCode::Dict:
        len = static_cast<int32_t>(tt::as_shared<DictData>(arg)->raw().size());
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<len> not supported for type: " + arg->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self, std::make_shared<Int32Data>(len));
    return OperatorReturnCode::OK;
}
