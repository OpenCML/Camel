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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "camel/common/ns.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

namespace camel::compile::gir {
class Node;
class Graph;
using graph_ptr_t     = std::shared_ptr<Graph>;
using graph_vec_t     = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
} // namespace camel::compile::gir

namespace GIR = camel::compile::gir;
namespace camel::core::module {

using Type            = camel::core::type::Type;
using entity          = std::variant<GIR::Node *, GIR::graph_vec_ptr_t, oper_group_ptr_t>;
using entity_ns_ptr_t = std::shared_ptr<Namespace<std::string, entity>>;
using type_ns_ptr_t   = std::shared_ptr<Namespace<std::string, Type *>>;

using context_ptr_t = camel::core::context::context_ptr_t;

class Module : public std::enable_shared_from_this<Module> {
  protected:
    bool loaded_;
    std::string name_;
    std::string path_;
    context_ptr_t context_;
    type_ns_ptr_t exportedTypeNS_;
    entity_ns_ptr_t exportedEntityNS_;
    /// 同一 ref 可能来自多个模块（同名函数/算子重载），按 ref 聚合模块列表
    std::unordered_map<Reference, std::vector<std::shared_ptr<Module>>> importedRefModMap_;
    /// 合并后的导入 entity 缓存，避免每次查询都做多模块合并
    mutable std::unordered_map<Reference, entity> importedEntityCache_;

  public:
    Module(const std::string &name, const std::string &path, context_ptr_t ctx);
    virtual ~Module() = default;

    const std::string &name() const { return name_; }
    const std::string &path() const { return path_; }

    virtual bool load() = 0;
    virtual bool loaded() const { return loaded_; }

    void markImportedRefFromMod(const Reference &ref, const std::shared_ptr<Module> &mod);
    void importAllRefsFromMod(const std::shared_ptr<Module> &mod);
    bool hasImportedRef(const Reference &ref) const;

    bool exportType(const Reference &ref, Type *type);
    bool exportEntity(const Reference &ref, const entity &ent);

    /// Returns true if this module imports the given module
    bool imports(const std::shared_ptr<Module> &mod) const;

    std::optional<Type *> getImportedType(const Reference &ref) const;
    std::optional<entity> getImportedEntity(const Reference &ref) const;

    std::optional<Type *> getExportedType(const Reference &ref) const;
    std::optional<entity> getExportedEntity(const Reference &ref) const;

    type_ns_ptr_t exportedTypeNS() const;
    entity_ns_ptr_t exportedEntityNS() const;
};

using module_ptr_t = std::shared_ptr<Module>;

} // namespace camel::core::module
