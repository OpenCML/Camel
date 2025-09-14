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

#include "core/module/builtin.h"
#include "error/diagnostics/diagnostics.h"
#include "execute/executor/executor.h"

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules

    std::string toString() const {
        std::ostringstream os;
        os << "{\n";
        os << "  entryDir: " << entryDir << "\n";
        os << "  entryFile: " << entryFile << "\n";
        os << "  searchPaths: [";

        bool first = true;
        for (const auto &path : searchPaths) {
            if (path.empty())
                continue;
            if (!first) {
                os << ", ";
            }
            os << path;
            first = false;
        }

        os << "]\n";
        os << "}";
        return os.str();
    }
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    exec_mgr_uptr_t exeMgr_;

    std::optional<module_ptr_t> getBuiltinModule(const std::string &name);
    std::vector<std::string>
    getModuleNameCandidates(const std::string &currentModule, const std::string &rawImportName);
    std::string
    resolveRelativeModuleName(const std::string &currentModule, const std::string &importName);
    std::string getModulePath(const std::string &moduleName);
    bool moduleFileExists(const std::string &moduleName);
    module_ptr_t tryLoadModule(const std::string &moduleName);

    Context(const EntryConfig &entryConf, const DiagsConfig &diagConf)
        : entryConfig_(entryConf), diagConfig_(diagConf) {}

  public:
    virtual ~Context() = default;

    static context_ptr_t create(
        const EntryConfig &entryConf = EntryConfig(), const DiagsConfig &diagConf = DiagsConfig());

    const std::string &entryDir() const { return entryConfig_.entryDir; }
    DiagsConfig diagConfig() const { return diagConfig_; }
    module_ptr_t mainModule() const { return mainModule_; }
    GIR::graph_ptr_t rootGraph() const;
    GIR::graph_ptr_t mainGraph() const;

    void setMainModule(module_ptr_t module) { mainModule_ = module; }
    void registerExecutorFactory(std::string name, executor_factory_t fact) {
        exeMgr_->registerExecutorFactory(name, fact);
    }
    data_ptr_t eval(std::string uri, data_vec_t &withArgs, data_vec_t &normArgs) {
        return exeMgr_->eval(uri, withArgs, normArgs);
    }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");
};

using context_ptr_t = std::shared_ptr<Context>;
