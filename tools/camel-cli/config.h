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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#ifndef CONFIG_H
#define CONFIG_H
#endif

#define VERSION "0.1.20"

#include <string>

enum class Command { Run, Info, Check, Inspect };

bool parseArgs(int argc, char *argv[]);

namespace CmdLineArgs {
extern Command selectedCommand;

namespace Global {
/// Filled by clipp for `--log-level` (for help/validation); effective threshold follows argv scan
/// in config.cpp.
extern std::string logLevelArg;
} // namespace Global

namespace Run {
extern std::string outputFile;
extern std::string errorFormat;
extern std::string stdLibPath;
extern std::vector<std::string> scriptsDirs;
extern std::vector<std::string> targetFiles;
extern std::vector<std::string> passes; // from --passes (comma-separated) or positional
extern std::string passesOpt;           // raw --passes value (e.g. "a,b,c")
extern std::string inputFile;           // input from --input (alternative to first positional)
// Resolved after parse (used by main): no need to branch on --input/--passes vs positional
extern std::string resolvedInputPath;             // effective input file path (or "" for stdin)
extern std::vector<std::string> resolvedPassList; // effective pass list for run
extern std::vector<std::string> fallbackPasses;   // run when graph != null after list; from rc
                                                  // passes.fallback or ["std::default"]

extern bool profile;
extern bool noCache;
extern bool semanticOnly;
extern unsigned int repeat;
extern int maxThreads;
} // namespace Run

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
extern bool dumpCST;    // Whether to dump Concrete Syntax Tree
extern bool dumpAST;    // Whether to dump Abstract Syntax Tree
extern bool dumpGCT;    // Whether to dump Graph Construction Tree
extern bool dumpGIR;    // Whether to dump Graph Intermediate Representation
extern bool dumpTNS;    // Whether to dump Topological Node Sequence
extern bool geneCode;   // Whether to dump generate code from GIR
extern int passUntil;   // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

namespace CLI = CmdLineArgs;
