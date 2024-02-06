/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open Source License].
 * You may obtain a copy of [Open Source License] at: [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 01, 2023
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>
#include <chrono>

#include "antlr4-runtime.h"
#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"

#include <Windows.h>

using namespace antlr4;

int main(int argc, const char *argv[])
{
    std::chrono::high_resolution_clock::time_point startTime, endTime;

    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::string filePath = argv[1];

    for (int i = 0; i < 10; i++)
    {
        startTime = std::chrono::high_resolution_clock::now();
        auto src = std::ifstream();
        src.open(filePath);
        ANTLRInputStream input(src);
        OpenCMLLexer lexer(&input);
        CommonTokenStream tokens(&lexer);
        OpenCMLParser parser(&tokens);
        tree::ParseTree *tree = parser.program();
        endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        std::cout << "Time used " << duration << " ms" << std::endl;
    }

    // auto s = tree->toStringTree(&parser);
    // std::cout << "Parse Tree: " << s << std::endl;

    return 0;
}