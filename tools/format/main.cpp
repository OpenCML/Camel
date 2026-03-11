/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of
 * the MIT license at: [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT
 * WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 *
 */

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/init.h"
#include "camel/parse/parse.h"
#include "camel/utils/log.h"
#include "fmt.h"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace camel::core::error;
using namespace camel::parse;
namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    camel::initialize();
    std::string inputPath;
    bool inplace                       = false;
    [[maybe_unused]] unsigned tabSize  = 4;
    [[maybe_unused]] bool useTabs      = false;
    std::string quotePrefer            = "single";
    [[maybe_unused]] unsigned maxWidth = 80;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" || arg == "--inplace")
            inplace = true;
        else if ((arg == "-t" || arg == "--tab-size") && i + 1 < argc) {
            tabSize = (unsigned)std::stoul(argv[++i]);
        } else if (arg == "-u" || arg == "--use-tabs")
            useTabs = true;
        else if ((arg == "-q" || arg == "--quote-prefer") && i + 1 < argc) {
            quotePrefer = argv[++i];
        } else if ((arg == "-m" || arg == "--max-width") && i + 1 < argc) {
            maxWidth = (unsigned)std::stoul(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: camel-format [options] <file>\n"
                         "  -i, --inplace     write back to file\n"
                         "  -t, --tab-size N  indent size (default 4)\n"
                         "  -u, --use-tabs    use tabs\n"
                         "  -q, --quote-prefer single|double\n"
                         "  -m, --max-width N (default 80)\n";
            return 0;
        } else if (arg[0] != '-') {
            inputPath = arg;
            break;
        }
    }

    if (inputPath.empty()) {
        std::cerr << "camel-format: missing input file\n";
        return 1;
    }

    fs::path path(inputPath);
    if (!fs::exists(path)) {
        std::cerr << "camel-format: file not found: " << inputPath << "\n";
        return 1;
    }

    auto diagnostics = std::make_shared<Diagnostics>("camel-format", inputPath);
    auto parser      = std::make_shared<CamelParser>(diagnostics);
    std::ifstream file(inputPath);
    if (!file) {
        std::cerr << "camel-format: cannot open: " << inputPath << "\n";
        return 1;
    }
    parser->parse(file);
    file.close();

    Formatter formatter(parser->getTokens());
    std::string out = std::any_cast<std::string>(formatter.visit(parser->cst()));

    if (inplace) {
        std::ofstream outFile(inputPath);
        if (!outFile) {
            std::cerr << "camel-format: cannot write: " << inputPath << "\n";
            return 1;
        }
        outFile << out;
    } else {
        std::cout << out;
    }
    return 0;
}
