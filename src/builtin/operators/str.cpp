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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "str.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"
#include "core/type/composite/array.h"

#include "fmt/args.h"
#include "fmt/core.h"

#include <sstream>

slot_t __format__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *fmtStrObj = norm.get<String *>(0);

    std::string fmtStr = fmtStrObj->toString();

    fmt::dynamic_format_arg_store<fmt::format_context> store;

    for (size_t i = 0; i < with.size(); ++i) {
        TypeCode type = with.code(i);

        switch (type) {
        case TypeCode::Int32:
            store.push_back(with.get<Int32>(i));
            break;
        case TypeCode::Int64:
            store.push_back(with.get<Int64>(i));
            break;
        case TypeCode::Float32:
            store.push_back(with.get<Float32>(i));
            break;
        case TypeCode::Float64:
            store.push_back(with.get<Float64>(i));
            break;
        default:
            // fallback to string
            std::ostringstream oss;
            printSlot(oss, with.slot(i), with.type(i));
            store.push_back(oss.str());
            break;
        }
    }

    try {
        std::string resultStr = fmt::vformat(fmtStr, store);
        String *resultObj     = String::from(resultStr, mm::autoSpace());
        return toSlot(resultObj);
    } catch (const fmt::format_error &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("<format>") + std::string(e.what()));
        return NullSlot;
    }
}

slot_t __join__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *sepObj = with.get<String *>(0);
    if (!sepObj) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<join>", "invalid separator");
        return NullSlot;
    }
    std::string separator = sepObj->toString();

    Array *arrObj            = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    Type *elemType           = arrType->elemType();

    std::ostringstream joined;
    size_t len = arrObj->size();

    for (size_t i = 0; i < len; ++i) {
        if (i > 0)
            joined << separator;
        slot_t slot = arrObj->get<slot_t>(i);
        printSlot(joined, slot, elemType);
    }

    String *resultObj = String::from(joined.str(), mm::autoSpace());
    return toSlot(resultObj);
}
