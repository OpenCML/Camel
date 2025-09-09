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

using entity = std::variant<GIR::node_ptr_t, GIR::graph_vec_ptr_t, oper_idx_vec_ptr_t>;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t = std::shared_ptr<Namespace<std::string, type_ptr_t>>;

class Module;
using module_ptr_t = std::shared_ptr<Module>;

class Module : public std::enable_shared_from_this<Module> {
  protected:
    bool loaded_;
    std::string name_;
    std::string path_;
    type_ns_ptr_t exportedTypeNS_;
    entity_ns_ptr_t exportedEntityNS_;
    std::unordered_map<Reference, module_ptr_t> importedRefModMap_;

  public:
    Module(const std::string &name, const std::string &path);
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    virtual bool load() = 0;
    bool loaded() const { return loaded_; }

    void markImportedRefFromMod(const Reference &ref, const module_ptr_t &mod);
    void importAllRefsFromMod(const module_ptr_t &mod);
    bool hasImportedRef(const Reference &ref) const;

    bool exportType(const Reference &ref, const type_ptr_t &type);
    bool exportEntity(const Reference &ref, const entity &ent);

    type_ptr_t getImportedType(const Reference &ref) const;
    entity getImportedEntity(const Reference &ref) const;

    std::optional<type_ptr_t> getExportedType(const Reference &ref) const;
    std::optional<entity> getExportedEntity(const Reference &ref) const;

    type_ns_ptr_t exportedTypeNS() const;
    entity_ns_ptr_t exportedEntityNS() const;
};
