/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Mar. 17, 2024
 * Updated: Mar. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#define VERSION "0.0.12"

#include <string>

// enum class mode {run, dump, help};

extern std::string targetFile;
extern std::string outputFile;

extern std::string errorFormat;

extern std::vector<std::string> includeDirs;
extern std::vector<std::string> scriptsDirs;

extern bool profile;
extern bool verbose;
extern bool noCache;

extern bool syntaxOnly;
extern bool semanticOnly;

extern bool dumpCST, dumpAST, dumpGIR, dumpONNX, dumpTokens, formatCode;

extern unsigned int repeat;

extern unsigned int maxThreads;
extern unsigned int serverPort;

bool parseArgs(int argc, char *argv[]);
