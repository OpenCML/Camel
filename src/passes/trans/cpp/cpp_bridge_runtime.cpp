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
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/passes/trans/cpp/cpp_bridge.h"

#include <iostream>
#include <memory>

namespace camel::execute::cppbridge {

namespace {

std::unique_ptr<RuntimeBridge> g_bridge;

void printError(const char *context, const std::exception &e) {
    std::cerr << "[libcamel] " << context << ": " << e.what() << std::endl;
}

void printError(const char *context) {
    std::cerr << "[libcamel] " << context << ": unknown error" << std::endl;
}

} // namespace

void initialize(const std::string &entryDir) {
    if (g_bridge) {
        return;
    }
    camel::utils::setupLibrarySearchPathForApp();
    try {
        auto context = createContextWithStandardConfig(entryDir);
        g_bridge     = std::make_unique<RuntimeBridge>(std::move(context));
    } catch (const std::exception &e) {
        printError("initialize", e);
    } catch (...) {
        printError("initialize");
    }
}

void importModules(const std::vector<std::string> &modules) {
    auto *b = getBridge();
    if (!b) {
        std::cerr << "[libcamel] importModules: call initialize() first" << std::endl;
        return;
    }
    try {
        for (const auto &m : modules) {
            if (!m.empty())
                b->context()->importModule(m);
        }
    } catch (const std::exception &e) {
        printError("importModules", e);
    } catch (...) {
        printError("importModules");
    }
}

void finalize() {
    try {
        g_bridge.reset();
    } catch (const std::exception &e) {
        printError("finalize", e);
    } catch (...) {
        printError("finalize");
    }
}

RuntimeBridge *getBridge() { return g_bridge.get(); }

} // namespace camel::execute::cppbridge
