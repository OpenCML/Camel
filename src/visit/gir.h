/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 29, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "utils/log.h"

inline void _dumpGIR() {
    std::ifstream file("D:\\Projects\\Camel\\demo\\feat\\gir.txt");

    if (file) {
        std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
        std::cout << content << std::endl;
        file.close();
    } else {
        error << "Error dumping GIR" << std::endl;
    }
}