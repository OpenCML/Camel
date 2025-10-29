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
 * Created: Oct. 29, 2025
 * Updated: Oct. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "cast.h"

void __itoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get(nargs[0]));
}

void __ltoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<LongData>(Type::Long())->data();
    frame.set(self, std::make_shared<IntData>(val));
}

void __ftoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<FloatData>(Type::Float())->data();
    frame.set(self, std::make_shared<IntData>(static_cast<int32_t>(val)));
}

void __dtoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<DoubleData>(Type::Double())->data();
    frame.set(self, std::make_shared<IntData>(static_cast<int32_t>(val)));
}

void __stoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto strData = src->as<StringData>(Type::String());
    int32_t val = std::stoi(strData->data());
    frame.set(self, std::make_shared<IntData>(val));
}

void __itol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<IntData>(Type::Int())->data();
    frame.set(self, std::make_shared<LongData>(static_cast<int64_t>(val)));
}

void __ltol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get(nargs[0]));
}

void __ftol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<FloatData>(Type::Float())->data();
    frame.set(self, std::make_shared<LongData>(static_cast<int64_t>(val)));
}

void __dtol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<DoubleData>(Type::Double())->data();
    frame.set(self, std::make_shared<LongData>(static_cast<int64_t>(val)));
}

void __stol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto strData = src->as<StringData>(Type::String());
    int64_t val = std::stoll(strData->data());
    frame.set(self, std::make_shared<LongData>(val));
}

void __itof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<IntData>(Type::Int())->data();
    frame.set(self, std::make_shared<FloatData>(static_cast<float>(val)));
}

void __ltof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<LongData>(Type::Long())->data();
    frame.set(self, std::make_shared<FloatData>(static_cast<float>(val)));
}

void __ftof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get(nargs[0]));
}

void __dtof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<DoubleData>(Type::Double())->data();
    frame.set(self, std::make_shared<FloatData>(static_cast<float>(val)));
}

void __stof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto strData = src->as<StringData>(Type::String());
    float val = std::stof(strData->data());
    frame.set(self, std::make_shared<FloatData>(val));
}

void __itod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<IntData>(Type::Int())->data();
    frame.set(self, std::make_shared<DoubleData>(static_cast<double>(val)));
}

void __ltod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<LongData>(Type::Long())->data();
    frame.set(self, std::make_shared<DoubleData>(static_cast<double>(val)));
}

void __ftod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<FloatData>(Type::Float())->data();
    frame.set(self, std::make_shared<DoubleData>(static_cast<double>(val)));
}

void __dtod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get(nargs[0]));
}

void __stod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto strData = src->as<StringData>(Type::String());
    double val = std::stod(strData->data());
    frame.set(self, std::make_shared<DoubleData>(val));
}

void __itos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<IntData>(Type::Int())->data();
    frame.set(self, std::make_shared<StringData>(std::to_string(val)));
}

void __ltos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<LongData>(Type::Long())->data();
    frame.set(self, std::make_shared<StringData>(std::to_string(val)));
}

void __ftos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<FloatData>(Type::Float())->data();
    frame.set(self, std::make_shared<StringData>(std::to_string(val)));
}

void __dtos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<DoubleData>(Type::Double())->data();
    frame.set(self, std::make_shared<StringData>(std::to_string(val)));
}

void __stos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get(nargs[0]));
}

void __atos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &src = frame.get(nargs[0]);
    auto val = src->as<StringData>(Type::String())->data();
    std::ostringstream oss;
    oss << val;
    frame.set(self, std::make_shared<StringData>(oss.str()));
}
