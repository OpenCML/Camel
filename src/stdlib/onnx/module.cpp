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
#include "model.h"
#include "passes/export.h"
#include "proto/onnx_writer.h"

#include "../tensor/interop.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/native.h"

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

#include <filesystem>
#include <format>
#include <fstream>

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

/// The name an error value carries when `onnx::model` itself made it.
constexpr std::string_view kModelOperator = "<onnx::model>";

/// The name an error value carries when `onnx::save` itself made it.
constexpr std::string_view kSaveOperator = "<onnx::save>";

/// The name an error value carries when `onnx::run` itself made it.
constexpr std::string_view kRunOperator = "<onnx::run>";

/// The result type of the call form: writing the model can fail, so the call gives `void!`
/// (a refusal to translate the function, or a file that cannot be written).
TupleType *fallibleVoid() {
    static TupleType *type = TupleType::fallible(Type::Void());
    return type;
}

// The stage form of the export (`onnx::export_model` as a pass): the same translation
// `onnx::model` makes, with the file written out. A program that wants the value writes
// `model(fn, example)->save<out: path>` instead; this entry stays for the pass, which names the
// stage and gets its path from the pipeline's `out` parameter.
//
// The names and the defaults of the `with` parameters are the declaration's
// (`stdlib/onnx.cml`), which is the one source of the operator's signature (design principles
// 6.4): a call names them, skips one that has a default, and the value it left out arrives here
// as that default -- an empty axis list, the target the model is checked against.
// `example` is the argument of a one-parameter `fn`, or a tuple with one example per parameter.
// A refusal to translate the function and a file that cannot be written are error values
// (`ExportUnsupported`, `IOError`): exporting is a tool, and the caller decides how to report
// what it could not do.
/// The examples a call gives the export: the argument of a one-parameter function, or a tuple with
/// one example per parameter. The export and the model entry read them the same way.
std::vector<camel::onnx::Example> examplesOf(::Function *fn, ArgsView &norm) {
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
    return examples;
}

slot_t exportKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn   = norm.get<::Function *>(0);
    auto *path = with.get<::String *>(0);
    camel::onnx::ExportOptions options;
    if (with.size() > 1) {
        options.dynamicAxes = camel::tensor::parseIntArray(with.get<::Array *>(1), with.type(1));
    }
    if (with.size() > 2) {
        options.target = with.get<::String *>(2)->toString();
    }
    std::vector<camel::onnx::Example> examples = examplesOf(fn, norm);
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

// model<dynamic_axes: int[] = [], target: string = 'onnxruntime-cpu'>(fn, example) => Model<F>!
//
// The translation itself is the export's: this entry and `export_model` call the same
// `camel::onnx::exportFunction`, against the same lowering table, and refuse with the same
// diagnostics. What differs is where the result goes: the file form writes it, this one answers
// it as a value so `save` and `run` can take it.
//
// What the value holds is the serialized bytes and nothing else (see model.h): the export reads
// the function, the examples and the constants inside a `NoCollectionScope`, and this value
// outlives that call, so it must not borrow any of them.
slot_t modelKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto *fn = norm.get<::Function *>(0);
    camel::onnx::ExportOptions options;
    if (with.size() > 0) {
        options.dynamicAxes = camel::tensor::parseIntArray(with.get<::Array *>(0), with.type(0));
    }
    if (with.size() > 1) {
        options.target = with.get<::String *>(1)->toString();
    }
    std::vector<camel::onnx::Example> examples = examplesOf(fn, norm);
    TupleType *resultType = TupleType::fallible(camel::onnx::ModelType::of(norm.type(0)));
    try {
        camel::core::mm::NoCollectionScope noCollection;
        const camel::onnx::Model model = camel::onnx::exportFunction(ctx, fn, examples, options);
        const std::vector<std::byte> bytes = camel::onnx::serialize(model);
        auto *value                        = camel::onnx::ModelValue::create(
            std::span<const std::byte>(bytes.data(), bytes.size()),
            camel::core::mm::autoSpace());
        return makeOk(camel::core::rtdata::toSlot(value), resultType);
    } catch (const camel::onnx::ExportError &e) {
        return makeError(
            "ExportUnsupported",
            e.what(),
            e.location().empty() ? kModelOperator : e.location());
    }
}

// save<out: string>(m: Model<F>) => void!
//
// Writes the bytes a model holds and nothing else: the file is the model, and it is the same file
// `export_model` wrote, because both write the bytes of one translation. `out` is a `with`
// parameter for the same reason `export_model`'s is: it decides which file, not what is written.
slot_t saveKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)ctx;
    auto *model = norm.get<camel::onnx::ModelValue *>(0);
    const std::filesystem::path path =
        camel::utils::pathFromUtf8(with.get<::String *>(0)->toString());
    try {
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error(
                "onnx: cannot open '" + camel::utils::pathToUtf8(path) + "' for writing");
        }
        const std::span<const std::byte> bytes = model->bytes();
        out.write(
            reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            throw std::runtime_error(
                "onnx: failed to write '" + camel::utils::pathToUtf8(path) + "'");
        }
    } catch (const std::runtime_error &e) {
        return makeError("IOError", e.what(), kSaveOperator);
    }
    return makeOk(NullSlot, fallibleVoid());
}

