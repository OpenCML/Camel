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
 * Created: Apr. 12, 2026
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Internal access layer for compile-side module entities. Public module APIs
 * expose Entity as an abstract carrier; only compile-side code is allowed to
 * inspect or construct the node/graph payload alternatives.
 */

#pragma once

#include "camel/core/module/module.h"

namespace camel::core::module::detail {

class EntityAccess {
  public:
    using node_handle_t   = Entity::node_payload_t;
    using graph_handle_t  = Entity::graph_payload_t;
    using graph_set_t     = Entity::graph_set_payload_t;
    using graph_set_ptr_t = Entity::graph_set_ptr_t;

    static Entity makeNode(node_handle_t node) { return Entity(node); }
    static Entity makeGraphSet(graph_set_ptr_t graphs) { return Entity(std::move(graphs)); }
    static Entity makeDecoratedGraph(graph_handle_t graph) { return Entity(std::move(graph)); }

    static bool isNode(const Entity &entity) {
        return std::holds_alternative<node_handle_t>(entity.storage_);
    }
    static bool isGraphSet(const Entity &entity) {
        return std::holds_alternative<graph_set_ptr_t>(entity.storage_);
    }
    static bool isDecoratedGraph(const Entity &entity) {
        return std::holds_alternative<graph_handle_t>(entity.storage_);
    }

    static node_handle_t node(const Entity &entity) {
        return std::get<node_handle_t>(entity.storage_);
    }
    static const graph_set_ptr_t &graphSet(const Entity &entity) {
        return std::get<graph_set_ptr_t>(entity.storage_);
    }
    static const graph_handle_t &decoratedGraph(const Entity &entity) {
        return std::get<graph_handle_t>(entity.storage_);
    }
};

} // namespace camel::core::module::detail
