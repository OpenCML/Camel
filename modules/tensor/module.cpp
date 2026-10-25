// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor module wiring: exports the operator groups and types derived from
 * the operator registry, serves the registry's kernels through the module
 * executor, and contributes the tensor graph passes (tensor::fuse).
 */

#include "module.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "attrs.h"
#include "camel/core/context/context.h"
#include "camel/core/module/module.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/equality.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/executor.h"
#include "camel/runtime/node_roles.h"
#include "camel/utils/path.h"
#include "dtype.h"
#include "fusion_group.h"
#include "kernels/parallel.h"
#include "npy.h"
#include "ops/binary_ops.h"
#include "ops/registry.h"
#include "passes/fuse.h"
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

/// The operators whose signatures `stdlib/tensor.cml` declares (handover H19, H151, H175). The C++
/// side keeps their kernels, executors, traits, resources and folds; the declaration is the
/// module's signature, so their groups are not exported here. Every operator, declared or
/// computed, is also exported under the type namespace (`Tensor.shape`): a declaration cannot
/// write that alias, and the compiler points it at the declaration when the declaration
/// replaces the module's entity for the name (Module::replaceTypeQualifiedAliases).
constexpr std::string_view kDeclaredOperators[] = {
    "dim",
    "gemm_backend",
    "gemm_library",
    "gemm_library_threads",
    "gemm_threads",
    "load_npy",
    "num_threads",
    "numel",
    "save_npy",
    "seed",
    "set_num_threads",
    "shape",
    "show",
    "to_bytes",
};

bool isDeclaredOperator(std::string_view name) {
    return std::find(std::begin(kDeclaredOperators), std::end(kDeclaredOperators), name) !=
           std::end(kDeclaredOperators);
}

} // namespace

TensorModule::TensorModule(context_ptr_t ctx) : BuiltinModule("tensor", ctx) {
    camel::tensor::registerTensorAttrKeys();
    camel::tensor::registerDivideMulNegFusion();
    camel::tensor::registerMulNegFusion();
    camel::tensor::ops::registerTensorOps();
    camel::tensor::passes::registerTensorPasses();
    exportType(Reference("Tensor"), camel::tensor::TensorType::Default());
    const auto binary = camel::tensor::ops::fromBytesOperatorGroup();
    exportEntity(Reference("from_bytes"), binary);
    exportEntity(Reference(std::vector<std::string>{"Tensor"}, "from_bytes"), binary);
    for (const auto &group : OpRegistry::instance().operatorGroups("tensor")) {
        if (!isDeclaredOperator(group->name())) {
            exportEntity(group->name(), group);
        }
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
    // The run's thread limit, installed once into the kernel pool before any kernel can size it:
    // `-t` sets every pool of the run, the schedulers and the modules alike, and nothing else
    // does (design principles 9.3: a setting is not an environment variable). Without `-t` this
    // is the hardware concurrency.
    camel::tensor::kernels::setNumThreads(static_cast<int>(context_->threadLimit()));
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
                const auto *t   = camel::core::rtdata::fromSlot<tensor::Array *>(value);
                const auto dims = t->shapeSpan();
                return tensor::TensorType::get(
                    t->dtype(),
                    tensor::StaticShape(dims.begin(), dims.end()));
            });
        return true;
    }();
    (void)refinerAdded;
    // A tensor is a numeric array: `==` compares two of them by shape and by element, as it
    // does for any array (design principles 4.2), which the core cannot know by itself because
    // the type is this module's. Registered once, where the type itself is declared.
    static const bool comparableAdded = [] {
        camel::core::rtdata::registerContentComparableType(camel::tensor::TensorType::typeCode());
        return true;
    }();
    (void)comparableAdded;
    // A Tensor parameter of `main` given on the command line is a .npy file (the extension
    // may be left out, as in the layout nn.save_params writes). The tensor is checked against
    // the declared element type and shape, like `as`.
    static const bool loaderAdded = [] {
        camel::execute::registerEntryArgumentLoader(
            camel::tensor::TensorType::typeCode(),
            [](const std::string &text,
               camel::core::type::Type *declared,
               camel::core::type::Type *&valueType) -> slot_t {
                namespace tensor = camel::tensor;
                std::filesystem::path path = camel::utils::pathFromUtf8(text);
                if (!std::filesystem::exists(path) && path.extension() != ".npy") {
                    path += ".npy";
                }
                if (!std::filesystem::exists(path)) {
                    throw std::runtime_error(
                        "a Tensor parameter takes a .npy file, and '" + text + "' does not exist");
                }
                tensor::Array *t = tensor::loadNpy(path, camel::core::mm::autoSpace());
                const slot_t value =
                    camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Object *>(t));
                if (const auto *want = tensor::asTensorType(declared)) {
                    (void)want->castSlotFrom(value, declared);
                }
                const auto dims = t->shapeSpan();
                valueType       = tensor::TensorType::get(
                    t->dtype(),
                    tensor::StaticShape(dims.begin(), dims.end()));
                return value;
            });
        return true;
    }();
    (void)loaderAdded;
    // The counterpart: a Tensor that `main` returns is written as a .npy file.
    static const bool saverAdded = [] {
        camel::execute::registerEntryResultSaver(
            camel::tensor::TensorType::typeCode(),
            [](slot_t value, camel::core::type::Type *, const std::string &text) -> std::string {
                std::filesystem::path path = camel::utils::pathFromUtf8(text);
                if (path.extension() != ".npy") {
                    path += ".npy";
                }
                camel::tensor::saveNpy(
                    camel::core::rtdata::fromSlot<camel::tensor::Array *>(value),
                    path);
                return camel::utils::pathToUtf8(path);
            });
        return true;
    }();
    (void)saverAdded;
    // Where a value crosses between tensors and host numbers: reading an element or a reduction
    // back to the host, or a computed host number entering tensor math.
    camel::runtime::NodeRoleRegistry::instance().add(
        {"scalar-boundary", "scalar boundary (tensor <-> host number)", "#fff2cc"},
        [](const camel::runtime::GCGraph &graph,
           camel::runtime::gc_node_ref_t ref) -> std::optional<std::string> {
            namespace rt           = camel::runtime;
            const rt::GCNode *node = graph.node(ref);
            if (node->kind != rt::GCNodeKind::Oper || !node->dataType()) {
                return std::nullopt;
            }
            const auto isTensor = [](const camel::core::type::Type *t) {
                return t && camel::tensor::asTensorType(t) != nullptr;
            };
            const auto isNumber = [](const camel::core::type::Type *t) {
                return t && camel::tensor::isSupportedTensorScalar(t->code());
            };
            const auto inputs = graph.normInputsOf(ref);
            if (isNumber(node->dataType())) {
                for (rt::gc_node_ref_t in : inputs) {
                    if (isTensor(graph.node(in)->dataType())) {
                        return "tensor -> host " + node->dataType()->toString();
                    }
                }
                return std::nullopt;
            }
            if (!isTensor(node->dataType())) {
                return std::nullopt;
            }
            for (rt::gc_node_ref_t in : inputs) {
                const rt::GCNode *input = graph.node(in);
                if (input->kind != rt::GCNodeKind::Data && isNumber(input->dataType())) {
                    return "host " + input->dataType()->toString() + " -> tensor";
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
