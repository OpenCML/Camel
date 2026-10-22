// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * camel-ls: the entry point. The server is in server.h, the transport in protocol.h, and
 * everything it knows about a program comes from camel/service/semantic.
 *
 *   camel-ls [--stdio]            serve the language server protocol on the standard streams
 *   camel-ls --dump-model <file>  print the semantic model of a file and exit (for debugging)
 *   camel-ls --time <file>        print how long building the model of a file takes
 */

#include "camel/utils/windows_parser_guard.h"
#include "server.h"

#include "camel/core/mm.h"
#include "camel/init.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace mm = camel::core::mm;

static bool readFile(const std::string &path, std::string &text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "camel-ls: cannot read " << path << "\n";
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

int main(int argc, char *argv[]) {
    camel::ScopedRuntime camelRuntime;

#ifdef _WIN32
    // LSP framing requires exact CRLF bytes; Windows text mode would translate
    // them and corrupt the protocol on both directions.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--stdio") {
            continue; // the only transport
        }
        if ((arg == "--dump-model" || arg == "--time") && i + 1 < argc) {
            camel::service::semantic::AnalysisOptions options;
            options.path = argv[i + 1];
            if (!readFile(options.path, options.text)) {
                return 1;
            }
            if (arg == "--dump-model") {
                std::cout << camel::service::semantic::analyze(options)->dump();
                return 0;
            }
            // Three ways, several times (the first run also loads the modules): the whole
            // model; the same compilation without a recorder, which is what `camel check`
            // does; the syntactic index alone.
            const auto timed = [&](bool compile, bool record) {
                options.compile  = compile;
                options.record   = record;
                const auto start = std::chrono::steady_clock::now();
                auto model       = camel::service::semantic::analyze(options);
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start)
                    .count();
            };
            for (int run = 0; run < 7; ++run) {
                const double model   = timed(true, true);
                const double compile = timed(true, false);
                const double syntax  = timed(false, false);
                std::cout << "model " << model << " ms  compile-only " << compile
                          << " ms  syntax-only " << syntax << " ms\n";
            }
            return 0;
        }
        std::cerr << "camel-ls: ignoring unknown argument: " << arg << "\n";
    }

    std::error_code ec;
    std::string executableDir;
    if (argc > 0) {
        auto self = std::filesystem::weakly_canonical(std::filesystem::path(argv[0]), ec);
        if (!ec && self.has_parent_path() && std::filesystem::exists(self, ec)) {
            executableDir = self.parent_path().string();
        }
    }
    if (executableDir.empty()) {
        if (const char *home = std::getenv("CAMEL_HOME")) {
            executableDir = (std::filesystem::path(home) / "bin").string();
        }
    }

    camel::ls::Transport transport(std::cin, std::cout);
    camel::ls::Server server(transport, executableDir);
    const int code = server.run();
    // The reader thread may still be blocked in a read: leave without waiting for it.
    std::cout.flush();
    std::_Exit(code);
}
