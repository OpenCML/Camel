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
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "common/graph.h"
#include "common/ns.h"
#include "common/operator.h"
#include "common/type.h"

using entity = std::variant<GIR::node_ptr_t, GIR::graph_vec_ptr_t, operator_vec_ptr_t>;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t = std::shared_ptr<Namespace<std::string, type_ptr_t>>;

class Module;
using module_ptr_t = std::shared_ptr<Module>;

class Module : public std::enable_shared_from_this<Module> {
  protected:
    std::string name_;
    std::string path_;
    type_ns_ptr_t importedTypeSpace_, exportedTypeSpace_;
    entity_ns_ptr_t importedEntitySpace_, exportedEntitySpace_;
    std::vector<module_ptr_t> imports_;

  public:
    Module(const std::string &name, const std::string &path)
        : name_(name), path_(path),
          importedTypeSpace_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
          exportedTypeSpace_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
          importedEntitySpace_(std::make_shared<Namespace<std::string, entity>>()),
          exportedEntitySpace_(std::make_shared<Namespace<std::string, entity>>()) {};
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    void importModule(const module_ptr_t &mod, const std::vector<Reference> &refs) {
        imports_.push_back(mod);
        for (const auto &ref : refs) {
            if (auto type = mod->getExportedType(ref); type.has_value()) {
                importedTypeSpace_->insert(ref, type.value());
            } else if (auto ent = mod->getExportedEntity(ref); ent.has_value()) {
                importedEntitySpace_->insert(ref, ent.value());
            }
        }
    }
    bool exportType(const Reference &ref, const type_ptr_t &type) {
        return exportedTypeSpace_->insert(ref, type);
    }
    bool exportEntity(const Reference &ref, const entity &ent) {
        return exportedEntitySpace_->insert(ref, ent);
    }

    std::optional<type_ptr_t> getImportedType(const Reference &ref) const {
        return importedTypeSpace_->get(ref);
    };
    std::optional<entity> getImportedEntity(const Reference &ref) const {
        return importedEntitySpace_->get(ref);
    };
    std::optional<type_ptr_t> getExportedType(const Reference &ref) const {
        return exportedTypeSpace_->get(ref);
    };
    std::optional<entity> getExportedEntity(const Reference &ref) const {
        return exportedEntitySpace_->get(ref);
    };
};
