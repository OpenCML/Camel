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
 * Created: Jul. 29, 2025
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tensor module wiring: exports the operator groups and types derived from
 * the operator registry, serves the registry's kernels through the module
 * executor, and contributes the tensor graph passes (tensor::fuse).
 */

#include "module.h"

#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_traits.h"
#include "camel/runtime/node_roles.h"
#include "camel/execute/executor.h"
#include "ops/registry.h"
#include "passes/fuse.h"
#include "dtype.h"
#include "tensor.h"
#include "type.h"

using namespace camel::core::context;
using namespace camel::core::module;
using camel::tensor::ops::OpRegistry;

namespace {

class TensorExecutor : public Executor {
  public:
    TensorExecutor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(std::move(ctx), std::move(ops)) {}
};

} // namespace

TensorModule::TensorModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    camel::tensor::ops::registerTensorOps();
    camel::tensor::passes::registerTensorPasses();
    exportType(Reference("Tensor"), camel::tensor::TensorType::Default());
    for (const auto &group : OpRegistry::instance().operatorGroups("tensor")) {
        exportEntity(group->name(), group);
        if (group->name().starts_with("__")) {
            exportDefaultImportRef(group->name());
        } else {
            exportEntity(Reference(std::vector<std::string>{"Tensor"}, group->name()), group);
        }
    }
}

module_ptr_t TensorModule::create(context_ptr_t ctx) { return std::make_shared<TensorModule>(ctx); }

bool TensorModule::load() {
    if (loaded_)
        return true;
    context_->registerExecutorFactory("tensor", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<TensorExecutor>(ctx, OpRegistry::instance().kernelMap("tensor"));
    });
    // A tensor constant's exact type carries its dtype and shape.
    static const bool refinerAdded = [] {
        camel::core::ValueTypeRefinerRegistry::instance().add(
            [](slot_t value, camel::core::type::Type *type) -> camel::core::type::Type * {
                namespace tensor = camel::tensor;
                if (!tensor::asTensorType(type) || value == NullSlot) {
                    return nullptr;
                }
                const auto *t   = camel::core::rtdata::fromSlot<tensor::TensorObject *>(value);
                const auto dims = t->shapeSpan();
                return tensor::TensorType::get(
                    t->dtype(),
                    tensor::StaticShape(dims.begin(), dims.end()));
            });
        return true;
    }();
    (void)refinerAdded;
    // Where a value crosses between tensors and host numbers: reading an element or a reduction
    // back to the host, or a computed host number entering tensor math.
    camel::runtime::NodeRoleRegistry::instance().add(
        {"scalar-boundary", "scalar boundary (tensor <-> host number)", "#fff2cc"},
        [](const camel::runtime::GCGraph &graph,
           camel::runtime::gc_node_ref_t ref) -> std::optional<std::string> {
            namespace rt = camel::runtime;
            const rt::GCNode *node = graph.node(ref);
            if (node->kind != rt::GCNodeKind::Oper || !node->dataType) {
                return std::nullopt;
            }
            const auto isTensor = [](const camel::core::type::Type *t) {
                return t && camel::tensor::asTensorType(t) != nullptr;
            };
            const auto isNumber = [](const camel::core::type::Type *t) {
                return t && camel::tensor::isSupportedTensorScalar(t->code());
            };
            const auto inputs = graph.normInputsOf(ref);
            if (isNumber(node->dataType)) {
                for (rt::gc_node_ref_t in : inputs) {
                    if (isTensor(graph.node(in)->dataType)) {
                        return "tensor -> host " + node->dataType->toString();
                    }
                }
                return std::nullopt;
            }
            if (!isTensor(node->dataType)) {
                return std::nullopt;
            }
            for (rt::gc_node_ref_t in : inputs) {
                const rt::GCNode *input = graph.node(in);
                if (input->kind != rt::GCNodeKind::Data && isNumber(input->dataType)) {
                    return "host " + input->dataType->toString() + " -> tensor";
                }
            }
            return std::nullopt;
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TensorModule(ctx->shared_from_this()); }
}
