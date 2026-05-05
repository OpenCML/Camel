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
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 *
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/init.h"
#include "camel/parse/parse.h"
#include "camel/utils/log.h"
#include "fmt.h"
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace camel::core::error;
using namespace camel::parse;
namespace fs = std::filesystem;

namespace {

void printUsage(std::ostream &os) {
    os << "Usage: camel-format [options] <file>\n"
          "  -i, --inplace     write back to file\n"
          "  -t, --tab-size N  indent size (default 4)\n"
          "  -u, --use-tabs    use tabs\n"
          "  -q, --quote-prefer single|double\n"
          "  -m, --max-width N (default 80)\n";
}

bool parseUnsignedOption(const std::string &name, const std::string &value, unsigned &out) {
    try {
        size_t parsed  = 0;
        const auto raw = std::stoul(value, &parsed);
        if (parsed != value.size() || raw == 0 || raw > std::numeric_limits<unsigned>::max()) {
            std::cerr << "camel-format: invalid value for " << name << ": " << value << "\n";
            return false;
        }
        out = static_cast<unsigned>(raw);
        return true;
    } catch (const std::exception &) {
        std::cerr << "camel-format: invalid value for " << name << ": " << value << "\n";
        return false;
    }
}

} // namespace

int main(int argc, char *argv[]) {
    camel::ScopedRuntime camelRuntime;
    std::string inputPath;
    bool inplace            = false;
    unsigned tabSize        = 4;
    bool useTabs            = false;
    std::string quotePrefer = "single";
    unsigned maxWidth       = 80;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-i" || arg == "--inplace") {
            inplace = true;
        } else if (arg == "-t" || arg == "--tab-size") {
            if (i + 1 >= argc || !parseUnsignedOption(arg, argv[++i], tabSize)) {
                return 1;
            }
        } else if (arg == "-u" || arg == "--use-tabs") {
            useTabs = true;
        } else if (arg == "-q" || arg == "--quote-prefer") {
            if (i + 1 >= argc) {
                std::cerr << "camel-format: missing value for " << arg << "\n";
                return 1;
            }
            quotePrefer = argv[++i];
            if (quotePrefer != "single" && quotePrefer != "double") {
                std::cerr << "camel-format: --quote-prefer must be single or double\n";
                return 1;
            }
        } else if (arg == "-m" || arg == "--max-width") {
            if (i + 1 >= argc || !parseUnsignedOption(arg, argv[++i], maxWidth)) {
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            printUsage(std::cerr);
            return 0;
        } else if (arg[0] != '-') {
            inputPath = arg;
            break;
        } else {
            std::cerr << "camel-format: unknown option: " << arg << "\n";
            return 1;
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
    try {
        if (!parser->parseCST(file)) {
            parser->dumpDiagnostics(std::cerr);
            return 1;
        }
    } catch (Diagnostic &d) {
        RangeConverter conv(parser->getTokens());
        d.fetchRange(conv);
        std::cerr << "Uncaught diagnostic: " << d.toText() << "\n";
        return 1;
    } catch (DiagnosticsLimitExceededBaseException &) {
        parser->dumpDiagnostics(std::cerr);
        return 1;
    }
    file.close();

    Formatter::Options options;
    options.indent      = useTabs ? "\t" : std::string(tabSize, ' ');
    options.quotePrefer = quotePrefer;

    (void)maxWidth;

    std::string out;
    try {
        Formatter formatter(parser->getTokens(), options);
        out = std::any_cast<std::string>(formatter.visit(parser->cst()));
    } catch (const std::exception &e) {
        std::cerr << "camel-format: failed to format: " << e.what() << "\n";
        return 1;
    }

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
