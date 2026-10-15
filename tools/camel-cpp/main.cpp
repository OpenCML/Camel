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
 * Created: Mar. 11, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * camel-cpp: a Camel program to an executable, in one step.
 *
 *   camel-cpp <file.cml> [-o <executable>] [passes before std::cpp ...]
 *
 * It runs `camel <file.cml> [passes] std::cpp` (the `camel` next to this tool) to get the C++
 * of the program, writes it next to the executable as <name>.cpp, and builds it with the C++
 * compiler ($CXX, else c++ / clang++) and the headers of the installation
 * ($CAMEL_HOME/include). A standalone program needs nothing else; a program that uses operators
 * of runtime modules (the C++ includes camel/standalone/bridge.h) is linked with libcamel
 * (docs/cpp_export_build.md, docs/design/cpp-standalone/README.cn.md section 10).
 */

#include "camel/utils/install_layout.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string quoted(const fs::path &p) { return "\"" + p.string() + "\""; }

int runCommand(const std::string &command) {
    std::cerr << "[camel-cpp] " << command << std::endl;
#ifdef _WIN32
    // cmd.exe drops the outer quotes of a command that starts with a quote.
    return std::system(("\"" + command + "\"").c_str());
#else
    const int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

std::string compiler() {
    if (const char *cxx = std::getenv("CXX"); cxx != nullptr && *cxx != '\0') {
        return cxx;
    }
#ifdef _WIN32
    return "clang++";
#else
    return "c++";
#endif
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: camel-cpp <file.cml> [-o <executable>] [passes before std::cpp ...]\n";
        return 2;
    }
    const fs::path source = argv[1];
    fs::path executable   = source.stem();
#ifdef _WIN32
    executable += ".exe";
#endif
    std::vector<std::string> passes;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            executable = argv[++i];
        } else {
            passes.push_back(arg);
        }
    }

    const fs::path home = camel::utils::resolveInstallRoot();
    fs::path tool       = camel::utils::detail::executableDirectoryRaw() / "camel";
#ifdef _WIN32
    tool += ".exe";
#endif
    if (!fs::exists(tool)) {
        tool = home / "bin" / tool.filename();
    }
    fs::path cpp = executable;
    cpp.replace_extension(".cpp");

    std::string translate = quoted(tool) + " " + quoted(source);
    for (const std::string &p : passes) {
        translate += " " + p;
    }
    translate += " std::cpp > " + quoted(cpp);
    if (runCommand(translate) != 0) {
        std::cerr << "[camel-cpp] std::cpp did not translate " << source << std::endl;
        return 1;
    }

    std::ifstream in(cpp);
    std::stringstream text;
    text << in.rdbuf();
    const bool bridged = text.str().find("camel/standalone/bridge.h") != std::string::npos;

    std::string build = compiler() + " -std=c++20 -O2 -ffp-contract=off -I" +
                        quoted(home / "include") + " " + quoted(cpp) + " -o " + quoted(executable);
    if (bridged) {
        // The program uses operators of runtime modules: it links the runtime, and finds it
        // (and the modules) through the installation when it runs.
        const fs::path lib = home / "libs";
#ifdef _WIN32
        build += " " + quoted(lib / "libcamel.lib");
#else
        build += " -L" + quoted(lib) + " -lcamel";
        build += " -Wl,-rpath," + quoted(lib);
#endif
    }
    if (runCommand(build) != 0) {
        std::cerr << "[camel-cpp] the C++ compiler failed on " << cpp << std::endl;
        return 1;
    }
    std::cerr << "[camel-cpp] " << executable.string()
              << (bridged ? " (links the Camel runtime)" : " (standalone)") << std::endl;
    return 0;
}
