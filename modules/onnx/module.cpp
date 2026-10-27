// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * onnx module wiring: the `export_model` operator group and its executor. `supported_operators`
 * has a fixed signature and is declared in stdlib/onnx.cml; the module keeps its
 * kernel in the executor below.
 */

#include "module.h"

#include "../tensor/tensor.h"
#include "../tensor/type.h"
#include "emitter.h"
#include "exporter.h"
#include "lowering.h"
#include "passes/export.h"

#include "../tensor/interop.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"

#include "camel/core/context/context.h"
#include "camel/core/operator_traits.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/node_roles.h"
#include "camel/utils/path.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
using camel::core::rtdata::makeError;
using camel::core::rtdata::makeOk;

namespace {

/// The name an error value carries when the operator itself made it: an operator does not know
/// its call site, so a report of such a value names the operator instead of a position
/// (docs/technical/29_nullable_and_fallible_values.md 8).
constexpr std::string_view kExportOperator = "<onnx::export_model>";

/// The result type of the call form: writing the model can fail, so the call gives `void!`
/// (a refusal to translate the function, or a file that cannot be written).
TupleType *fallibleVoid() {
    static TupleType *type = TupleType::fallible(Type::Void());
    return type;
}

// export_model<out: string, dynamic_axes: void[] = [], target: string = 'onnxruntime-cpu'>
//                                                                     (fn, example)
//                                                                     => void!
// `out` is the file the model is written to, and it is a `with` argument because it is a
// runtime value (a path the program reads from the environment, a name it composes); the
// axis list and the target are `with` arguments for the same reason. `fn` and `example` stay
// positional: they are what the export is about.
// The names and the defaults of the `with` parameters are the declaration's
// (`stdlib/onnx.cml`), which is the one source of the operator's signature (design principles
// 6.4): a call names them, skips one that has a default, and the value it left out arrives here
// as that default -- an empty axis list, the target the model is checked against. Neither
// default needs a case of its own: no axis left dynamic and the default target are what the
// export does with no argument at all.
// The declaration types the axis list as `void[]`: an axis list is an `int[]`, but the empty
// literal `[]` states no element type (its element is `void`), so a declaration writing `int[]`
// would refuse the empty list -- a call that names no axis, which the module accepted before the
// declaration did. The element check is the kernel's, then, and it is one: `void[]` admits any
// array, and `parseIntArray` refuses one whose elements are not integers (an error value, since
// the call is well-formed).
// `example` is the argument of a one-parameter `fn`, or a tuple with one example per parameter.
// A refusal to translate the function and a file that cannot be written are error values
// (`ExportUnsupported`, `IOError`): exporting is a tool, and the caller decides how to report
// what it could not do.
slot_t exportKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn   = norm.get<::Function *>(0);
    auto *path = with.get<::String *>(0);
    if (fn == nullptr || norm.type(0) == nullptr ||
        norm.type(0)->code() != TypeCode::Function || fn->graph() == nullptr) {
        // The declaration takes `fn` as a type variable (a declaration cannot write "a function
        // of any parameter list and result"), so this is the check the signature cannot make: a
        // value that is not a function is an error value here, not a call the compiler refuses.
        return makeError(
            "ExportUnsupported",
            "onnx.export_model expects a function as its first argument, not " +
                (norm.type(0) ? norm.type(0)->toString() : std::string("no value")),
            kExportOperator);
    }
    camel::onnx::ExportOptions options;
    if (with.size() > 1) {
        Type *axes = with.type(1);
        if (axes == nullptr || axes->code() != TypeCode::Array) {
            return makeError(
                "ExportUnsupported",
                "the 'dynamic_axes' argument of onnx.export_model is an array of axes, not " +
                    (axes ? axes->toString() : std::string("no value")),
                kExportOperator);
        }
        try {
            options.dynamicAxes = camel::tensor::parseIntArray(with.get<::Array *>(1), axes);
        } catch (const std::invalid_argument &e) {
            return makeError("ExportUnsupported", e.what(), kExportOperator);
        }
    }
    if (with.size() > 2) {
        options.target = with.get<::String *>(2)->toString();
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
        // The export simplifies the function with the passes of the pipeline, macro expansion
        // included, and the macro evaluator reaches safepoints. This kernel and the exporter
        // hold the function, the examples, the path and every constant they translate as raw
        // slots (and key tables by their addresses), so nothing may move until it is done.
        // Its allocation is bounded by the size of the exported graph.
        camel::core::mm::NoCollectionScope noCollection;
        const camel::onnx::Model model = camel::onnx::exportFunction(ctx, fn, examples, options);
        camel::onnx::writeModelFile(model, camel::utils::pathFromUtf8(path->toString()));
    } catch (const camel::onnx::ExportError &e) {
        // The refusal names the construct it is about, and that position travels in the error
        // value: a value nobody handles is reported where the export stopped (design
        // principles 3.9; entry_args.cpp#reportUnhandledError). The message stays what the
        // exporter said -- the kind says it was the export, so it is not repeated.
        return makeError(
            "ExportUnsupported",
            e.what(),
            e.location().empty() ? kExportOperator : e.location());
    } catch (const std::runtime_error &e) {
        // The file could not be written: everything else about the request was accepted.
        return makeError("IOError", e.what(), kExportOperator);
    }
    return makeOk(NullSlot, fallibleVoid());
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

class OnnxExecutor : public Executor {
  public:
    explicit OnnxExecutor(context_ptr_t ctx)
        : Executor(
              std::move(ctx), {{"export_model", &exportKernel},
                               {"supported_operators", &supportedOperatorsKernel}}) {}
};

} // namespace

