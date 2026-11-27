// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The onnx module: translates Camel functions into ONNX models.
 *
 *   onnx.model(fn, example[, dynamic_axes][, target])   the model, as a value
 *   onnx.supported_operators()
 *
 * `model` translates `fn` applied to a tensor with `example`'s dtype and shape, leaving the
 * listed input axes dynamic. The translation is a value, and its bytes are the ONNX file (see
 * model.h): writing them is `save`'s business, so the file form of `export_model` is the pair
 * `model` then `save`.
 */

#pragma once

#include "camel/core/module/builtin.h"

class OnnxModule : public camel::core::module::BuiltinModule {
  public:
    OnnxModule(camel::core::context::context_ptr_t ctx);
    ~OnnxModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
