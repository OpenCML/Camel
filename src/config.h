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
enum class Command { help, format, check, inspect, build, serve, debug }; // Modes of operation
extern std::string targetFile;
extern std::string outputFile;

extern std::string errorFormat;

extern std::vector<std::string> scriptsDirs;

extern bool profile;
extern bool noCache;

extern bool semanticOnly;

extern unsigned int repeat;

extern unsigned int maxThreads;

bool parseArgs(int argc, char *argv[]);

namespace Format {
std::string quotePrefer = "single"; // Quote preference (default to single quotes)
std::string configFile = "";        // Configuration file path
bool useTabs = false;               // Whether to use tabs for indentation
bool inplace = false;               // Whether to modify the input file in place
bool ignore = false;                // Whether to ignore the definition file
unsigned int tabSize = 4;           // Indentation size in spaces
unsigned int maxWidth = 80;         // Max line width
}; // namespace Format
namespace Check {
bool lexical = false;          // Whether to check lexical errors
bool syntaxOnly = false;       // Whether to check syntax only
bool ignore = false;           // Whether to ignore the definition file
string outputFormat = "json";  // Output format (default to JSON)
string configFilePath = "";    // Configuration file path
unsigned int maxWaring = 1000; // Max warnings
} // namespace Check
namespace Inspect {
bool dumpTokens = false; // Whether to dump tokens
bool dumpCST = false;    // Whether to dump CST
bool dumpAST = false;    // Whether to dump AST
bool dumpGCT = false;    // Whether to dump GCT
bool dumpGIR = false;    // Whether to dump GIR
bool dumpONNX = false;   // Whether to dump ONNX
int passUntil = -1;      // Pass until the given pass
} // namespace Inspect
namespace Build {
bool optimize = false;                     // Whether to optimize the code
bool rollup = false;                       // Whether to rollup the code
bool verbose = false;                      // Whether to show verbose information
std::string warningSwitch = "on";          // Warning switch (default to on)
std::string outputDir = "";                // Output directory
std::string serverHost = "";               // Server host
unsigned int serverPort;                   // Server port
} // namespace Build
namespace Debug {
std::string variable = "";                     // Whether to optimize the code
} // namespace Debug