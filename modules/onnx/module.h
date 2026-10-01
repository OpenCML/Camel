/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The onnx module: exports Camel functions as ONNX models.
 *
 *   onnx.export_model(fn, example, path[, dynamic_axes])
 *   onnx.supported_operators()
 *
 * writes the model of `fn` applied to a tensor with `example`'s dtype and
 * shape to `path`, leaving the listed input axes dynamic, and lists the
 * operator URIs the backend can lower (see exporter.h).
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
