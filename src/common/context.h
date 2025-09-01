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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <fstream>
#include <memory>
#include <unordered_map>

#include "common/error/diagnostic.h"
#include "module/builtin.h"

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules
    std::unordered_map<std::string, std::string> envs; // environment variables
};

std::ostream &operator<<(std::ostream &os, const EntryConfig &config);

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    DiagnosticsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;

    std::vector<std::string>
    getModuleNameCandidates(const std::string &currentModule, const std::string &rawImportName);
    std::string
    resolveRelativeModuleName(const std::string &currentModule, const std::string &importName);
    std::string getModulePath(const std::string &moduleName);
    bool moduleFileExists(const std::string &moduleName);
    module_ptr_t tryLoadModule(const std::string &moduleName);

  public:
    Context(
        const EntryConfig &entryConf = EntryConfig(),
        const DiagnosticsConfig &diagConf = DiagnosticsConfig());
    virtual ~Context() = default;

    const std::string &root() const { return entryConfig_.entryDir; }
    DiagnosticsConfig diagConfig() const { return diagConfig_; }
    module_ptr_t mainModule() const { return mainModule_; }

    void setMainModule(module_ptr_t module) { mainModule_ = module; }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");
};

using context_ptr_t = std::shared_ptr<Context>;
