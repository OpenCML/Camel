// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <iostream>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "build_stamp.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/execute/devices.h"
#include "camel/execute/limits.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/str.h"
#include "clipp/clipp.h"
#include "config.h"
#include "opencmlrc.h"

#include <filesystem>

using namespace clipp;
using namespace std;
namespace fs = std::filesystem;

namespace {

LogLevel parseLogLevelToken(std::string_view s) {
    std::string t(strutil::trim(s));
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(c));
    });
    if (t == "trace")
        return LogLevel::Trace;
    if (t == "debug")
        return LogLevel::Debug;
    if (t == "info")
        return LogLevel::Info;
    if (t == "warn" || t == "warning")
        return LogLevel::Warn;
    if (t == "fatal")
        return LogLevel::Fatal;
    if (t == "off")
        return LogLevel::Off;
    std::cerr << "Unknown log level: '" << s << "'" << std::endl;
    std::exit(1);
}

LogCategoryPreset parseLogPresetToken(std::string_view s) {
    std::string t(strutil::trim(s));
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(c));
    });
    if (t.empty() || t == "none")
        return LogCategoryPreset::None;
    if (t == "wall")
        return LogCategoryPreset::Wall;
    if (t == "extra")
        return LogCategoryPreset::Extra;
    std::cerr << "Unknown --log-preset: '" << s << "'" << std::endl;
    std::exit(1);
}

std::vector<std::string> splitCommaScopes(std::string_view s) {
    std::vector<std::string> out;
    std::string acc;
    for (char c : s) {
        if (c == ',') {
            std::string p(strutil::trim(acc));
            if (!p.empty())
                out.push_back(std::move(p));
            acc.clear();
        } else {
            acc += c;
        }
    }
    std::string p(strutil::trim(acc));
    if (!p.empty())
        out.push_back(std::move(p));
    return out;
}

bool parseTimeitSpecValue(
    std::string_view spec, std::string &outPassName, unsigned int &outIterations) {
    const std::string trimmed(strutil::trim(spec));
    const size_t slashPos = trimmed.rfind('/');
    if (slashPos == std::string::npos || slashPos == 0 || slashPos + 1 >= trimmed.size()) {
        std::cerr << "Invalid --timeit value: '" << spec
                  << "'. Expected format '<pass>/<iterations>'." << std::endl;
        return false;
    }

    outPassName = strutil::trim(trimmed.substr(0, slashPos));
    const std::string countToken(strutil::trim(trimmed.substr(slashPos + 1)));
    if (outPassName.empty() || countToken.empty()) {
        std::cerr << "Invalid --timeit value: '" << spec
                  << "'. Expected format '<pass>/<iterations>'." << std::endl;
        return false;
    }

    unsigned int parsedIterations = 0;
    const char *begin             = countToken.data();
    const char *end               = countToken.data() + countToken.size();
    auto [ptr, ec]                = std::from_chars(begin, end, parsedIterations);
    if (ec != std::errc() || ptr != end || parsedIterations == 0) {
        std::cerr << "Invalid --timeit iteration count in '" << spec
                  << "'. Iterations must be a positive integer." << std::endl;
        return false;
    }

    outIterations = parsedIterations;
    return true;
}

/// Reads a depth limit given by `option` (its raw text in `optionText`; empty when the option
/// is absent) or else by the environment variable `envName`. `out` gets the option's value;
/// the environment's is read where it is used (camel::execute::recursionLimit) and is only
/// checked here. False, with a message on stderr, when the text in effect is no limit.
bool readDepthLimit(
    const std::string &optionText, const char *option, const char *envName, size_t &out) {
    const auto refuse = [](const char *what, std::string_view text) {
        std::cerr << "Invalid " << what << " value: '" << text
                  << "'. Expected an integer from 1 to " << camel::execute::kMaxDepthLimit << "."
                  << std::endl;
        return false;
    };
    if (!optionText.empty()) {
        const auto value = camel::execute::parseDepthLimit(optionText);
        if (!value) {
            return refuse(option, optionText);
        }
        out = *value;
        return true;
    }
    if (const char *raw = std::getenv(envName)) {
        if (!camel::execute::parseDepthLimit(raw)) {
            return refuse(envName, raw);
        }
    }
    return true;
}

/// As readDepthLimit, for the rounds of a loop (a wider range).
bool readRoundLimit(
    const std::string &optionText, const char *option, const char *envName, size_t &out) {
    const auto refuse = [](const char *what, std::string_view text) {
        std::cerr << "Invalid " << what << " value: '" << text
                  << "'. Expected an integer from 1 to " << camel::execute::kMaxRoundsLimit << "."
                  << std::endl;
        return false;
    };
    if (!optionText.empty()) {
        const auto value = camel::execute::parseRoundLimit(optionText);
        if (!value) {
            return refuse(option, optionText);
        }
        out = *value;
        return true;
    }
    if (const char *raw = std::getenv(envName)) {
        if (!camel::execute::parseRoundLimit(raw)) {
            return refuse(envName, raw);
        }
    }
    return true;
}

