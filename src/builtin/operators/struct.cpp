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
 * Updated: Oct. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "struct.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

OperatorReturnCode __len_str__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &arg = frame.get(ins[0]->index());

    int32_t len = static_cast<int32_t>(tt::as_shared<StringData>(arg)->data().size());

    frame.set(self->index(), std::make_shared<Int32Data>(len));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __len_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &arg = frame.get(ins[0]->index());

    int32_t len = static_cast<int32_t>(tt::as_shared<ArrayData>(arg)->raw().size());

    frame.set(self->index(), std::make_shared<Int32Data>(len));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __zip__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &normIns = self->normInputs();

    if (normIns.size() != 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> operator requires exactly two input sequences");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &a = frame.get(normIns[0]->index());
    const data_ptr_t &b = frame.get(normIns[1]->index());

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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    const data_vec_t &aElems = *aElemsOpt;
    const data_vec_t &bElems = *bElemsOpt;

    if (aElems.size() != bElems.size()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<zip> requires both sequences to have the same length");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    data_vec_t zipped;
    zipped.reserve(aElems.size());

    for (size_t i = 0; i < aElems.size(); ++i) {
        data_vec_t pair{aElems[i], bElems[i]};
        zipped.push_back(ArrayData::from(Type::Array(Type::Any()), std::move(pair)));
    }

    frame.set(
        self->index(),
        ArrayData::from(Type::Array(Type::Array(Type::Any())), std::move(zipped)));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __head_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &normIns = self->normInputs();

    const data_ptr_t &collect = frame.get(normIns[0]->index());

    auto extract_first = [&](const data_vec_t &arr) -> data_ptr_t {
        return arr.empty() ? Data::null() : arr[0];
    };

    frame.set(self->index(), extract_first(tt::as_shared<ArrayData>(collect)->raw()));

    return OperatorReturnCode::OK;
}

OperatorReturnCode __tail_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &normIns = self->normInputs();

    const data_ptr_t &collect = frame.get(normIns[0]->index());

    auto slice_tail = [](const data_vec_t &arr) -> data_vec_t {
        return arr.size() <= 1 ? data_vec_t{} : data_vec_t(arr.begin() + 1, arr.end());
    };

    auto array = tt::as_shared<ArrayData>(collect)->raw();
    auto new_vec = slice_tail(array);
    auto elem_type = tt::as_shared<ArrayType>(collect->type())->elementType();
    frame.set(self->index(), ArrayData::from(Type::Array(elem_type), std::move(new_vec)));

    return OperatorReturnCode::OK;
}

OperatorReturnCode __range__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &startData = frame.get(ins[0]->index());
    const data_ptr_t &stopData = frame.get(ins[1]->index());
    data_ptr_t stepData = nullptr;

    if (ins.size() == 3) {
        stepData = frame.get(ins[2]->index());
    }

    int32_t start = startData->as<Int32Data>(Type::Int32())->data();
    int32_t stop = stopData->as<Int32Data>(Type::Int32())->data();
    int32_t step = stepData ? stepData->as<Int32Data>(Type::Int32())->data() : 1;

    if (step == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<range> step cannot be zero");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    data_vec_t values;

    if ((step > 0 && start < stop) || (step < 0 && start > stop)) {
        for (int32_t i = start; (step > 0 ? i < stop : i > stop); i += step) {
            values.push_back(std::make_shared<Int32Data>(i));
        }
    }

    auto arrayType = Type::Array(Type::Int64());
    auto result = ArrayData::from(arrayType, std::move(values));

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __slice_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    const data_ptr_t &collect = frame.get(normIns[0]->index());
    const data_ptr_t &startArg = frame.get(withIns[0]->index());
    const data_ptr_t &endArg = frame.get(withIns[1]->index());

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

    auto array = tt::as_shared<ArrayData>(collect)->raw();
    slice_range(static_cast<int32_t>(array.size()), start, end);
    data_vec_t sliced(array.begin() + start, array.begin() + end);
    frame.set(
        self->index(),
        ArrayData::from(
            Type::Array(tt::as_shared<ArrayType>(collect->type())->elementType()),
            std::move(sliced)));

    return OperatorReturnCode::OK;
}

OperatorReturnCode __concat_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &normIns = self->normInputs();

    const data_ptr_t &left = frame.get(normIns[0]->index());
    const data_ptr_t &right = frame.get(normIns[1]->index());

    auto l = tt::as_shared<ArrayData>(left)->raw();
    auto r = tt::as_shared<ArrayData>(right)->raw();
    l.insert(l.end(), r.begin(), r.end());
    auto elemType = tt::as_shared<ArrayType>(left->type())->elementType();
    frame.set(self->index(), ArrayData::from(Type::Array(elemType), std::move(l)));

    return OperatorReturnCode::OK;
}

OperatorReturnCode __append_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    const auto &collectNode = normIns[0];
    const data_ptr_t &collection = frame.get(collectNode->index());
    const data_ptr_t &element = frame.get(withIns[0]->index());

    auto arr = tt::as_shared<ArrayData>(collection)->raw();
    arr.push_back(element);
    auto elemType = tt::as_shared<ArrayType>(collection->type())->elementType();
    frame.set(collectNode->index(), ArrayData::from(Type::Array(elemType), std::move(arr)));

    frame.set(self->index(), collection);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __extend_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withIns = self->withInputs();
    const auto &normIns = self->normInputs();

    const auto &collectNode = normIns[0];
    const data_ptr_t &collection = frame.get(collectNode->index());
    const data_ptr_t &other = frame.get(withIns[0]->index());

    auto arr = tt::as_shared<ArrayData>(collection)->raw();
    auto ext = tt::as_shared<ArrayData>(other)->raw();
    arr.insert(arr.end(), ext.begin(), ext.end());
    frame.set(collectNode->index(), collection);

    return OperatorReturnCode::OK;
}

OperatorReturnCode __contains_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &withs = self->withInputs();
    const auto &norms = self->normInputs();

    const data_ptr_t &collection = frame.get(norms[0]->index());
    const data_ptr_t &target = frame.get(withs[0]->index());

    bool found = false;

    for (const auto &item : tt::as_shared<ArrayData>(collection)->raw()) {
        if (item->equals(target)) {
            found = true;
            break;
        }
    }

    frame.set(self->index(), std::make_shared<BoolData>(found));
    return OperatorReturnCode::OK;
}