// run<model: Model<F>>(args) => ResultOf<F>!
//
// Running a model means the translated function applied to `args`; the ONNX runtime is only how
// it is computed, so the effect of the call is the effect of that function. The model is a value
// the translation already finished: its bytes are the whole of what there is to run, which is why
// the call needs no graph of its own and why `model` is a `with` parameter (it decides which
// model, not what is computed).
//
// No runtime is loaded in this build: the module does not link or open the ONNX runtime, so every
// call answers the error value the declaration promises (`ExportUnsupported`) instead of a result.
// A caller that wants a result reports it as an error; one that does not gets the unhandled-error
// diagnostic of the run, which names this operator's place in the program. The session, the
// dlopen of the C API and the DLPack handover of the tensors are the next step, and they replace
// the body of this function alone.
slot_t runKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)norm;
    (void)ctx;
    auto *model = with.get<camel::onnx::ModelValue *>(0);
    (void)model;
    return makeError(
        "ExportUnsupported",
        "onnx: the ONNX runtime is not available: this build loads no runtime, so a model value "
        "cannot be run here",
        kRunOperator);
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
                               {"run", &runKernel},
                               {"model", &modelKernel},
                               {"save", &saveKernel},
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
    // The purity of every operator (design principles 3.3). `model` translates a function value and
    // answers the result as a value, so it writes nothing outside the program;
    // supported_operators only reads the lowering table compiled into the module, which is why
    // stdlib/onnx.cml declares it pure. The file-writing form is the `export_model` *pass* below,
    // whose purity is set with its traits.
    camel::core::OperatorTable::instance().declare(
        "onnx",
        {{"model", true}, {"run", false}, {"save", false}, {"supported_operators", true}});
    // `onnx::model` translates a function value and answers it as a model value: the translation
    // reads the graph it is handed, and it writes nothing outside the program. `foldable = false`
    // because the call must run where the program writes it, though running it has no effect:
    // translating a function is where the lowering table is consulted, so an operator the table
    // does not know is refused *there*, with the position of that call (design principles 3.9).
    // A pass that evaluated it ahead of time would take that diagnostic away and hand the program
    // a value computed from a function it never ran -- and, for a function that cannot be
    // translated at all, would let the refusal become an error value that quietly leaves the
    // graph (a fold of `tag_is_error` over such a value turns the branch into its error arm, and
    // an effect in the other arm, `save` writing the file, goes with it).
    camel::core::OperatorTable::instance().set(
        "onnx:model",
        {.pure = true, .readsGraphs = true, .foldable = false});
    // `onnx::save` writes the model's bytes to a file: the effect `export_model` had, without the
    // translation that made the bytes. `readsGraphs = false` because what it reads is the bytes
    // the model value already owns, not a graph: the translation happened in `onnx::model`, and
    // this entry only writes the result down.
    camel::core::OperatorTable::instance().set("onnx:save", {.pure = false, .readsGraphs = false});
    camel::core::OperatorTable::instance().declareResources(
        "onnx:save",
        camel::core::sem::externalAmbient("fs", camel::core::sem::AccessMode::ReadModifyWrite));
    // The type `Model<F>` the module's values have (see model.h).
    camel::core::type::registerNativeType("onnx", "Model", camel::onnx::ModelType::get());
    // export_model translates the graph of the function value it receives: it is handed the
    // program's own graph, also on a back end that lowered loops in a private copy
    // (OperatorTraits::readsGraphs; op-semantics 11.4).
    camel::core::OperatorTable::instance().set(
        "onnx:export_model",
        {.pure = false, .readsGraphs = true});
    // What it touches: the model file it writes. `exportKernel` translates the function value it
    // is handed and then writes the result with `writeModelFile`, so the resource is the file
    // system, named the way `modules/fs` names it (`fs`, read-modify-written), and the effect is
    // outside the program, as the traits above say. The graph it translates is a value it is
    // handed, not a resource. Left undeclared, every operand read conservatively, so a body that
    // called it reached a resource no clause could name and could prove nothing about its own
    // effect.
    camel::core::OperatorTable::instance().declareResources(
        "onnx:export_model",
        camel::core::sem::externalAmbient(
            "fs", camel::core::sem::AccessMode::ReadModifyWrite));
    // Where an export would stop: an operator the ONNX backend has no lowering for.
    camel::runtime::NodeRoleRegistry::instance().add(
        {"export-capability", "export capability (no ONNX lowering)", "#cfe2f3"},
        [](const camel::runtime::NodeRoleSite &site,
           camel::graph::NodeRef ref) -> std::optional<std::string> {
            const camel::graph::GraphView &graph = site.view;
            // An operator that names one: the four kinds the view folds into `NodeKind::Oper` keep
            // their own payload today, so they answer `kNoOper` and the old reading (a stored OPER)
            // is this condition.
            if (graph.kindOf(ref) != camel::graph::NodeKind::Oper) {
                return std::nullopt;
            }
            const camel::core::OperId oper = graph.operatorOf(ref);
            if (oper == camel::core::kNoOper) {
                return std::nullopt;
            }
            const std::string uri(camel::core::OperatorTable::instance().uri(oper));
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
