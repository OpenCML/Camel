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
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#ifndef CONFIG_H
#define CONFIG_H
#endif

#define VERSION "0.2.24"

#include <string>
#include <vector>

enum class Command { Run, Info, Check, Inspect };

bool parseArgs(int argc, char *argv[]);
/// The exit code of the process when parseArgs returns false (an info command such as
/// --explain can fail; a usage error is reported by the caller as before).
extern int exitCode;

namespace CmdLineArgs {
extern Command selectedCommand;

namespace Global {
/// Filled by clipp for `--log-level` (for help/validation); effective threshold follows argv scan
/// in config.cpp.
extern std::string logLevelArg;
} // namespace Global

/// How diagnostics are reported; shared by the run, check and inspect commands.
namespace Diag {
/// "text" (source excerpts), "short" (one line per diagnostic) or "json". Resolved after
/// parsing: the option, else the environment variable CAMEL_ERROR_FORMAT, else "text".
extern std::string format;
/// --color auto|always|never (default auto: colours only on a terminal).
extern std::string color;
/// --max-errors <n>: compile errors reported before the compiler gives up (0: no limit).
extern int maxErrors;
/// -N / --max-warning <n>: warnings shown (the rest are counted, not printed).
extern int maxWarnings;
/// The -W... arguments in command-line order, without the leading "-W".
extern std::vector<std::string> warningSpecs;
/// --explain <name|code>: the catalog entry to print ("" = none).
extern std::string explain;
} // namespace Diag

namespace Run {
extern std::string outputFile;
extern std::string errorFormat;
extern std::string stdLibPath;
extern std::vector<std::string> scriptsDirs;
extern std::vector<std::string> targetFiles;
extern std::vector<std::string> passes; // from --passes (comma-separated) or positional
extern std::string passesOpt;           // raw --passes value (e.g. "a,b,c")
extern std::string timeitSpec;          // raw --timeit value (e.g. "std::jit/10")
extern std::string inputFile;           // input from --input (alternative to first positional)
// Resolved after parse (used by main): no need to branch on --input/--passes vs positional
extern std::string resolvedInputPath;             // effective input file path (or "" for stdin)
extern std::vector<std::string> resolvedPassList; // effective pass list for run
extern std::vector<std::string> fallbackPasses;   // run when graph != null after list; from rc
                                                  // passes.fallback or ["std::default"]
extern std::string timeitPass;                    // parsed target pass from --timeit
extern unsigned int timeitIterations;             // parsed iteration count from --timeit
extern bool timeitEnabled;
/// --dump-after-each-pass <dir>: directory for per-pass graph snapshots ("" = disabled).
extern std::string dumpAfterEachPassDir;
/// --arg name=value (repeatable): values for the parameters of `main`.
extern std::vector<std::string> entryArgs;

extern bool profile;
extern bool noCache;
extern bool semanticOnly;
extern int maxThreads;
/// --max-recursion-depth / --max-macro-depth: the limits given on the command line, 0 when the
/// option is absent (camel/execute/limits.h; the environment variables are read there).
extern size_t maxRecursionDepth;
extern size_t maxMacroDepth;
extern size_t maxMacroRounds; // --max-macro-rounds
/// --block-limit <megabytes>: the limit of the data-block space, "" when absent.
extern std::string blockLimitOpt;
/// --graph-gc-mb <megabytes>: the floor of the graph-space collection, "" when absent.
extern std::string graphGcMbOpt;
/// --native-stack-guard <on|off>: the checks of the native stack; "" (on) when absent.
extern std::string nativeStackGuardOpt;
/// --devices <cpu|ordinal[,ordinal...]>: the devices a pipeline may use; "" when absent.
extern std::string devicesOpt;
/// --profile-mode <normal|full>: the mode of the advanced profiler of --profile.
extern std::string profileModeOpt;
} // namespace Run

namespace Check {
extern bool lexical;               // Whether to check lexical errors
extern bool syntaxOnly;            // Whether to check syntax only
extern bool ignoreDefiFile;        // Whether to ignore the definition file
extern std::string outputFormat;   // Output format (default to JSON)
extern std::string configFilePath; // Configuration file path
} // namespace Check

namespace Inspect {
extern bool dumpTokens;    // Whether to dump tokens
extern bool dumpCST;       // Whether to dump Concrete Syntax Tree
extern bool dumpAST;       // Whether to dump Abstract Syntax Tree
extern bool dumpGCT;       // Whether to dump Graph Construction Tree
extern bool dumpGIR;       // Whether to dump Graph Intermediate Representation
extern bool dumpTNS;       // Whether to dump Topological Node Sequence
extern bool dumpOps;       // Whether to dump the operators of the imported modules
extern bool dumpInstances; // Whether to dump the index semantics of every operator node
extern bool dumpFacts;     // Whether to compute and dump the graph facts of the program
extern bool
    dumpTags; // Whether to dump the tags the program declares and the facts on its declarations
extern bool geneCode; // Whether to dump generate code from GIR
extern int passUntil; // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

namespace CLI = CmdLineArgs;
