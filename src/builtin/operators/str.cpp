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
 * Created: Jul. 29, 2025
 * Updated: Dec. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "str.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include "fmt/args.h"
#include "fmt/core.h"

#include <sstream>

namespace GIR = GraphIR;

std::string format_vector(const std::string &fmtStr, const std::vector<std::string> &args) {
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (const auto &arg : args) {
        store.push_back(fmt::string_view(arg));
    }
    return fmt::vformat(fmtStr, store);
}

void __format__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *fmtStrObj = frame.get<String *>(nargs[0]);

    std::string fmtStr = fmtStrObj->toString();

    std::vector<std::string> argStrs;
    argStrs.reserve(wargs.size);

    for (size_t i = 0; i < wargs.size; ++i) {
        std::ostringstream oss;
        TypeCode type = frame.typeAt(wargs[i]);
        slot_t slot   = frame.get<slot_t>(wargs[i]);
        printSlot(oss, slot, type);
        argStrs.push_back(oss.str());
    }

    try {
        std::string resultStr = format_vector(fmtStr, argStrs);
        String *resultObj     = String::from(resultStr, mm::autoSpace());
        frame.set(self, resultObj);
    } catch (const fmt::format_error &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("<format>") + std::string(e.what()));
        frame.set(self, NullSlot);
    }

    return;
}

void __join__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *sepObj = frame.get<String *>(wargs[0]);
    if (!sepObj) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<join>", "invalid separator");
        frame.set(self, NullSlot);
        return;
    }
    std::string separator = sepObj->toString();

    Array *arrObj = frame.get<Array *>(nargs[0]);

    std::ostringstream joined;
    size_t len = arrObj->size();

    TypeCode elemType = arrObj->elemType();

    if (isGCTraced(elemType)) {
        // 引用类型
        for (size_t i = 0; i < len; ++i) {
            if (i > 0)
                joined << separator;
            joined << arrObj->get<Object *>(i);
        }
    } else {
        // 非引用类型
        for (size_t i = 0; i < len; ++i) {
            if (i > 0)
                joined << separator;
            slot_t slot = arrObj->get<slot_t>(i);
            printSlot(joined, slot, elemType);
        }
    }

    String *resultObj = String::from(joined.str(), mm::autoSpace());
    frame.set(self, resultObj);
}
