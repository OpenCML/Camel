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
#include "lowering.h"

#include "../tensor/interop.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;

namespace {

// export_model(fn, example, path[, dynamic_axes: int[]]) => void
// `example` is the argument of a one-parameter `fn`, or a tuple with one example per parameter.
slot_t exportKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    auto *fn   = norm.get<::Function *>(0);
    auto *path = norm.get<::String *>(2);
    camel::onnx::ExportOptions options;
    if (norm.size() > 3) {
        options.dynamicAxes = camel::tensor::parseIntArray(norm.get<::Array *>(3), norm.type(3));
    }
    std::vector<camel::onnx::Example> examples;
    const size_t params = fn && fn->graph() ? fn->graph()->normPorts().size() : 0;
    if (params == 1 || norm.type(1)->code() != TypeCode::Tuple) {
        examples.push_back({norm.slot(1), norm.type(1)});
    } else {
        auto *tupleType = static_cast<TupleType *>(norm.type(1));
        auto *tuple     = norm.get<::Tuple *>(1);
        for (size_t i = 0; i < tupleType->size(); ++i) {
            examples.push_back({tuple->get<slot_t>(i), tupleType->typeAt(i)});
        }
    }
    try {
        const camel::onnx::Model model = camel::onnx::exportFunction(ctx, fn, examples, options);
        camel::onnx::writeModelFile(model, path->toString());
    } catch (const camel::onnx::ExportError &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("onnx.export_model: ") + e.what());
    } catch (const std::runtime_error &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("onnx.export_model: ") + e.what());
    }
    return NullSlot;
}

slot_t supportedOperatorsKernel(ArgsView &, ArgsView &, Context &) {
    const auto uris =
        camel::onnx::LoweringRegistry::instance().supported(camel::onnx::kDefaultOpset);
    ::Array *result = ::Array::create(camel::core::mm::autoSpace(), uris.size());
    for (size_t i = 0; i < uris.size(); ++i) {
        result->set(i, ::String::from(uris[i], camel::core::mm::autoSpace()));
    }
    return camel::core::rtdata::toSlot(result);
}

bool isIntArray(Type *type) {
    if (!type || type->code() != TypeCode::Array) {
        return false;
    }
    Type *elem = static_cast<ArrayType *>(type)->elemType();
    return elem && (elem->code() == TypeCode::Int32 || elem->code() == TypeCode::Int64);
}

class OnnxExecutor : public Executor {
  public:
    explicit OnnxExecutor(context_ptr_t ctx)
        : Executor(
              std::move(ctx), {{"export_model", &exportKernel},
                               {"supported_operators", &supportedOperatorsKernel}}) {}
};

} // namespace

OnnxModule::OnnxModule(context_ptr_t ctx) : BuiltinModule("onnx", ctx) {
    exportEntity(
        "export_model",
        OperatorGroup::create(
            "export_model",
            {{"onnx:export_model",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {-1, {}}},
                  "(fn: (...) => any, example: Tensor | tuple | struct, path: string, "
                  "dynamic_axes?: int[]) => void",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm.size() < 3 || norm.size() > 4 ||
                          norm[0]->code() != TypeCode::Function ||
                          !(camel::tensor::asTensorType(norm[1]) ||
                            norm[1]->code() == TypeCode::Tuple ||
                            norm[1]->code() == TypeCode::Struct) ||
                          norm[2]->code() != TypeCode::String) {
                          return std::nullopt;
                      }
                      if (norm.size() == 4 && !isIntArray(norm[3])) {
                          return std::nullopt;
                      }
                      return Type::Void();
                  })}}));
    // supported_operators(): the operator URIs the ONNX backend can lower (capability report).
    exportEntity(
        "supported_operators",
        OperatorGroup::create(
            "supported_operators",
            {{"onnx:supported_operators",
              StaticFuncTypeResolver::create({}, {}, ArrayType::create(Type::String()))}}));
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
