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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "io.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"

#include "fmt/args.h"
#include "fmt/core.h"

#include <iostream>
#include <sstream>

slot_t __print__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t result = NullSlot;
    if (with.size() > 0) {
        String *fmtStrObj  = with.get<String *>(0);
        std::string fmtStr = fmtStrObj->toString();

        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (size_t i = 0; i < norm.size(); ++i) {
            TypeCode code = norm.code(i);
            switch (code) {
            case TypeCode::Int32:
                store.push_back(norm.get<Int32>(i));
                break;
            case TypeCode::Int64:
                store.push_back(norm.get<Int64>(i));
                break;
            case TypeCode::Float32:
                store.push_back(norm.get<Float32>(i));
                break;
            case TypeCode::Float64:
                store.push_back(norm.get<Float64>(i));
                break;
            default:
                std::ostringstream oss;
                printSlot(oss, norm.slot(i), code);
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

        if (norm.size() > 0) {
            result = norm.slot(0);
        }
    } else {
        for (size_t i = 0; i < norm.size(); i++) {
            if (i > 0) {
                std::cout << " ";
            } else {
                result = norm.slot(i);
            }
            TypeCode code = norm.code(i);
            slot_t data   = norm.slot(i);
            printSlot(std::cout, data, code);
        }
    }

    return result;
}

slot_t __println__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t result = NullSlot;
    if (with.size() > 0) {
        String *fmtStrObj  = with.get<String *>(0);
        std::string fmtStr = fmtStrObj->toString();

        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (size_t i = 0; i < norm.size(); ++i) {
            TypeCode code = norm.code(i);
            switch (code) {
            case TypeCode::Int32:
                store.push_back(norm.get<Int32>(i));
                break;
            case TypeCode::Int64:
                store.push_back(norm.get<Int64>(i));
                break;
            case TypeCode::Float32:
                store.push_back(norm.get<Float32>(i));
                break;
            case TypeCode::Float64:
                store.push_back(norm.get<Float64>(i));
                break;
            default:
                std::ostringstream oss;
                printSlot(oss, norm.slot(i), code);
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

        if (norm.size() > 0) {
            result = norm.slot(0);
        }
    } else {
        for (size_t i = 0; i < norm.size(); i++) {
            if (i > 0) {
                std::cout << " ";
            } else {
                result = norm.slot(i);
            }
            TypeCode code = norm.code(i);
            slot_t data   = norm.slot(i);
            printSlot(std::cout, data, code);
        }
    }

    std::cout << std::endl;
    return result;
}

slot_t __input__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::stringstream oss;

    for (size_t i = 0; i < norm.size(); i++) {
        if (i > 0) {
            oss << " ";
        }
        TypeCode code = norm.code(i);
        slot_t data   = norm.slot(i);
        printSlot(oss, data, code);
    }

    std::cout << oss.str();

    std::string input;
    std::getline(std::cin, input);

    String *str = String::from(input, mm::autoSpace());
    return toSlot(str);
}
