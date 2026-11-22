// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/module/builtin.h"

class GlobalsBuiltinModule : public camel::core::module::BuiltinModule {
  public:
    GlobalsBuiltinModule(camel::core::context::context_ptr_t ctx);
    virtual ~GlobalsBuiltinModule() = default;

    virtual bool load() override;

    void exportBinaryOp(const std::string &name, const std::string &uri);
    void exportAssnOp(const std::string &name, const std::string &uri);

    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
