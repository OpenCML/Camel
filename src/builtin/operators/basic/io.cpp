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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "io.h"

#include <iostream>

data_ptr_t __print__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    for (size_t i = 0; i < with.size(); i++) {
        if (i > 0) {
            std::cout << " ";
        }
        std::cout << with[i]->toString();
    }
    for (size_t i = 0; i < norm.size(); i++) {
        if (i > 0) {
            std::cout << " ";
        }
        std::cout << norm[i]->toString();
    }
    return Data::null();
}

data_ptr_t __println__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    for (size_t i = 0; i < with.size(); i++) {
        if (i > 0) {
            std::cout << " ";
        }
        std::cout << with[i]->toString();
    }
    for (size_t i = 0; i < norm.size(); i++) {
        if (i > 0) {
            std::cout << " ";
        }
        std::cout << norm[i]->toString();
    }
    std::cout << std::endl;
    return Data::null();
}

data_ptr_t __input__(Context &ctx, data_vec_t &with, data_vec_t &norm) {
    std::string prompt;

    for (size_t i = 0; i < with.size(); i++) {
        if (i > 0) {
            prompt += " ";
        }
        prompt += with[i]->toString();
    }
    for (size_t i = 0; i < norm.size(); i++) {
        if (i > 0 || !prompt.empty()) {
            prompt += " ";
        }
        prompt += norm[i]->toString();
    }

    if (!prompt.empty()) {
        std::cout << prompt;
    }

    std::string input;
    std::getline(std::cin, input);

    return std::make_shared<StringData>(input);
}
