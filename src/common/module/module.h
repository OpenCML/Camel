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
    type_ns_ptr_t importedTypeNS_, exportedTypeNS_;
    entity_ns_ptr_t importedEntityNS_, exportedEntityNS_;
    std::vector<module_ptr_t> imports_;

  public:
    Module(const std::string &name, const std::string &path);
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    void importEntities(const module_ptr_t &mod, const std::vector<Reference> &refs = {});

    bool exportType(const Reference &ref, const type_ptr_t &type) {
        return exportedTypeNS_->insert(ref, type);
    }
    bool exportEntity(const Reference &ref, const entity &ent) {
        return exportedEntityNS_->insert(ref, ent);
    }

    const type_ns_ptr_t &importedTypes() const { return importedTypeNS_; };
    const type_ns_ptr_t &exportedTypes() const { return exportedTypeNS_; };
    const entity_ns_ptr_t &importedEntities() const { return importedEntityNS_; };
    const entity_ns_ptr_t &exportedEntitys() const { return exportedEntityNS_; };

    std::optional<type_ptr_t> getImportedType(const Reference &ref) const {
        return importedTypeNS_->get(ref);
    };
    std::optional<entity> getImportedEntity(const Reference &ref) const {
        return importedEntityNS_->get(ref);
    };
    std::optional<type_ptr_t> getExportedType(const Reference &ref) const {
        return exportedTypeNS_->get(ref);
    };
    std::optional<entity> getExportedEntity(const Reference &ref) const {
        return exportedEntityNS_->get(ref);
    };
};
