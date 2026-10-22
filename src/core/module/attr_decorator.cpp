// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/module/attr_decorator.h"

#include "camel/core/type/composite/func.h"

namespace camel::core::module {

oper_group_ptr_t attrDecoratorGroup(
    const std::string &name, const std::string &uri, int withCount, std::string signature,
    std::function<bool(const type::type_vec_t &with)> acceptsWith) {
    std::vector<bool> withVars(withCount < 0 ? 0 : static_cast<size_t>(withCount), false);
    return OperatorGroup::create(
        name,
        {{uri,
          type::DynamicFuncTypeResolver::create(
              {{withCount, std::move(withVars)}, {1, {false}}},
              std::move(signature),
              [withCount, acceptsWith = std::move(acceptsWith)](
                  const type::type_vec_t &with,
                  const type::type_vec_t &norm,
                  const ModifierSet &) -> std::optional<type::Type *> {
                  if (norm.size() != 1 || norm[0]->code() != type::TypeCode::Function) {
                      return std::nullopt;
                  }
                  if (withCount >= 0 && with.size() != static_cast<size_t>(withCount)) {
                      return std::nullopt;
                  }
                  if (acceptsWith && !acceptsWith(with)) {
                      return std::nullopt;
                  }
                  return norm[0];
              })}});
}

OperatorTraits attrDecoratorTraits() { return {.pure = true, .mayFault = false}; }

slot_t attrDecoratorIdentity(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    (void)with;
    (void)ctx;
    return norm.slot(0);
}

} // namespace camel::core::module
