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
 * Updated: Dec. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "io.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include "fmt/args.h"
#include "fmt/core.h"

#include <iostream>
#include <sstream>

void __print__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    slot_t result = NullSlot;
    if (wargs.size > 0) {
        String *fmtStrObj  = frame.get<String *>(wargs[0]);
        std::string fmtStr = fmtStrObj->toString();

        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (size_t i = 0; i < nargs.size; ++i) {
            TypeCode type = frame.typeAt(nargs[i]);
            switch (type) {
            case TypeCode::Int32:
                store.push_back(frame.get<Int32>(wargs[i]));
                break;
            case TypeCode::Int64:
                store.push_back(frame.get<Int64>(wargs[i]));
                break;
            case TypeCode::Float32:
                store.push_back(frame.get<Float32>(wargs[i]));
                break;
            case TypeCode::Float64:
                store.push_back(frame.get<Float64>(wargs[i]));
                break;
            default:
                std::ostringstream oss;
                printSlot(oss, frame.get<slot_t>(wargs[i]), type);
                store.push_back(oss.str());
                break;
            }
        }

        try {
            std::string resultStr = fmt::vformat(fmtStr, store);
            std::cout << resultStr << std::endl;
        } catch (const fmt::format_error &e) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit(std::string("<format>") + std::string(e.what()));
        }

        if (nargs.size > 0) {
            result = frame.get<slot_t>(nargs[0]);
        }
    } else {
        for (size_t i = 0; i < nargs.size; i++) {
            if (i > 0) {
                std::cout << " ";
            } else {
                result = frame.get<slot_t>(nargs[i]);
            }
            TypeCode type = frame.typeAt(nargs[i]);
            slot_t data   = frame.get<slot_t>(nargs[i]);
            printSlot(std::cout, data, type);
        }
    }

    frame.set(self, result);
}

void __println__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    slot_t result = NullSlot;
    if (wargs.size > 0) {
        String *fmtStrObj  = frame.get<String *>(wargs[0]);
        std::string fmtStr = fmtStrObj->toString();

        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (size_t i = 0; i < nargs.size; ++i) {
            TypeCode type = frame.typeAt(nargs[i]);
            switch (type) {
            case TypeCode::Int32:
                store.push_back(frame.get<Int32>(wargs[i]));
                break;
            case TypeCode::Int64:
                store.push_back(frame.get<Int64>(wargs[i]));
                break;
            case TypeCode::Float32:
                store.push_back(frame.get<Float32>(wargs[i]));
                break;
            case TypeCode::Float64:
                store.push_back(frame.get<Float64>(wargs[i]));
                break;
            default:
                std::ostringstream oss;
                printSlot(oss, frame.get<slot_t>(wargs[i]), type);
                store.push_back(oss.str());
                break;
            }
        }

        try {
            std::string resultStr = fmt::vformat(fmtStr, store);
            std::cout << resultStr << std::endl;
        } catch (const fmt::format_error &e) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit(std::string("<format>") + std::string(e.what()));
        }

        if (nargs.size > 0) {
            result = frame.get<slot_t>(nargs[0]);
        }
    } else {
        for (size_t i = 0; i < nargs.size; i++) {
            if (i > 0) {
                std::cout << " ";
            } else {
                result = frame.get<slot_t>(nargs[i]);
            }
            TypeCode type = frame.typeAt(nargs[i]);
            slot_t data   = frame.get<slot_t>(nargs[i]);
            printSlot(std::cout, data, type);
        }
    }

    frame.set(self, result);
    std::cout << std::endl;
}

void __input__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::stringstream oss;

    for (size_t i = 0; i < nargs.size; i++) {
        if (i > 0) {
            oss << " ";
        }
        TypeCode type = frame.typeAt(nargs[i]);
        slot_t data   = frame.get<slot_t>(nargs[i]);
        printSlot(oss, data, type);
    }

    std::cout << oss.str();

    std::string input;
    std::getline(std::cin, input);

    frame.set(self, String::from(input, mm::autoSpace()));
}
