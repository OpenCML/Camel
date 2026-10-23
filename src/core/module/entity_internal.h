// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    /// A tag declared by the module (design principles 6.2): its id, later resolved through
    /// camel/core/tags.h.
    static Entity makeTag(uint32_t tag) { return Entity(tag); }

    /// The generic functions a name declares, as the GIR builder hands them to the module that
    /// imports the name: one template for a name declared once, all of them when the name is
    /// declared as several generic overloads (design principles 4.4). The payload is always the
    /// list, so every producer and every reader of it agree on one representation
    /// (DESIGN-QUESTION(generic-export)).
    using generic_template_list_t = std::vector<std::shared_ptr<void>>;
    static Entity makeGenericTemplate(std::shared_ptr<void> tmpl) {
        return makeGenericTemplates(generic_template_list_t{std::move(tmpl)});
    }
    static Entity makeGenericTemplates(const generic_template_list_t &tmpls) {
        return Entity(
            Entity::generic_payload_t{std::make_shared<generic_template_list_t>(tmpls)});
    }
    static bool isGenericTemplate(const Entity &entity) {
        return std::holds_alternative<Entity::generic_payload_t>(entity.storage_);
    }
    /// The templates the entity carries, in declaration order.
    static const generic_template_list_t &genericTemplates(const Entity &entity) {
        return *std::static_pointer_cast<generic_template_list_t>(
            std::get<Entity::generic_payload_t>(entity.storage_).tmpls);
    }
    /// The first template the entity carries: the one reader that names a single generic function
    /// of a name is a `when` condition's predicate, and a name with generic overloads answers it
    /// from its first declaration.
    static std::shared_ptr<void> genericTemplate(const Entity &entity) {
        const generic_template_list_t &tmpls = genericTemplates(entity);
        return tmpls.empty() ? nullptr : tmpls.front();
    }

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
