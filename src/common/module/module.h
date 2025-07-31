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

using entity = std::variant<GIR::node_ptr_t, GIR::graph_ptr_t, operator_ptr_t>;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t = std::shared_ptr<Namespace<std::string, type_ptr_t>>;

class Module;
using module_ptr_t = std::shared_ptr<Module>;

class Module : public std::enable_shared_from_this<Module> {
  protected:
    std::string name_;
    std::string path_;
    type_ns_ptr_t typeSpace_;
    entity_ns_ptr_t entitySpace_;

    std::vector<module_ptr_t> imports_;

    bool exportEntity(const std::string &name, const entity &ent) { return entitySpace_->insert(name, ent); }

  public:
    Module(const std::string &name, const std::string &path)
        : name_(name), path_(path), typeSpace_(std::make_shared<Namespace<std::string, type_ptr_t>>()),
          entitySpace_(std::make_shared<Namespace<std::string, entity>>()) {};
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    std::optional<entity> getEntity(const Reference &ref) const { return entitySpace_->get(ref); };
};
