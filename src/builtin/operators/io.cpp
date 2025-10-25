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
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "io.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include <iostream>
#include <sstream>

namespace GIR = GraphIR;

void __print__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    for (size_t i = 0; i < nargs.size; i++) {
        if (i > 0) {
            std::cout << " ";
        }
        const data_ptr_t &data = frame.get(nargs[i]);
        data->print(std::cout);
    }
    frame.set(self, Data::null());
    return;
}

void __println__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    for (size_t i = 0; i < nargs.size; i++) {
        if (i > 0) {
            std::cout << " ";
        }
        const data_ptr_t &data = frame.get(nargs[i]);
        data->print(std::cout);
    }
    std::cout << std::endl;
    frame.set(self, Data::null());
    return;
}

void __input__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::stringstream oss;

    for (size_t i = 0; i < nargs.size; i++) {
        if (i > 0) {
            oss << " ";
        }
        const data_ptr_t &data = frame.get(nargs[i]);
        data->print(oss);
    }

    std::cout << oss.str();

    std::string input;
    std::getline(std::cin, input);

    frame.set(self, std::make_shared<StringData>(input));
    return;
}