/// The largest limit a memory option accepts, in megabytes (1 TiB): a value that is not a
/// positive integer below it is no limit.
constexpr size_t kMaxMemoryOptionMb = size_t{1} << 20;

bool readMegabyteLimit(const std::string &text, const char *option, size_t &out) {
    if (text.empty()) {
        return true;
    }
    unsigned long long megabytes = 0;
    const char *begin            = text.data();
    const char *end              = text.data() + text.size();
    auto [ptr, ec]               = std::from_chars(begin, end, megabytes);
    if (ec != std::errc() || ptr != end || megabytes == 0 || megabytes > kMaxMemoryOptionMb) {
        std::cerr << "Invalid " << option << " value: '" << text
                  << "'. Expected a number of megabytes from 1 to " << kMaxMemoryOptionMb << "."
                  << std::endl;
        return false;
    }
    out = static_cast<size_t>(megabytes);
    return true;
}

/// Left-to-right over argv: last `--log-level` / `-v*` wins; then applies preset/include and stderr
/// sink.
void applyCamelCliLogging(int argc, char **argv) {
    static std::atomic<bool> stderrSinkRegistered{false};

    LogLevel lastLevel      = LogLevel::Fatal;
    std::string lastPreset  = "none";
    std::string lastInclude = "";

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-l" || a == "--log-level") {
            if (i + 1 < argc)
                lastLevel = parseLogLevelToken(argv[++i]);
            continue;
        }
        if (a == "--log-preset") {
            if (i + 1 < argc)
                lastPreset = argv[++i];
            continue;
        }
        if (a == "--log-include") {
            if (i + 1 < argc)
                lastInclude = argv[++i];
            continue;
        }
        if (a == "-vvvv") {
            lastLevel = LogLevel::Trace;
            continue;
        }
        if (a == "-vvv") {
            lastLevel = LogLevel::Debug;
            continue;
        }
        if (a == "-vv") {
            lastLevel = LogLevel::Info;
            continue;
        }
        if (a == "-v" || a == "--verbose") {
            lastLevel = LogLevel::Warn;
            continue;
        }
    }

    Logger::SetGlobalLogThreshold(lastLevel);
    Logger::SetLogCategoryPreset(parseLogPresetToken(lastPreset));
    if (!lastInclude.empty())
        Logger::SetScopeAllowPrefixes(splitCommaScopes(lastInclude));
    else
        Logger::SetScopeAllowPrefixes({});

    if (lastLevel != LogLevel::Off) {
        Logger::SetColorEnabled(true);
        bool expected = false;
        if (stderrSinkRegistered.compare_exchange_strong(expected, true))
            Logger::AddOutputStream(&std::cerr);
    }
}

const char *kWarningOptionsHelp =
    "\nWARNINGS\n"
    "        -Wno-<name>        do not report the warning <name> (e.g. -Wno-ignored-side-effect)\n"
    "        -Wnone             report no warnings\n"
    "        -Werror            report every warning as an error\n"
    "        -Werror=<name>     report the warning <name> as an error\n"
    "        -Wno-error=<name>  keep <name> a warning under -Werror\n"
    "        <name> is a diagnostic name as --explain prints it; case, '-' and '_' are ignored.\n";

/// `--time-phases` is taken out of argv by main() before the parser runs (camel-cli/main.cpp).
const char *kMeasurementOptionsHelp =
    "\nMEASUREMENT\n"
    "        --time-phases         write the time of each phase of the run as JSON to stderr\n"
    "        --time-phases=<file>  write it to <file> (also: CAMEL_TIME_PHASES=<file>); with\n"
    "                              the counting allocator shim preloaded, also the allocations\n";

} // namespace

int exitCode = 0;