OnnxModule::OnnxModule(context_ptr_t ctx) : BuiltinModule("onnx", ctx) {
    // No operator group: `export_model`'s signature is the declaration in `stdlib/onnx.cml`
    // (`with <out: string, dynamic_axes: int[]? = null, target: string? = null>` and two ordinary
    // parameters), and the module keeps the kernel, the executor and the purity. When the
    // declaration is compiled it replaces the group of this name, so a group here would be one
    // signature in two places (design principles 6.4).
    camel::onnx::passes::registerOnnxPasses();
}

module_ptr_t OnnxModule::create(context_ptr_t ctx) { return std::make_shared<OnnxModule>(ctx); }

bool OnnxModule::load() {
    if (loaded_) {
        return true;
    }
    context_->registerExecutorFactory("onnx", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<OnnxExecutor>(ctx);
    });
    // The purity of every operator (design principles 3.3). export_model writes a file;
    // supported_operators only reads the lowering table compiled into the module, which is why
    // stdlib/onnx.cml declares it pure.
    camel::core::OperatorTable::instance().declare(
        "onnx",
        {{"export_model", false}, {"supported_operators", true}});
    // export_model translates the graph of the function value it receives: it is handed the
    // program's own graph, also on a back end that lowered loops in a private copy
    // (OperatorTraits::readsGraphs; op-semantics 11.4).
    camel::core::OperatorTable::instance().set(
        "onnx:export_model",
        {.pure = false, .readsGraphs = true});
    // Where an export would stop: an operator the ONNX backend has no lowering for.
    camel::runtime::NodeRoleRegistry::instance().add(
        {"export-capability", "export capability (no ONNX lowering)", "#cfe2f3"},
        [](const camel::runtime::GCGraph &graph,
           camel::runtime::gc_node_ref_t ref) -> std::optional<std::string> {
            if (graph.node(ref)->kind != camel::runtime::GCNodeKind::Oper) {
                return std::nullopt;
            }
            const std::string uri(graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri());
            if (camel::onnx::LoweringRegistry::instance().find(uri, camel::onnx::kDefaultOpset)) {
                return std::nullopt;
            }
            return std::format("'{}' has no ONNX lowering at opset {}", uri, camel::onnx::kDefaultOpset);
        });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new OnnxModule(ctx->shared_from_this()); }
}
