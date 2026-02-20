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
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/execute/executor.h"

#include <fstream>
#include <memory>
#include <unordered_map>

namespace GraphIR {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

class Module;
using module_ptr_t = std::shared_ptr<Module>;

struct EntryConfig {
    std::string entryDir;                 // root path of the context, used for loading modules
    std::string entryFile;                // entry module path
    std::vector<std::string> searchPaths; // search paths for modules

    std::string toString() const;
};

class Context : public std::enable_shared_from_this<Context> {
    EntryConfig entryConfig_;
    DiagsConfig diagConfig_;

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;
    std::unordered_map<std::string, module_ptr_t> builtinModules_;

    exec_mgr_uptr_t exeMgr_;
    diagnostics_ptr_t rtmDiags_;
    std::unordered_map<std::string, void *> loadedDllHandles_; // .cmo 路径 -> 句柄，保证 DLL 不卸载

    std::optional<module_ptr_t> getBuiltinModule(const std::string &name);
    std::vector<std::string>
    getModuleNameCandidates(const std::string &currentModule, const std::string &rawImportName);
    std::string
    resolveRelativeModuleName(const std::string &currentModule, const std::string &importName);
    /** 查找模块对应文件：任意后缀；.cmo 视为动态库，其余视为源码。first 为空表示未找到。 */
    std::pair<std::string, bool> getModulePathAndKind(const std::string &moduleName);
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
    diagnostics_ptr_t rtmDiags() const { return rtmDiags_; }
    GraphIR::graph_ptr_t rootGraph() const;
    GraphIR::graph_ptr_t mainGraph() const;
    const ExecutorManager &execMgr() const { return *exeMgr_; }

    void setMainModule(module_ptr_t module) { mainModule_ = module; }
    void registerExecutorFactory(std::string name, executor_factory_t fact);
    void eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame);

    /** 由 loadCmoModule 调用，保存已加载 .cmo 的句柄以防被卸载。 */
    void addLoadedDll(const std::string &path, void *handle) { loadedDllHandles_[path] = handle; }

    module_ptr_t
    importModule(const std::string &rawModuleName, const std::string &currentModuleName = "");
};

using context_ptr_t = std::shared_ptr<Context>;
