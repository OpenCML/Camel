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
 * onnx module wiring: the `export_model` operator group and its executor.
 */

#include "module.h"

#include "../tensor/tensor.h"
#include "../tensor/type.h"
#include "emitter.h"
#include "exporter.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;

namespace {

// export_model(fn: (x: Tensor) => Tensor, example: Tensor, path: string) => void
slot_t exportKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    auto *fn      = norm.get<::Function *>(0);
    auto *example = norm.get<camel::tensor::TensorObject *>(1);
    auto *path    = norm.get<::String *>(2);
    try {
        const camel::onnx::Model model = camel::onnx::exportFunction(ctx, fn, example);
        camel::onnx::writeModelFile(model, path->toString());
    } catch (const camel::onnx::ExportError &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("onnx.export_model: ") + e.what());
    } catch (const std::runtime_error &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("onnx.export_model: ") + e.what());
    }
    return NullSlot;
}

class OnnxExecutor : public Executor {
  public:
    explicit OnnxExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), {{"export_model", &exportKernel}}) {}
};

} // namespace

OnnxModule::OnnxModule(context_ptr_t ctx) : BuiltinModule("onnx", ctx) {
    exportEntity(
        "export_model",
        OperatorGroup::create(
            "export_model",
            {{"onnx:export_model",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {3, {false, false, false}}},
                  "(fn: (x: Tensor) => Tensor, example: Tensor, path: string) => void",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Function ||
                          !camel::tensor::asTensorType(norm[1]) ||
                          norm[2]->code() != TypeCode::String) {
                          return std::nullopt;
                      }
                      return Type::Void();
                  })}}));
}

module_ptr_t OnnxModule::create(context_ptr_t ctx) { return std::make_shared<OnnxModule>(ctx); }

bool OnnxModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("onnx", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<OnnxExecutor>(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new OnnxModule(ctx->shared_from_this()); }
}
