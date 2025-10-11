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
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

OperatorReturnCode __len__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->withInputs();
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
    case TypeCode::Array:
        len = static_cast<int32_t>(tt::as_shared<ArrayData>(arg)->raw().size());
        break;
    case TypeCode::Tuple:
        len = static_cast<int32_t>(tt::as_shared<TupleData>(arg)->raw().size());
        break;
    case TypeCode::Struct:
        len = static_cast<int32_t>(tt::as_shared<StructData>(arg)->raw().size());
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

OperatorReturnCode __zip__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &normIns = self->normInputs();

    if (normIns.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> operator requires exactly two input sequences");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &a = frame.get(normIns[0]);
    const data_ptr_t &b = frame.get(normIns[1]);

    auto getElements = [](const data_ptr_t &data) -> std::optional<data_vec_t> {
        switch (data->type()->code()) {
        case TypeCode::Array:
            return tt::as_shared<ArrayData>(data)->raw();
        case TypeCode::Tuple:
            return tt::as_shared<TupleData>(data)->raw();
        default:
            return std::nullopt;
        }
    };

    auto aElemsOpt = getElements(a);
    auto bElemsOpt = getElements(b);

    if (!aElemsOpt || !bElemsOpt) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> requires both inputs to be List/Array/Vector/Tuple");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_vec_t &aElems = *aElemsOpt;
    const data_vec_t &bElems = *bElemsOpt;

    if (aElems.size() != bElems.size()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> requires both sequences to have the same length");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    data_vec_t zipped;
    zipped.reserve(aElems.size());

    for (size_t i = 0; i < aElems.size(); ++i) {
        data_vec_t pair{aElems[i], bElems[i]};
        zipped.push_back(ArrayData::create(Type::Array(Type::Any()), std::move(pair)));
    }

    frame.set(self, ArrayData::create(Type::Array(Type::Array(Type::Any())), std::move(zipped)));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __head__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    if (withIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<head> operator requires one with argument: (collection)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    if (!normIns.empty()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<head> operator does not take normal arguments");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &collect = frame.get(withIns[0]);

    if (!collect || collect->isNull()) {
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    auto extract_first = [&](const data_vec_t &vec) -> data_ptr_t {
        return vec.empty() ? Data::null() : vec[0];
    };

    switch (collect->type()->code()) {
    case TypeCode::Array:
        frame.set(self, extract_first(tt::as_shared<ArrayData>(collect)->raw()));
        break;
    case TypeCode::Tuple:
        frame.set(self, extract_first(tt::as_shared<TupleData>(collect)->raw()));
        break;
    case TypeCode::String: {
        auto str = tt::as_shared<StringData>(collect)->data();
        if (str.empty()) {
            frame.set(self, Data::null());
        } else {
            frame.set(self, std::make_shared<StringData>(std::string(1, str[0])));
        }
    } break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<head> not supported for type: " + collect->type()->toString());
        frame.set(self, Data::null());
        break;
    }

    return OperatorReturnCode::OK;
}

OperatorReturnCode __tail__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    if (withIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<tail> operator requires one with argument: (collection)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    if (!normIns.empty()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<tail> operator does not take normal arguments");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &collect = frame.get(withIns[0]);

    if (!collect || collect->isNull()) {
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    auto slice_tail = [](const data_vec_t &vec) -> data_vec_t {
        return vec.size() <= 1 ? data_vec_t{} : data_vec_t(vec.begin() + 1, vec.end());
    };

    switch (collect->type()->code()) {
    case TypeCode::Array: {
        auto array = tt::as_shared<ArrayData>(collect)->raw();
        auto new_vec = slice_tail(array);
        auto elem_type = tt::as_shared<ArrayType>(collect->type())->elementType();
        frame.set(self, ArrayData::create(Type::Array(elem_type), std::move(new_vec)));
        break;
    }
    case TypeCode::Tuple: {
        auto vec = tt::as_shared<TupleData>(collect)->raw();
        frame.set(
            self,
            TupleData::create(
                tt::as_shared<TupleType>(collect->type())
                    ->slice(1, tt::as_shared<TupleType>(collect->type())->size()),
                slice_tail(vec)));
        break;
    }
    case TypeCode::String: {
        auto str = tt::as_shared<StringData>(collect)->data();
        if (str.size() <= 1) {
            frame.set(self, Data::null());
        } else {
            frame.set(self, std::make_shared<StringData>(str.substr(1)));
        }
        break;
    }
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<tail> not supported for type: " + collect->type()->toString());
        frame.set(self, Data::null());
        break;
    }

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

    auto extractInt32 = [&](const data_ptr_t &d) -> std::optional<int32_t> {
        if (d->type() == Type::Int32()) {
            return d->as<Int32Data>(Type::Int32())->data();
        } else if (d->type() == Type::Int64()) {
            return static_cast<int32_t>(d->as<Int64Data>(Type::Int64())->data());
        }
        return std::nullopt;
    };

    auto startOpt = extractInt32(startData);
    auto stopOpt = extractInt32(stopData);
    auto stepOpt = stepData ? extractInt32(stepData) : std::optional<int32_t>(1);

    if (!startOpt || !stopOpt || !stepOpt) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<range> arguments must be Int32 or Int64");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    int32_t start = *startOpt;
    int32_t stop = *stopOpt;
    int32_t step = *stepOpt;

    if (step == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<range> step cannot be zero");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    data_vec_t values;

    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        for (int32_t i = start; (step > 0 ? i < stop : i > stop); i += step) {
            values.push_back(std::make_shared<Int32Data>(i));
        }
    }

    auto arrayType = std::make_shared<ArrayType>(Type::Int64());
    auto result = ArrayData::create(arrayType, std::move(values));

    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __slice__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();
    if (withIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<slice> operator requires one with argument: (collection)");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    if (normIns.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<slice> operator requires exactly three arguments: start, end");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &collect = frame.get(withIns[0]);
    const data_ptr_t &startArg = frame.get(normIns[0]);
    const data_ptr_t &endArg = frame.get(normIns[1]);

    if (!Type::castSafetyCheck(startArg->type(), Type::Int32()) ||
        !Type::castSafetyCheck(endArg->type(), Type::Int32())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<slice> operator requires start and end to be integer");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    int32_t start = startArg->as<Int32Data>(Type::Int32())->data();
    int32_t end = endArg->as<Int32Data>(Type::Int32())->data();

    auto slice_range = [](int32_t size, int32_t &start, int32_t &end) {
        if (start < 0)
            start += size;
        if (end < 0)
            end += size;
        start = std::max(0, std::min(start, size));
        end = std::max(0, std::min(end, size));
        if (end < start)
            end = start; // empty slice
    };

    switch (collect->type()->code()) {
    case TypeCode::Array: {
        auto array = tt::as_shared<ArrayData>(collect)->raw();
        slice_range(static_cast<int32_t>(array.size()), start, end);
        data_vec_t sliced(array.begin() + start, array.begin() + end);
        frame.set(
            self,
            ArrayData::create(
                Type::Array(tt::as_shared<ArrayType>(collect->type())->elementType()),
                std::move(sliced)));
        break;
    }
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<slice> not supported for type: " + collect->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    return OperatorReturnCode::OK;
}

OperatorReturnCode __concat__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    if (withIns.size() != 1 || normIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<concat> operator requires one with argument and one normal argument");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &left = frame.get(withIns[0]);
    const data_ptr_t &right = frame.get(normIns[0]);

    if (left->type()->code() != right->type()->code()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<concat> operator requires both arguments to be of same type");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    switch (left->type()->code()) {
    case TypeCode::Array: {
        auto l = tt::as_shared<ArrayData>(left)->raw();
        auto r = tt::as_shared<ArrayData>(right)->raw();
        l.insert(l.end(), r.begin(), r.end());
        auto elemType = tt::as_shared<ArrayType>(left->type())->elementType();
        frame.set(self, ArrayData::create(Type::Array(elemType), std::move(l)));
        break;
    }
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<concat> not supported for type: " + left->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    return OperatorReturnCode::OK;
}

OperatorReturnCode __append__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    if (withIns.size() != 1 || normIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<append> operator requires one with argument and one element to append");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const auto &collectNode = withIns[0];
    const data_ptr_t &collection = frame.get(collectNode);
    const data_ptr_t &element = frame.get(normIns[0]);

    switch (collection->type()->code()) {
    case TypeCode::Array: {
        auto vec = tt::as_shared<ArrayData>(collection)->raw();
        vec.push_back(element);
        auto elemType = tt::as_shared<ArrayType>(collection->type())->elementType();
        frame.set(collectNode, ArrayData::create(Type::Array(elemType), std::move(vec)));
        break;
    }
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<append> not supported for type: " + collection->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self, Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __extend__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    if (withIns.size() != 1 || normIns.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                "<extend> operator requires one with argument and one collection to extend from");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const auto &collectNode = withIns[0];
    const data_ptr_t &collection = frame.get(collectNode);
    const data_ptr_t &other = frame.get(normIns[0]);

    if (collection->type()->code() != other->type()->code()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<extend> operator requires both operands to be of same type");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    switch (collection->type()->code()) {
    case TypeCode::Array: {
        auto vec = tt::as_shared<ArrayData>(collection)->raw();
        auto ext = tt::as_shared<ArrayData>(other)->raw();
        vec.insert(vec.end(), ext.begin(), ext.end());
        auto elemType = tt::as_shared<ArrayType>(collection->type())->elementType();
        frame.set(collectNode, ArrayData::create(Type::Array(elemType), std::move(vec)));
        break;
    }
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<extend> not supported for type: " + collection->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self, Data::null());
    return OperatorReturnCode::OK;
}

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
    case TypeCode::Array:
        checkInListLike(tt::as_shared<ArrayData>(collection));
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
