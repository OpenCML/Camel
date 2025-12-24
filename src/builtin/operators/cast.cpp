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
 * Updated: Dec. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "cast.h"
#include "core/context/context.h"

void __itoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get<Int32>(nargs[0]));
}

void __ltoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int64 v = frame.get<Int64>(nargs[0]);
    frame.set(self, static_cast<Int32>(v));
}

void __ftoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float32 v = frame.get<Float32>(nargs[0]);
    frame.set(self, static_cast<Int32>(v));
}

void __dtoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float64 v = frame.get<Float64>(nargs[0]);
    frame.set(self, static_cast<Int32>(v));
}

void __stoi__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    try {
        Int32 v = std::stoi(s->toString());
        frame.set(self, v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stoi__ invalid integer string");
        frame.set(self, NullSlot);
    }
}

void __itol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int32 v = frame.get<Int32>(nargs[0]);
    frame.set(self, static_cast<Int64>(v));
}

void __ltol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get<Int64>(nargs[0]));
}

void __ftol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float32 v = frame.get<Float32>(nargs[0]);
    frame.set(self, static_cast<Int64>(v));
}

void __dtol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float64 v = frame.get<Float64>(nargs[0]);
    frame.set(self, static_cast<Int64>(v));
}

void __stol__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    try {
        Int64 v = std::stoll(s->toString());
        frame.set(self, v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stol__ invalid integer string");
        frame.set(self, NullSlot);
    }
}

void __itof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int32 v = frame.get<Int32>(nargs[0]);
    frame.set(self, static_cast<Float32>(v));
}

void __ltof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int64 v = frame.get<Int64>(nargs[0]);
    frame.set(self, static_cast<Float32>(v));
}

void __ftof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get<Float32>(nargs[0]));
}

void __dtof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float64 v = frame.get<Float64>(nargs[0]);
    frame.set(self, static_cast<Float32>(v));
}

void __stof__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    try {
        Float32 v = std::stof(s->toString());
        frame.set(self, v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stof__ invalid float string");
        frame.set(self, NullSlot);
    }
}

void __itod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int32 v = frame.get<Int32>(nargs[0]);
    frame.set(self, static_cast<Float64>(v));
}

void __ltod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int64 v = frame.get<Int64>(nargs[0]);
    frame.set(self, static_cast<Float64>(v));
}

void __ftod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float32 v = frame.get<Float32>(nargs[0]);
    frame.set(self, static_cast<Float64>(v));
}

void __dtod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get<Float64>(nargs[0]));
}

void __stod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    try {
        Float64 v = std::stod(s->toString());
        frame.set(self, v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stod__ invalid double string");
        frame.set(self, NullSlot);
    }
}

void __itos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int32 v   = frame.get<Int32>(nargs[0]);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    frame.set(self, s);
}

void __ltos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int64 v   = frame.get<Int64>(nargs[0]);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    frame.set(self, s);
}

void __ftos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float32 v = frame.get<Float32>(nargs[0]);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    frame.set(self, s);
}

void __dtos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float64 v = frame.get<Float64>(nargs[0]);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    frame.set(self, s);
}

void __stos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    frame.set(self, frame.get<String *>(nargs[0]));
}

void __atos__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *s = frame.get<String *>(nargs[0]);
    std::ostringstream oss;
    oss << s->toString();
    String *res = String::from(oss.str(), mm::autoSpace());
    frame.set(self, res);
}
