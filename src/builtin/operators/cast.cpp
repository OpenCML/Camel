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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "cast.h"
#include "core/context/context.h"
#include <sstream>

slot_t __itoi__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __ltoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(static_cast<Int32>(v));
}

slot_t __ftoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(static_cast<Int32>(v));
}

slot_t __dtoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(static_cast<Int32>(v));
}

slot_t __stoi__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    try {
        Int32 v = std::stoi(s->toString());
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stoi__ invalid integer string");
        return NullSlot;
    }
}

slot_t __itol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Int64>(v));
}

slot_t __ltol__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __ftol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(static_cast<Int64>(v));
}

slot_t __dtol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(static_cast<Int64>(v));
}

slot_t __stol__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    try {
        Int64 v = std::stoll(s->toString());
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stol__ invalid integer string");
        return NullSlot;
    }
}

slot_t __itof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __ltof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __ftof__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __dtof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    return toSlot(static_cast<Float32>(v));
}

slot_t __stof__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    try {
        Float32 v = std::stof(s->toString());
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stof__ invalid float string");
        return NullSlot;
    }
}

slot_t __itod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v = norm.get<Int32>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __ltod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v = norm.get<Int64>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __ftod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    return toSlot(static_cast<Float64>(v));
}

slot_t __dtod__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __stod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    try {
        Float64 v = std::stod(s->toString());
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("__stod__ invalid double string");
        return NullSlot;
    }
}

slot_t __itos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 v   = norm.get<Int32>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __ltos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 v   = norm.get<Int64>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __ftos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 v = norm.get<Float32>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __dtos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 v = norm.get<Float64>(0);
    String *s = String::from(std::to_string(v), mm::autoSpace());
    return toSlot(s);
}

slot_t __stos__(ArgsView &with, ArgsView &norm, Context &ctx) { return norm.slot(0); }

slot_t __atos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *s = norm.get<String *>(0);
    std::ostringstream oss;
    oss << s->toString();
    String *res = String::from(oss.str(), mm::autoSpace());
    return toSlot(res);
}
