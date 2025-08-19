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

#include "module/builtin.h"
#include "module/main.h"

inline bool fileExists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

class Context : public std::enable_shared_from_this<Context> {
    std::string root_;                     // root path of the context, used for loading modules
    std::vector<std::string> searchPaths_; // search paths for modules

    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;

    void setMainModule(const module_ptr_t &module) { mainModule_ = module; }

  public:
    Context() { modules_[""] = getBuiltinModule("").value(); };
    virtual ~Context() = default;

    const std::string &root() const { return root_; }
    void setRoot(const std::string &root) {
        root_ = root;
        if (root_.back() != '/') {
            root_ += '/';
        }
    }
    void addSearchPath(const std::string &path) { searchPaths_.push_back(path); }

    module_ptr_t tryLoadModule(const std::string &path) {
        for (const auto &dir : searchPaths_) {
            std::string fullPath = root_ + dir + "/" + path + ".cml";
            if (fileExists(fullPath)) {
                return UserDefinedModule::loadFromFile(fullPath, shared_from_this());
            }
        }

        std::string fullPath = root_  + path + ".cml";
        if (fileExists(fullPath)) {
            return UserDefinedModule::loadFromFile(fullPath, shared_from_this());
        }

        return nullptr;
    }

    module_ptr_t importModule(const std::string &path) {
        auto it = modules_.find(path);
        if (it != modules_.end()) {
            return it->second;
        }

        module_ptr_t module = tryLoadModule(path);
        if (module) {
            modules_[path] = module;
            return module;
        }

        auto moduleOpt = getBuiltinModule(path);
        if (moduleOpt.has_value()) {
            modules_[path] = moduleOpt.value();
            return moduleOpt.value();
        }

        throw std::runtime_error("Module not found: " + path);
    }

    static context_ptr_t create() {
        auto ctx = std::make_shared<Context>();
        ctx->setRoot(".");
        ctx->addSearchPath("");
        ctx->addSearchPath("lib");
        ctx->setMainModule(MainModule::create(ctx));
        return ctx;
    }
};

using context_ptr_t = std::shared_ptr<Context>;
