/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel_pass.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/module/module.h"
#include "camel/core/module/userdef.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/definitions.h"
#include "camel/utils/assert.h"
#include "passes/sched/nodevm/nodevm.h"

#include <algorithm>
#include <format>
#include <vector>

using camel::core::context::context_ptr_t;
using camel::core::error::RuntimeDiag;
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
/// supply. A pass parameter is a bool, an int, a float or a string: the values the pipeline
/// parses. The function's `with` parameters are its pass parameters (design principles 4.4,
/// 9.3); they have no default in source, so the pipeline has to write each of them.
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
                "its `with` parameter '{}' has type {}, and a pass parameter is a bool, an int, "
                "a float or a string",
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

bool hasPassSignature(
    const camel::runtime::GCGraph &graph, std::vector<CamelParam> &params, std::string &problem) {
    const auto *type = graph.funcType();
    if (type == nullptr) {
        problem = "a pass is a function (p: Program) => Program! (its `with` parameters are its parameters)";
        return false;
    }
    return hasPassSignature(*type, params, problem);
}

/// The passes a module provides, by name: the functions it exports with the signature of a pass,
/// which is what `<module>::<name>` can name (H130).
std::vector<std::string> passNamesOf(const camel::core::module::UserDefinedModule &module) {
    std::vector<std::string> names;
    for (const auto &function : module.exportedFunctions()) {
        if (function.type == nullptr) {
            continue;
        }
        std::vector<CamelParam> params;
        std::string problem;
        if (hasPassSignature(*function.type, params, problem)) {
            names.push_back(function.name);
        }
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
    }
    return false;
}

class CamelPass final : public GraphIRPass {
  public:
    CamelPass(
        const context_ptr_t &ctx, std::string module, std::string function,
        std::vector<CamelParam> params, const PassArgs &args)
        : GraphIRPass(ctx),
          module_(std::move(module)),
          function_(std::move(function)),
          params_(std::move(params)),
          args_(args) {}

    const Program *apply(const Program *program, std::ostream &os) override {
        (void)os;
        std::string problem;
        const auto tool = context_->definitions().libraryFunction(module_, function_, problem);
        ASSERT(tool.has_value(), "A registered Camel pass is available.");
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
        const slot_t argument = camel::core::rtdata::toSlot(const_cast<Program *>(program));
        const slot_t result   = vm.callGraph(
            tool->program,
            tool->graph,
            std::span(&argument, 1),
            std::span<const slot_t>(withArgs));
        if (camel::core::rtdata::isErrorValue(result)) {
            std::ostringstream text;
            camel::core::rtdata::printTaggedSlots(
                text,
                camel::core::rtdata::fromSlot<::Tuple *>(result)->data(),
                camel::core::type::asFallible(tool->graph->funcType()->exitType()));
            context_->rtmDiags()->of(RuntimeDiag::GraphPassFailed).commit(module_ + "::" + function_, text.str());
            return nullptr;
        }
        const auto *produced = camel::core::rtdata::fromSlot<const Program *>(
            camel::core::rtdata::falliblePayload(result));
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
        // give is what it does provide, not a "not found" over the whole pipeline (H130). A name
        // whose module is not there (or is not written in Camel) stays unknown, with `problem`
        // cleared: the pipeline lists the passes it does know.
        problem.clear();
        if (missing != nullptr && lookup.moduleFound && lookup.module) {
            missing->moduleHasNoSuchPass = true;
            missing->declaresUnexported  = lookup.declaresUnexported;
            missing->provided            = passNamesOf(*lookup.module);
        }
        return;
    }
    if (!tool) {
        return;
    }
    std::vector<CamelParam> params;
    if (!hasPassSignature(*tool->graph, params, problem)) {
        problem = std::format("'{}::{}' is not a pass: {}.", module, function, problem);
        return;
    }
    PassDescriptor descriptor;
    descriptor.doc = "A pass written in Camel: " + module + "::" + function + ".";
    for (const CamelParam &param : params) {
        descriptor.params.push_back(PassParam{
            param.name,
            param.passType,
            zeroOf(param.passType),
            "the `with` parameter '" + param.name + "' of " + module + "::" + function,
            true});
    }
    descriptor.make = [module, function, params](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<CamelPass>(ctx, module, function, params, args);
    };
    // A Camel pass says which tags it reads with `#reads<...>` on its function: that is what
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