namespace CmdLineArgs {
Command selectedCommand = Command::Run;

namespace Global {
string logLevelArg;
} // namespace Global

namespace Diag {
string format               = "";
string color                = "auto";
int maxErrors               = camel::core::error::kDefaultMaxErrors;
int maxWarnings             = 1000;
vector<string> warningSpecs = {};
string explain              = "";
} // namespace Diag

namespace Run {
string outputFile               = "";
string package                  = "";
string schedular                = "";
string errorFormat              = ""; // "" = not given; see Diag::format
string stdLibPath               = ""; // "" means use the default path
vector<string> includeDirs      = {}; // Include directories
vector<string> targetFiles      = {};
vector<string> passes           = {}; // from --passes comma-separated or positional
string passesOpt                = ""; // raw --passes "a,b,c"
string timeitSpec               = ""; // raw --timeit "std::jit/10"
string inputFile                = ""; // from --input (alternative to first positional as file)
string resolvedInputPath        = ""; // computed: inputFile or targetFiles[0]
vector<string> resolvedPassList = {}; // computed: passes or derived from targetFiles
vector<string> fallbackPasses   = {"std::default"};
string timeitPass               = "";
unsigned int timeitIterations   = 0;
bool timeitEnabled              = false;
string dumpAfterEachPassDir     = "";
vector<string> entryArgs        = {};

bool profile      = false;
bool noCache      = false;
bool semanticOnly = false;
int maxThreads    = -1; // -1 means use the maximum number of threads

string maxRecursionDepthOpt = ""; // raw --max-recursion-depth
string maxMacroDepthOpt     = ""; // raw --max-macro-depth
string maxMacroRoundsOpt    = ""; // raw --max-macro-rounds
string gcModeOpt            = ""; // raw --gc
string blockLimitOpt        = ""; // raw --block-limit (megabytes)
string graphGcMbOpt         = ""; // raw --graph-gc-mb (megabytes)
string nativeStackGuardOpt  = ""; // raw --native-stack-guard (on|off)
string devicesOpt           = ""; // raw --devices (cpu|ordinal[,ordinal...])
string profileModeOpt       = ""; // raw --profile-mode (normal|full)
size_t maxRecursionDepth    = 0;  // 0: not given on the command line
size_t maxMacroDepth        = 0;
size_t maxMacroRounds       = 0;
} // namespace Run

namespace Check {
bool lexical          = false; // Whether to check lexical errors
bool syntaxOnly       = false; // Whether to check syntax only
bool ignoreDefiFile   = false; // Whether to ignore the definition file
string outputFormat   = "";    // Output format; "" = not given, see Diag::format
string configFilePath = "";    // Configuration file path
} // namespace Check

namespace Inspect {
bool dumpTokens    = false; // Whether to dump tokens
bool dumpCST       = false; // Whether to dump Concrete Syntax Tree
bool dumpAST       = false; // Whether to dump Abstract Syntax Tree
bool dumpGCT       = false; // Whether to dump Graph Construction Tree
bool dumpGIR       = false; // Whether to dump Graph Intermediate Representation
bool dumpTNS       = false; // Whether to dump Topological Node Sequence
bool dumpOps       = false; // Whether to dump the operators of the imported modules
bool dumpInstances = false; // Whether to dump the index semantics of every operator node
bool dumpFacts     = false; // Whether to compute and dump the graph facts of the program
bool dumpTags      = false; // Whether to dump the tags the program declares
bool dumpAttrs     = false; // Whether to dump the attribute-fate table and check its coverage
bool geneCode      = false; // Whether to generate code from GIR
int passUntil      = -1;    // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

using namespace CmdLineArgs;
using namespace CmdLineArgs::Run;
using namespace CmdLineArgs::Check;
using namespace CmdLineArgs::Inspect;

namespace {

/// Removes the -W... arguments from argv and records them (without "-W") in Diag::warningSpecs.
/// Only arguments in front of the first positional one after which passes follow are options;
/// a pass is never named -W..., so the whole argv is scanned.
std::vector<char *> takeWarningArguments(int argc, char *argv[]) {
    std::vector<char *> rest;
    for (int i = 0; i < argc; ++i) {
        std::string_view a = argv[i];
        if (i > 0 && a.size() > 2 && a.starts_with("-W")) {
            Diag::warningSpecs.emplace_back(a.substr(2));
        } else {
            rest.push_back(argv[i]);
        }
    }
    return rest;
}

/// Turns the diagnostics options into the process-wide render configuration and warning
/// policy. Returns false (after printing why) on a value or a warning name that does not exist.
bool applyDiagnosticOptions() {
    using namespace camel::core::error;

    // Format: -E/--error-format (run) or -O/--output-format (check), else the environment.
    std::string format = selectedCommand == Command::Check ? Check::outputFormat : Run::errorFormat;
    if (format.empty()) {
        if (const char *env = std::getenv("CAMEL_ERROR_FORMAT"); env != nullptr && env[0] != '\0') {
            format = env;
        } else {
            format = "text";
        }
    }
    if (format != "text" && format != "short" && format != "json") {
        std::cerr << "Error: unknown diagnostics format '" << format
                  << "'. Expected text, short or json." << std::endl;
        return false;
    }
    Diag::format = format;

    ColorMode color = ColorMode::Auto;
    if (Diag::color == "always") {
        color = ColorMode::Always;
    } else if (Diag::color == "never") {
        color = ColorMode::Never;
    } else if (Diag::color != "auto") {
        std::cerr << "Error: unknown --color value '" << Diag::color
                  << "'. Expected auto, always or never." << std::endl;
        return false;
    }
    setDiagnosticRenderConfig(format == "short" ? DiagFormat::Short : DiagFormat::Rich, color);

    if (Diag::maxErrors < 0 || Diag::maxWarnings < 0) {
        std::cerr << "Error: --max-errors and --max-warning take a non-negative number."
                  << std::endl;
        return false;
    }

    // Warning names are checked against the catalog: a misspelt -Wno-<name> that silently did
    // nothing would be worse than no option.
    std::vector<std::string> known;
    for (const DiagCatalogEntry &entry : diagnosticCatalog()) {
        if (entry.severity == Severity::Warn) {
            known.push_back(normalizeDiagnosticName(entry.name));
        }
    }
    auto checkName = [&](const std::string &spec, const std::string &name) {
        const std::string key = normalizeDiagnosticName(name);
        if (std::find(known.begin(), known.end(), key) != known.end()) {
            return true;
        }
        std::cerr << "Error: -W" << spec << ": no warning is named '" << name
                  << "'. The warnings are:";
        for (const DiagCatalogEntry &entry : diagnosticCatalog()) {
            if (entry.severity == Severity::Warn) {
                std::cerr << " " << entry.name;
            }
        }
        std::cerr << std::endl;
        return false;
    };
    WarningPolicy policy;
    policy.maxShown = static_cast<size_t>(Diag::maxWarnings);
    for (const std::string &spec : Diag::warningSpecs) {
        if (spec == "error") {
            policy.allAsErrors = true;
        } else if (spec == "none") {
            policy.disableAll = true;
        } else if (spec.starts_with("error=")) {
            if (!checkName(spec, spec.substr(6)))
                return false;
            policy.asErrors.push_back(spec.substr(6));
        } else if (spec.starts_with("no-error=")) {
            if (!checkName(spec, spec.substr(9)))
                return false;
            policy.notAsErrors.push_back(spec.substr(9));
        } else if (spec.starts_with("no-")) {
            if (!checkName(spec, spec.substr(3)))
                return false;
            policy.disabled.push_back(spec.substr(3));
        } else {
            std::cerr << "Error: unknown option -W" << spec << "." << kWarningOptionsHelp;
            return false;
        }
    }
    setWarningPolicy(std::move(policy));
    return true;
}

/// --explain: prints the catalog entry named by `target` (a name, with or without the type
/// prefix, or a code such as 0x04000002). Returns false when nothing matches.
bool explainDiagnostic(const std::string &target) {
    using namespace camel::core::error;
    const std::string key = normalizeDiagnosticName(target);
    uint32_t code         = 0;
    bool isCode           = false;
    if (target.starts_with("0x") || target.starts_with("0X")) {
        const char *begin = target.data() + 2;
        const char *end   = target.data() + target.size();
        auto [ptr, ec]    = std::from_chars(begin, end, code, 16);
        isCode            = ec == std::errc() && ptr == end;
    }
    bool found = false;
    for (const DiagCatalogEntry &entry : diagnosticCatalog()) {
        if (isCode ? entry.code != code : normalizeDiagnosticName(entry.name) != key) {
            continue;
        }
        if (found) {
            cout << "\n";
        }
        found = true;
        // The templates of every type but RuntimeDiag are std::format strings, in which a
        // literal brace is written twice.
        auto literal = [&](std::string text) {
            if (entry.type == DiagType::RuntimeDiag) {
                return text;
            }
            for (const char *doubled : {"{{", "}}"}) {
                for (size_t at = text.find(doubled); at != std::string::npos;
                     at        = text.find(doubled, at + 1)) {
                    text.erase(at, 1);
                }
            }
            return text;
        };
        cout << to_string(entry.type) << "::" << entry.name << "  (0x"
             << Diagnostic::hex8(entry.code) << ", " << to_string(entry.severity) << ")\n\n"
             << "  message: " << literal(entry.message) << "\n";
        if (!entry.suggestion.empty()) {
            cout << "  help:    " << literal(entry.suggestion) << "\n";
        }
        cout << "\n  {0}, {1}, ... stand for the values a concrete report fills in. A triggering "
                "example is in docs/diagnostics.md.\n";
    }
    if (!found) {
        std::cerr << "Error: no diagnostic is named or numbered '" << target
                  << "'. docs/diagnostics.md lists them all." << std::endl;
    }
    return found;
}

/// clipp's positional parameter accepts any non-empty text, so an option it does not know
/// (`--bogus`, `-x`) is taken for the input file. Report it as an unknown option instead; a
/// file whose name really begins with '-' is written './-name'.
bool rejectUnknownOptions() {
    for (const string &arg : Run::targetFiles) {
        if (arg.size() > 1 && arg.front() == '-') {
            std::cerr << "Error: unknown option '" << arg
                      << "'. Run 'camel --help' for the options accepted." << std::endl;
            exitCode = 1;
            return false;
        }
    }
    return true;
}

} // namespace

void _printCheckArgs() {
    cout << "Check args: " << endl;
    cout << "\tlexical-only:" << lexical << endl;
    cout << "\tsyntax-only:" << syntaxOnly << endl;
    cout << "\toutput-format:" << Diag::format << endl;
    cout << "\tmax-warning:" << Diag::maxWarnings << endl;
    cout << "\tconfig-file:" << configFilePath << endl;
    cout << "\tignore:" << Check::ignoreDefiFile << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void _printInspectArgs() {
    cout << "Inspect args: " << endl;
    cout << "\tdump-tokens:" << dumpTokens << endl;
    cout << "\tdump-cst:" << dumpCST << endl;
    cout << "\tdump-ast:" << dumpAST << endl;
    cout << "\tdump-gct:" << dumpGCT << endl;
    cout << "\tdump-gir:" << dumpGIR << endl;
    cout << "\tpass-until:" << passUntil << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void printCliArgs(Command selected) {
    switch (selected) {
    case Command::Check:
        _printCheckArgs();
        break;
    case Command::Inspect:
        _printInspectArgs();
        break;
    default:
        break;
    }
}

bool parseArgs(int argc, char *argv[]) {
    const bool startupProfile = std::getenv("CAMEL_STARTUP_PROFILE") != nullptr;
    auto startupLast          = std::chrono::steady_clock::now();
    auto startupMark          = [&](const char *stage) {
        if (!startupProfile)
            return;
        const auto now = std::chrono::steady_clock::now();
        std::cerr << "ARGS " << stage << " "
                  << std::chrono::duration<double, std::micro>(now - startupLast).count() << "us\n";
        startupLast = std::chrono::steady_clock::now();
    };
    bool showVersion = false; // Version information
    bool showHelp    = false; // Help information
    bool showDocs    = false; // Documentation information
    bool showAbout   = false; // About information
    bool showZen     = false; // Show Zen of Camel

    std::string logPresetConsumed;
    std::string logIncludeConsumed;

    auto globalOps =
        (option("-vvvv") % "log threshold: trace (shortcut)",
         option("-vvv") % "log threshold: debug (shortcut)",
         option("-vv") % "log threshold: info (shortcut)",
         (option("-v", "--verbose") % "log threshold: warn (shortcut)"),
         ((option("-l", "--log-level") & value("level", Global::logLevelArg)) %
          "fatal|warn|info|debug|trace|off (default: fatal)"),
         ((option("--log-preset") & value("preset", logPresetConsumed)) %
          "scope filter preset: none|wall|extra"),
         ((option("--log-include") & value("scopes", logIncludeConsumed)) %
          "comma-separated scope prefixes (overrides preset list)"));

    // How diagnostics are reported, for every command that compiles. The -W... options are
    // taken out of argv before clipp sees it (applyWarningArguments): their value is joined to
    // the flag, which clipp cannot express.
    auto diagOps =
        ((option("--color") & value("when", Diag::color)) %
             "colours in diagnostics: auto (only on a terminal), always or never",
         (option("--max-errors") & integer("n", Diag::maxErrors)) %
             "compile errors reported before giving up (default 20; 0: no limit)",
         (option("-N", "--max-warning") & integer("n", Diag::maxWarnings)) %
             "warnings shown (default 1000); further ones are counted, not printed");

    auto run =
        (option("-P", "--profile").set(profile) %
             "record a trace of the stages into profile_reports/ (debug builds only: a release "
             "build accepts the option and writes nothing)",
         (option("-S", "--scheduler") & value("scheduler type", schedular)) % "scheduler type",
         (option("-t", "--threads") & integer("max threads", maxThreads)) % "max threads",
         (option("--max-recursion-depth") & value("calls", maxRecursionDepthOpt)) %
             "limit on calls open at once that are not tail calls (default: 1000 on every "
             "scheduler; also CAMEL_MAX_RECURSION_DEPTH)",
         (option("--max-macro-depth") & value("calls", maxMacroDepthOpt)) %
             "limit on nested calls while std::macro evaluates a call (default: 64; also "
             "CAMEL_MAX_MACRO_DEPTH)",
         (option("--max-macro-rounds") & value("rounds", maxMacroRoundsOpt)) %
             "limit on the rounds one loop may run while std::macro evaluates it (default: "
             "1000000; also CAMEL_MAX_MACRO_ROUNDS)",
         (option("--gc") & value("collector", gcModeOpt)) %
             "collector of run-time values: generational (default; young objects are copied) or "
             "mark-sweep (objects keep their address)",
         (option("--block-limit") & value("megabytes", blockLimitOpt)) %
             "limit of the data-block space in megabytes (default 2048); raise it for a run that "
             "builds several large models in one process",
         (option("--graph-gc-mb") & value("megabytes", graphGcMbOpt)) %
             "the graph space asks for a collection when it holds this many megabytes more than "
             "after the last one (default 64)",
         (option("--native-stack-guard") & value("on|off", nativeStackGuardOpt)) %
             "check the native stack on every nested call (default on); off measures where the "
             "stack really ends and is not for normal use",
         (option("--devices") & value("cpu|ordinal[,ordinal...]", devicesOpt)) %
             "devices a pipeline may use: cpu (the host only) or CUDA ordinals in order (e.g. 0 "
             "or 0,1); a device named here must be there",
         (option("--profile-mode") & value("normal|full", profileModeOpt)) %
             "mode of the profiler of --profile: full also opens the Perfetto UI when the report "
             "is written",
         option("-n", "--no-cache").set(noCache) % "do not use cache",
         (option("-I", "--include") & values("include dir", includeDirs)) % "add include directory",
         (option("-L", "--stdlib") & value("stdlib path", stdLibPath)) % "add stdlib path",
         (option("-E", "--error-format", "--output-format") & value("error format", errorFormat)) %
             "diagnostics format: text (source excerpts), short (one line each) or json",
         diagOps,
         (option("--passes") & value("pass1 key=value,pass2", passesOpt)) %
             "pass list as comma-separated, each pass followed by its parameters as key=value "
             "words (e.g. --passes std::opt safe_fp=true,std::fvm)",
         (option("--timeit") & value("pass/iterations", timeitSpec)) %
             "measure one pass N times from a fresh graph (e.g. --timeit std::jit/10)",
         (option("--input") & value("input file", inputFile)) %
             "input file (optional if positional given)",
         (option("--dump-after-each-pass") & value("dir", dumpAfterEachPassDir)) %
             "write the graph (readable DOT + stats JSON) before the first pass and after each "
             "pass that returns a graph",
         repeatable(option("--arg") & value("name=value", entryArgs)) %
             "value of a parameter of main (repeatable): a literal for a scalar, a .npy file for "
             "a tensor, a directory of .npy files for a struct of tensors",
         (option("-o", "--output") & value("output file", outputFile)) %
             "where the value main returns is written (default: stdout): a scalar as text, a "
             "tensor as .npy, a struct or tuple as a directory of those",
         globalOps,
         opt_values("input", targetFiles) %
             "input file (optional if --input given; at least one required)");

    auto info =
        (option("-V", "--version").set(showVersion).set(selectedCommand, Command::Info) %
             "show version",
         option("-h", "--help").set(showHelp).set(selectedCommand, Command::Info) %
             "show this help message",
         option("-d", "--docs").set(showDocs).set(selectedCommand, Command::Info) %
             "show documentation",
         option("-a", "--about").set(showAbout).set(selectedCommand, Command::Info) %
             "show copyright and related information",
         (option("--explain").set(selectedCommand, Command::Info) &
          value("name or code", Diag::explain)) %
             "describe a diagnostic by its name or code (e.g. --explain IndexOutOfRange)",
         option("-z", "--zen").set(showZen).set(selectedCommand, Command::Info) %
             "show the Zen of Camel");

    auto check =
        (command("check").set(selectedCommand, Command::Check) %
             "check the code without running it (exit code 1 when an error is found)",
         option("-i", "--lexical-only").set(lexical) %
             "lexical analysis only: report lexical errors and stop before parsing",
         option("-s", "--syntax-only").set(syntaxOnly) %
             "lexical and syntax analysis only: stop before names and types are checked",
         (option("-O", "--output-format") & value("output format", outputFormat)) %
             "diagnostics format: text, short or json",
         diagOps,
         (option("-c", "--config") & value("config file path", configFilePath)) %
             "config file path",
         option("-e", "--ignore").set(Check::ignoreDefiFile) % "ignore the definition file",
         option("-o", "--output") & value("output file", outputFile = "stdout") % "output file",
         globalOps,
         values("input", targetFiles) % "input file");

    auto inspect =
        (command("inspect").set(selectedCommand, Command::Inspect) % "inspect the code",
         joinable(
             option("-t", "-T").set(dumpTokens) % "dump tokens",
             option("-s", "-S").set(dumpCST) % "dump concrete syntax tree",
             option("-a", "-A").set(dumpAST) % "dump abstract syntax tree",
             option("-c", "-C").set(dumpGCT) % "dump graph construct tree",
             option("-g", "-G").set(dumpGIR) % "dump graph intermediate representation"),
         option("--tok", "--token-stream").set(dumpTokens) % "dump tokens",
         option("--cst", "--concrete-syntax-tree").set(dumpCST) % "dump concrete syntax tree",
         option("--ast", "--abstract-syntax-tree").set(dumpAST) % "dump abstract syntax tree",
         option("--gct", "--graph-construct-tree").set(dumpGCT) % "dump graph construct tree",
         option("--gir", "--graph-ir").set(dumpGIR) % "dump graph intermediate representation",
         option("--tns", "--topo-node-seq").set(dumpTNS) % "dump topological sorted node sequence",
         option("--ops", "--operators").set(dumpOps) %
             "list the operators of the built-in module and of every module the program "
             "imports: name, URI, parameter counts, declared purity, signature",
         option("--instances").set(dumpInstances) %
             "print the index semantics (iteration domain, access maps, reduction law) of every "
             "operator node whose operator declares them",
         option("--tags").set(dumpTags) %
             "print the tags the program declares, with the positions each allows and the passes "
             "it names (read_by), and the facts written on its declarations and struct fields",
         option("--facts").set(dumpFacts) %
             "compute the graph facts of every kind the core and the loaded modules register "
             "(tensor shapes, index instances, effect summaries, ...) and print each with its "
             "stamp",
         option("--attrs").set(dumpAttrs) %
             "print every node attribute the tree can write with its fate in the interpreter, "
             "the dumps, the ONNX export and Tiles, then fail if a loaded module interned an "
             "attribute key the table does not classify",
         option("--gen", "--gene-code").set(geneCode) % "generate code from AST",
         (option("-p", "-P", "--pass-until") & integer("pass until", passUntil)) %
             "pass until the given pass",
         diagOps,
         globalOps,
         values("input", targetFiles) % "input file");

    auto cli = run | info | check | inspect;
    startupMark("construct");

    std::vector<char *> args = takeWarningArguments(argc, argv);
    argc                     = static_cast<int>(args.size());
    argv                     = args.data();
    startupMark("warnings");

    const parsing_result parsed = parse(argc, argv, cli);
    if (!parsed) {
        // Name what did not parse (an unknown option, a missing value, a conflict) before the
        // usage: a command line that cannot be honoured is an error, not a run with a default.
        std::cerr << "Error: invalid command line:";
        for (const auto &argument : parsed) {
            if (argument.any_error()) {
                std::cerr << " '" << argument.arg() << "'";
            }
        }
        std::cerr << std::endl;
        cout << "Usage: " << endl;
        cout << "\tcamel [options] [<target file>]  (run: need --input or positional)\n";
        cout << usage_lines(cli, "camel") << endl;
        cout << kWarningOptionsHelp;
        exitCode = 1;
        return false;
    }
    if (!rejectUnknownOptions()) {
        return false;
    }

    startupMark("parse");
    applyCamelCliLogging(argc, argv);
    startupMark("logging");
    if (!applyDiagnosticOptions()) {
        exitCode = 1;
        return false;
    }
    startupMark("diagnostics");

    if (!Diag::explain.empty()) {
        exitCode = explainDiagnostic(Diag::explain) ? 0 : 1;
        return false;
    }

    if (showVersion) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << " (Build " << camelBuildFootprint() << ")" << endl;
#else
        cout << "Camel v" << VERSION << " (Debug " << camelBuildFootprint() << ")" << endl;
#endif
    }

    if (showHelp) {
        cout << make_man_page(cli, "camel");
        cout << kWarningOptionsHelp;
        cout << kMeasurementOptionsHelp;
    }

    if (showDocs) {
        cout << "Not implemented yet" << endl;
    }

    if (showAbout) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << " (Build " << camelBuildFootprint() << ")";
#else
        cout << "Camel v" << VERSION << " (Debug " << camelBuildFootprint() << ")";
#endif
        cout <<
#include "ABOUT"
            ;
    }

    if (showZen) {
        cout <<
#include "ZEN"
            ;
    }

    if (selectedCommand == Command::Info) {
        return false;
    }

    // Resolve input path and pass list so main does not branch on --input/--passes
    if (selectedCommand == Command::Run || selectedCommand == Command::Inspect ||
        selectedCommand == Command::Check) {
        Run::resolvedInputPath = Run::inputFile.empty()
                                     ? (Run::targetFiles.empty() ? "" : Run::targetFiles[0])
                                     : Run::inputFile;
    }
    if (selectedCommand == Command::Run) {
        if (!Run::passesOpt.empty()) {
            // Commas separate passes; inside one, `key=value` words follow the pass name.
            Run::passes = groupPassWords(splitPassList(Run::passesOpt));
        }
        if (!Run::timeitSpec.empty()) {
            if (!parseTimeitSpecValue(Run::timeitSpec, Run::timeitPass, Run::timeitIterations)) {
                // A usage error is not a successful run (H181): the value was refused above.
                exitCode = 1;
                return false;
            }
            Run::timeitEnabled = true;
        }
        if (!Run::passes.empty()) {
            Run::resolvedPassList = Run::passes;
        } else if (!Run::inputFile.empty()) {
            // `--input file a b`: every positional word is a pass or one of its parameters.
            Run::resolvedPassList = groupPassWords(Run::targetFiles);
        } else if (!Run::targetFiles.empty()) {
            // The first positional word is the program; the rest are passes and parameters.
            Run::resolvedPassList = groupPassWords(Run::targetFiles, 1);
        }
        if (Run::timeitEnabled && Run::resolvedPassList.empty()) {
            Run::resolvedPassList.push_back(Run::timeitPass);
        }

        // Apply .opencmlrc: list = prefix + CLI passes + suffix; fallback used only when graph !=
        // null after list.
        string cwd = camel::utils::currentDirectory();
        string scriptDir =
            Run::resolvedInputPath.empty()
                ? ""
                : camel::utils::pathToUtf8(
                      fs::absolute(camel::utils::pathFromUtf8(Run::resolvedInputPath))
                          .parent_path());
        Run::resolvedPassList = opencmlrc::resolvePassListWithRc(
            cwd,
            scriptDir,
            Run::resolvedPassList,
            &Run::fallbackPasses);

        // The two depth limits: the option, else the environment variable. A value that is not
        // an integer in range is refused here, before anything is compiled or run.
        if (!readDepthLimit(
                Run::maxRecursionDepthOpt,
                camel::execute::kMaxRecursionDepthOption,
                camel::execute::kMaxRecursionDepthEnvVar,
                Run::maxRecursionDepth) ||
            !readDepthLimit(
                Run::maxMacroDepthOpt,
                camel::execute::kMaxMacroDepthOption,
                camel::execute::kMaxMacroDepthEnvVar,
                Run::maxMacroDepth) ||
            !readRoundLimit(
                Run::maxMacroRoundsOpt,
                camel::execute::kMaxMacroRoundsOption,
                camel::execute::kMaxMacroRoundsEnvVar,
                Run::maxMacroRounds)) {
            std::exit(1);
        }

        // The collector: --gc, which the allocator applies before its heap exists.
        if (!Run::gcModeOpt.empty()) {
            const auto mode = camel::core::mm::parseCollectorMode(Run::gcModeOpt);
            if (!mode) {
                std::cerr << "Error: --gc " << Run::gcModeOpt
                          << " is not a collector; give mark-sweep or generational." << std::endl;
                std::exit(1);
            }
            if (!camel::core::mm::selectCollectorMode(*mode)) {
                std::cerr << "Error: --gc was read after the run-time heap was set up."
                          << std::endl;
                std::exit(1);
            }
        }

        // The memory limits: --block-limit and --graph-gc-mb. Both must be read before the
        // spaces they bound exist.
        size_t blockLimitMb   = 0;
        size_t graphGcFloorMb = 0;
        if (!readMegabyteLimit(Run::blockLimitOpt, "--block-limit", blockLimitMb) ||
            !readMegabyteLimit(Run::graphGcMbOpt, "--graph-gc-mb", graphGcFloorMb)) {
            std::exit(1);
        }
        if (blockLimitMb != 0 &&
            !camel::core::mm::selectBlockSpaceLimit(blockLimitMb * camel::core::mm::MB)) {
            std::cerr << "Error: --block-limit was read after the data-block space was set up."
                      << std::endl;
            std::exit(1);
        }
        if (graphGcFloorMb != 0) {
            camel::runtime::setGraphCollectionFloorBytes(graphGcFloorMb * camel::core::mm::MB);
        }

        // --native-stack-guard: off is the switch for measuring where the stack ends.
        if (!Run::nativeStackGuardOpt.empty()) {
            if (Run::nativeStackGuardOpt == "off") {
                camel::execute::setNativeStackGuardEnabled(false);
            } else if (Run::nativeStackGuardOpt == "on") {
                camel::execute::setNativeStackGuardEnabled(true);
            } else {
                std::cerr << "Invalid --native-stack-guard value: '" << Run::nativeStackGuardOpt
                          << "'. Expected on or off." << std::endl;
                std::exit(1);
            }
        }

        // --devices: the devices a pipeline may use (design principles 13.4). Read before any
        // module can ask for one; a device named here is a requirement, not a preference.
        if (!Run::devicesOpt.empty()) {
            std::string why;
            const auto selection = camel::execute::parseDeviceSelection(Run::devicesOpt, why);
            if (!selection) {
                std::cerr << "Error: --devices '" << Run::devicesOpt << "': " << why << "."
                          << std::endl;
                std::exit(1);
            }
            camel::execute::selectDevices(*selection);
        }

        // --profile-mode: the mode of the advanced profiler that --profile starts.
        if (!Run::profileModeOpt.empty()) {
            if (Run::profileModeOpt != "normal" && Run::profileModeOpt != "full") {
                std::cerr << "Invalid --profile-mode value: '" << Run::profileModeOpt
                          << "'. Expected normal or full." << std::endl;
                std::exit(1);
            }
            if (!Run::profile) {
                std::cerr << "Error: --profile-mode needs --profile." << std::endl;
                std::exit(1);
            }
        }

        if (Run::timeitEnabled && Run::profile) {
            std::cerr << "Error: --timeit cannot be combined with --profile." << std::endl;
            exitCode = 1;
            return false;
        }
    }

    // Run requires at least one input source: --input or positional file
    if (selectedCommand == Command::Run && Run::resolvedInputPath.empty()) {
        std::cerr
            << "Error: Run requires an input file. Use --input <file> or provide a positional file."
            << std::endl;
        exitCode = 1;
        return false;
    }

    return true;
}
