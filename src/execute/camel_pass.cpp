// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel_pass.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/module/module.h"
#include "camel/core/module/userdef.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/typevar.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/definitions.h"
#include "camel/utils/assert.h"
#include "pass_declarations.h"
#include "passes/sched/nodevm/nodevm.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <vector>

using camel::core::context::context_ptr_t;
using camel::core::rtdata::Bool;
using camel::core::rtdata::Float32;
using camel::core::rtdata::Float64;
using camel::core::rtdata::Int32;
using camel::core::rtdata::Int64;
using camel::core::rtdata::toSlot;
using camel::core::type::TypeCode;
using camel::runtime::Program;

namespace camel::execute {

namespace {

/// One `with` parameter of a pass written in Camel: the name the pipeline writes and the type
/// its value is converted to.
struct CamelParam {
    std::string name;
    PassParamType passType = PassParamType::Bool;
    TypeCode code          = TypeCode::Bool;
};

/// The pass parameters of `type`, in declaration order, or why it has none this pipeline can
/// supply. A pass whose declaration is its own `with` list takes a bool, an int, a float or a
/// string: those are the values the pipeline can put into a `with` argument, and a `with`
/// parameter has no default in source, so the pipeline has to write each of them (design
/// principles 4.4, 9.3). A parameter of another type — `int[]`, or a nullable `T?`, which a
/// default of `null` makes omittable — belongs to a declaration in the module's `.cml`, where the
/// declaration holds its type and its default together.
std::optional<std::string>
passParamsOf(const camel::core::type::FunctionType &type, std::vector<CamelParam> &params) {
    if (!type.hasArgNames()) {
        return "its `with` parameters have no names, so the pipeline cannot write them";
    }
    for (size_t i = 0; i < type.withTypesCount(); ++i) {
        CamelParam param;
        param.name = std::string(type.argNameAt(i));
        if (param.name.empty()) {
            return "one of its `with` parameters has no name";
        }
        switch (type.withTypeAt(i)->code()) {
        case TypeCode::Bool:
            param.passType = PassParamType::Bool;
            break;
        case TypeCode::Int32:
        case TypeCode::Int64:
            param.passType = PassParamType::Int;
            break;
        case TypeCode::Float32:
        case TypeCode::Float64:
            param.passType = PassParamType::Float;
            break;
        case TypeCode::String:
            param.passType = PassParamType::String;
            break;
        default:
            return std::format(
                "its `with` parameter '{}' has type {}, and a pass written in Camel takes a bool, "
                "an int, a float or a string; a parameter of another type, with the default that "
                "makes it optional, is declared in the module's `.cml`",
                param.name,
                type.withTypeAt(i)->toString());
        }
        param.code = type.withTypeAt(i)->code();
        params.push_back(std::move(param));
    }
    return std::nullopt;
}

/// Whether `type` has the signature of a pass: one ordinary parameter of type Program, any
/// number of `with` parameters of a pass parameter type, result `Program!`. `params` receives
/// the `with` parameters; `problem` says what is wrong when it has not.
bool hasPassSignature(
    const camel::core::type::FunctionType &type, std::vector<CamelParam> &params,
    std::string &problem) {
    if (type.normTypesCount() != 1 || type.normTypeAt(0)->toString() != "Program") {
        problem = "a pass is a function (p: Program) => Program! (its `with` parameters are its parameters)";
        return false;
    }
    const auto *result = camel::core::type::asFallible(type.exitType());
    if (result == nullptr || result->payloadType()->toString() != "Program") {
        problem = "a pass returns Program! (the program it produced)";
        return false;
    }
    if (auto why = passParamsOf(type, params)) {
        problem = *why;
        return false;
    }
    return true;
}

/// Whether `type` has the signature of a pass over a function: one ordinary parameter of a type
/// variable `F`, any number of `with` parameters of a pass parameter type, result `F!`. A pass is a
/// function transform `(f: F) => F!` (design principles 9.1); `Program`, the snapshot, is the
/// implementation's own and is not on the interface.
bool hasGenericPassSignature(
    const camel::core::type::FunctionType &type, std::vector<CamelParam> &params,
    std::string &problem) {
    if (type.normTypesCount() != 1 || type.normTypeAt(0)->code() != TypeCode::TypeVar) {
        problem = "a pass is a function (f: F) => F! of a function f";
        return false;
    }
    const auto *result = camel::core::type::asFallible(type.exitType());
    if (result == nullptr || !result->payloadType()->equals(type.normTypeAt(0))) {
        problem = "a pass returns F! (the function it produced, of the type it was given)";
        return false;
    }
    if (auto why = passParamsOf(type, params)) {
        problem = *why;
        return false;
    }
    return true;
}

bool hasPassSignature(
    const camel::runtime::GCGraph &graph, std::vector<CamelParam> &params, std::string &problem) {
    const auto *type = graph.funcType();
    if (type == nullptr) {
        problem = "a pass is a function (p: Program) => Program! (its `with` parameters are its parameters)";
        return false;
    }
    return hasPassSignature(*type, params, problem);
}

/// The passes a module provides, by name: the pass declarations of its `.cml` file, which is what
/// `<module>::<name>` can name . "Which passes it provides" is
/// read from the declaration and from nothing else: an exported function is a pass because the
/// module declared it one, never because its signature happens to look like one, and never because
/// C++ registered a kernel under that name. The declaration's applicability condition is what
/// `findDescriptor` checks before it runs, so this follows `findPassDeclaration` for the same
/// reason the pipeline does.
///
/// Only a declaration module is read this way. `declarationModule()` is what says that the `.cml` is
/// the single statement of a native module's surface, and `Context` sets it exactly where a `.cml`
/// is found beside the `.cmo` it declares (6.4, step 1). A module written in Camel is read the
/// older way, from the functions it exports: it declares its passes itself, and there the exported
/// `(p: Program) => Program!` function is its pass — the shape is the whole declaration.
std::vector<std::string> passNamesOf(
    const camel::core::module::UserDefinedModule &module, const std::string &name,
    const context_ptr_t &ctx) {
    if (!module.declarationModule()) {
        std::vector<std::string> names;
        for (const auto &function : module.exportedFunctions()) {
            if (function.type == nullptr) {
                continue;
            }
            std::vector<CamelParam> params;
            std::string problem;
            if (hasPassSignature(*function.type, params, problem) ||
                (params.clear(), hasGenericPassSignature(*function.type, params, problem))) {
                names.push_back(function.name);
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }
    std::vector<std::string> names;
    for (const auto &declaration : camel::execute::detail::passDeclarations(name, ctx)) {
        names.push_back(declaration.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// The default value a `PassParam` carries when the source has none: the pipeline fills it but
/// the parameter is required, so `bindArgs` refuses a specification that leaves it out.
PassParamValue zeroOf(PassParamType type) {
    switch (type) {
    case PassParamType::Bool:
        return false;
    case PassParamType::Int:
        return int64_t{0};
    case PassParamType::Float:
        return 0.0;
    case PassParamType::String:
        return std::string();
    case PassParamType::IntArray:
        // A pass written in Camel declares no `int[]` parameter: `passParamsOf` refuses one, so
        // no parameter of this pass ever arrives here. The case is here so the switch covers every
        // type a parameter may have.
        return std::vector<int64_t>();
    }
    return false;
}

class CamelPass final : public GraphIRPass {
  public:
    CamelPass(
        const context_ptr_t &ctx, std::string module, std::string function,
        std::vector<CamelParam> params, const PassArgs &args, bool overFunction)
        : GraphIRPass(ctx),
          module_(std::move(module)),
          function_(std::move(function)),
          params_(std::move(params)),
          args_(args),
          overFunction_(overFunction) {}

    const Program *apply(const Program *program, std::ostream &os) override {
        (void)os;
        std::string problem;
        // A pass over a function is generic in the function (`(f: F) => F!`): the program is
        // offered to it as its root function, and the instance for that function's type is the
        // one that runs. The snapshot stays on this side of the interface.
        camel::core::type::FunctionType *expected = nullptr;
        if (overFunction_) {
            const camel::runtime::GCGraph *root = program->root();
            ASSERT(root != nullptr && root->funcType() != nullptr, "A program has a root function.");
            camel::core::type::Type *subject = root->funcType();
            camel::core::type::FunctionTypeFactory factory;
            for (const CamelParam &param : params_) {
                factory.addWithArg(param.name, camel::core::type::Type::create(param.code), false);
            }
            factory.addNormArg("f", subject, false);
            factory.setExitType(camel::core::type::TupleType::fallible(subject));
            expected = factory.build();
        }
        const auto tool = context_->definitions().libraryFunction(
            module_, function_, problem, nullptr, nullptr, expected);
        if (!tool.has_value()) {
            // The instance for this program's function cannot be built (its `when` refuses the
            // type, or its body is in error): the pass is not applicable, and the reason is the
            // compile's own.
            failWithErrorValue(camel::core::rtdata::makeError(
                "PassNotApplicable",
                problem.empty() ? std::string("the pass cannot be built for this program") : problem,
                "<" + module_ + "::" + function_ + ">"));
            return nullptr;
        }
        // The values of the `with` parameters are bound to the ports of the tool graph's root
        // frame before it runs; a string is made in the auto space and rooted by that frame, so
        // no collection can take it away while the pass runs.
        std::vector<slot_t> withArgs;
        withArgs.reserve(params_.size());
        for (const CamelParam &param : params_) {
            switch (param.code) {
            case TypeCode::Bool:
                withArgs.push_back(toSlot<Bool>(args_.getBool(param.name)));
                break;
            case TypeCode::Int32:
                withArgs.push_back(toSlot<Int32>(static_cast<Int32>(args_.getInt(param.name))));
                break;
            case TypeCode::Int64:
                withArgs.push_back(toSlot<Int64>(args_.getInt(param.name)));
                break;
            case TypeCode::Float32:
                withArgs.push_back(
                    toSlot<Float32>(static_cast<Float32>(args_.getFloat(param.name))));
                break;
            case TypeCode::Float64:
                withArgs.push_back(toSlot<Float64>(args_.getFloat(param.name)));
                break;
            case TypeCode::String:
                withArgs.push_back(
                    toSlot(::String::from(args_.getString(param.name), camel::core::mm::autoSpace())));
                break;
            default:
                ASSERT(false, "A pass parameter was checked when the pass was registered.");
            }
        }
        NodeVMSchedPass vm(context_);
        slot_t argument = camel::core::rtdata::toSlot(const_cast<Program *>(program));
        if (overFunction_) {
            // The root function of the program: it captures nothing, since the root graph holds
            // the values of the top level itself.
            camel::core::mm::NoCollectionScope noCollection;
            static const camel::core::type::TupleType *noClosure =
                camel::core::type::TupleType::create();
            argument = camel::core::rtdata::toSlot(::Function::create(
                program, program->root()->id(), noClosure, camel::core::mm::autoSpace()));
        }
        // What the pass body captured is not reached through a function value here: a library
        // function is looked up as a program, and its entry graph computes the values of its module
        // and binds them before it calls the function, so the captures are filled before the body
        // runs. There is no closure left for this caller to pass, and the entry graph, not a
        // function value, is what `callGraph` is for.
        const slot_t result   = vm.callGraph(
            tool->program,
            tool->graph,
            std::span(&argument, 1),
            std::span<const slot_t>(withArgs));
        if (camel::core::rtdata::isErrorValue(result)) {
            // The pass is `(p: Program) => Program!` (design principles 9.1): it failed, and what
            // it returned is an error value like any other. It is handed to the driver, which
            // stops the pipeline there and reports it the way an error value nobody handled is
            // (3.9) -- the same report the value an entry graph returns gets. The module and the
            // function are in the error the pass made, not in a diagnostic of our own.
            failWithErrorValue(result);
            return nullptr;
        }
        const Program *produced = nullptr;
        if (overFunction_) {
            // The function the pass handed back is the root of a program of its own (a commit
            // makes one); that program is what the pipeline goes on with.
            const auto *function = camel::core::rtdata::fromSlot<::Function *>(
                camel::core::rtdata::falliblePayload(result));
            produced = function->program();
            if (produced->root() == nullptr || produced->root()->id() != function->graphId()) {
                failWithErrorValue(camel::core::rtdata::makeError(
                    "GraphInvalid",
                    "the function a pass returns must be the root function it was given, "
                    "transformed",
                    "<" + module_ + "::" + function_ + ">"));
                return nullptr;
            }
        } else {
            produced = camel::core::rtdata::fromSlot<const Program *>(
                camel::core::rtdata::falliblePayload(result));
        }
        // Only the installed program is replaced. A pass over a private program (onnx.export's
        // working copy, a specialization's detached snapshot) leaves the context's program as it
        // is; the commit already keeps the produced program alive as a detached one.
        if (produced != program) {
            context_->installRuntimeProgramIfCurrent(program, produced);
        }
        return produced;
    }

  private:
    std::string module_;
    std::string function_;
    std::vector<CamelParam> params_;
    PassArgs args_;
    bool overFunction_ = false;
};

} // namespace

void registerCamelPass(
    const context_ptr_t &ctx, const std::string &module, const std::string &function,
    std::string &problem, std::optional<camel::core::error::DiagLabel> *errorLocation,
    MissingCamelPass *missing) {
    camel::runtime::DefinitionStore::LibraryLookup lookup;
    const auto tool = ctx->definitions().libraryFunction(
        module, function, problem, errorLocation, &lookup);
    if (lookup.absent) {
        // The module is there and compiles, and the name is not one of its passes: the reason to
        // give is what it does provide, not a "not found" over the whole pipeline. A name
        // whose module is not there (or is not written in Camel) stays unknown, with `problem`
        // cleared: the pipeline lists the passes it does know.
        problem.clear();
        if (missing != nullptr && lookup.moduleFound && lookup.module) {
            missing->moduleHasNoSuchPass = true;
            missing->declaresUnexported  = lookup.declaresUnexported;
            missing->provided            = passNamesOf(*lookup.module, module, ctx);
        }
        return;
    }
    if (!tool) {
        // A generic pass over a function is not built until the function it transforms is
        // known: it is registered from its declaration.
        if (lookup.module) {
            for (const auto &function_ : lookup.module->exportedFunctions()) {
                std::vector<CamelParam> genericParams;
                std::string genericProblem;
                if (function_.name != function || function_.type == nullptr ||
                    !hasGenericPassSignature(*function_.type, genericParams, genericProblem)) {
                    continue;
                }
                problem.clear();
                PassDescriptor generic;
                for (const CamelParam &param : genericParams) {
                    generic.params.push_back(PassParam{
                        param.name,
                        param.passType,
                        zeroOf(param.passType),
                        "the `with` parameter '" + param.name + "' of " + module + "::" + function,
                        true});
                }
                generic.make = [module, function, genericParams](
                                   const context_ptr_t &ctx, const PassArgs &args) {
                    return std::make_unique<CamelPass>(
                        ctx, module, function, genericParams, args, true);
                };
                registerModulePass(module + "::" + function, std::move(generic));
                return;
            }
        }
        return;
    }
    std::vector<CamelParam> params;
    if (!hasPassSignature(*tool->graph, params, problem)) {
        problem = std::format("'{}::{}' is not a pass: {}.", module, function, problem);
        return;
    }
    PassDescriptor descriptor;
    // The declaration in the module's `.cml` file is the pass's documentation and its
    // parameters; the signature is read here only as the fallback when it has none.
    for (const CamelParam &param : params) {
        descriptor.params.push_back(PassParam{
            param.name,
            param.passType,
            zeroOf(param.passType),
            "the `with` parameter '" + param.name + "' of " + module + "::" + function,
            true});
    }
    descriptor.make = [module, function, params](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<CamelPass>(ctx, module, function, params, args, false);
    };
    // A Camel pass says which tags it reads with `#reads(...)` on its function: that is what
    // makes a tag's facts read, exactly as a C++ module's `read_by` does (design principles
    // 6.2). The module is the one the pass was compiled from (already in the context).
    try {
        if (const auto owner = ctx->importModule(module)) {
            const auto facts =
                camel::core::tag::ofDeclaration(*owner, Reference(function));
            const auto value = camel::core::attr::valueOf(facts, camel::core::attr::key("reads"));
            if (value != camel::core::attr::kNoValue) {
                const camel::core::attr::Value decoded = camel::core::attr::value(value);
                const auto declare = [&](const std::string &writen) {
                    // The reader is recorded under the tag's qualified name, which is what a use
                    // of it in a program names (the program writes the short name).
                    std::string key = writen;
                    if (const auto id = camel::core::tag::find(module, writen)) {
                        key = camel::core::tag::get(*id)->name;
                    } else if (const auto core = camel::core::tag::find(writen)) {
                        key = camel::core::tag::get(*core)->name;
                    }
                    camel::core::tag::declareTagReader(key, module + "::" + function);
                };
                if (decoded.kind == camel::core::attr::ValueKind::Symbol) {
                    declare(decoded.text);
                } else if (decoded.kind == camel::core::attr::ValueKind::Tuple) {
                    for (const auto item : decoded.items) {
                        const camel::core::attr::Value tag = camel::core::attr::value(item);
                        if (tag.kind == camel::core::attr::ValueKind::Symbol) {
                            declare(tag.text);
                        }
                    }
                }
            }
        }
    } catch (const std::exception &) {
        // The declaration cannot be read here; the pass still runs and the tag simply has no
        // reader recorded for it (the pipeline warns when that leaves it unread).
    }
    registerModulePass(module + "::" + function, std::move(descriptor));
}

} // namespace camel::execute