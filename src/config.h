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
 * Created: Mar. 17, 2024
 * Updated: Mar. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#ifndef CONFIG_H
#define CONFIG_H
#endif

#define VERSION "0.0.17"

#include <string>

enum class Command { Run, Info, Format, Check, Inspect };

bool parseArgs(int argc, char *argv[]);

namespace CmdLineArgs {
extern Command selectedCommand;

namespace Run {
extern std::string outputFile;
extern std::string errorFormat;
extern std::string stdLibPath;
extern std::vector<std::string> scriptsDirs;
extern std::vector<std::string> targetFiles;

extern bool profile;
extern bool noCache;
extern bool semanticOnly;
extern unsigned int repeat;
extern int maxThreads;
} // namespace Run

namespace Format {
extern std::string quotePrefer; // Quote preference (default to single quotes)
extern std::string configFile;  // Configuration file path
extern bool useTabs;            // Whether to use tabs for indentation
extern bool inplace;            // Whether to modify the input file in place
extern bool ignoreDefiFile;     // Whether to ignore the definition file
extern unsigned int tabSize;    // Indentation size in spaces
extern unsigned int maxWidth;   // Max line width
}; // namespace Format

namespace Check {
extern bool lexical;               // Whether to check lexical errors
extern bool syntaxOnly;            // Whether to check syntax only
extern bool ignoreDefiFile;        // Whether to ignore the definition file
extern std::string outputFormat;   // Output format (default to JSON)
extern std::string configFilePath; // Configuration file path
extern unsigned int maxWaring;     // Max warnings
} // namespace Check

namespace Inspect {
extern bool dumpTokens; // Whether to dump tokens
extern bool dumpCST;    // Whether to dump CST
extern bool dumpAST;    // Whether to dump AST
extern bool dumpGCT;    // Whether to dump GCT
extern bool dumpGIR;    // Whether to dump GIR
extern int passUntil;   // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

namespace CLI = CmdLineArgs;
