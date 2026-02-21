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
 * Created: Feb. 20, 2026
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/utils/log.h"
#include "operators.h"

#include <optional>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

using data_arr_t = RawArray<const GraphIR::data_idx_t>;

namespace {

class PythonExecutor : public Executor {
  public:
    PythonExecutor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(ctx, std::move(ops)) {}

    void eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) override {
        EXEC_WHEN_DEBUG(l.in("PythonExec").debug("Evaluating operator of URI: {}", uri));
        auto it = opsMap_.find(uri);
        if (it == opsMap_.end()) {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
        }
        std::vector<GraphIR::data_idx_t> normIndices;
        for (const auto &in : self->normInputs())
            normIndices.push_back(in->index());
        std::vector<GraphIR::data_idx_t> withIndices;
        for (const auto &in : self->withInputs())
            withIndices.push_back(in->index());

        data_arr_t nargs = data_arr_t{
            normIndices.data(),
            static_cast<GraphIR::arr_size_t>(normIndices.size()),
        };
        data_arr_t wargs = data_arr_t{
            withIndices.data(),
            static_cast<GraphIR::arr_size_t>(withIndices.size()),
        };

        FrameArgsView withView(frame, wargs);
        FrameArgsView normView(frame, nargs);
        slot_t result = it->second(withView, normView, *context_);
        frame.set(self->index(), result);
    }
};

const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "py_call",
            {
                {
                    "python:py_call",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(fn: string | PyObject, ...args: PyObject[]) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::String &&
                                norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_exec",
            {
                {
                    "python:py_exec",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(code: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_eval",
            {
                {
                    "python:py_eval",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(expr: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_run",
            {
                {
                    "python:py_run",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(file_path: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_attr",
            {
                {
                    "python:py_attr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(obj: PyObject, attr: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 2)
                                return std::nullopt;
                            if (norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            if (norm[1]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_import",
            {
                {
                    "python:py_import",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(module_name: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "to_py",
            {
                {
                    "python:to_py",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(x: T) => PyObject<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            return getPyObjectType()->cloneWithParams(
                                std::span<Type *const>(norm.data(), 1));
                        }),
                },
            }),
        OperatorGroup::create(
            "from_py",
            {
                {
                    "python:from_py",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(obj: PyObject<T>) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            Type *t = norm[0];
                            if (!t->isOtherType() || t->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            auto *o = static_cast<OtherType *>(t);
                            if (o->paramCount() == 0)
                                return std::nullopt;
                            return o->params()[0];
                        }),
                },
            }),
    };
    return groups;
}

} // namespace

PythonModule::PythonModule(context_ptr_t ctx) : BuiltinModule("python", ctx) {
    exportType(Reference("PyObject"), getPyObjectType());
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t PythonModule::create(context_ptr_t ctx) { return std::make_shared<PythonModule>(ctx); }

bool PythonModule::load() {
    if (loaded_)
        return true;
    // 在模块加载时完成 Python 初始化，后续 python 协议算子无需再检查
    try {
        if (!Py_IsInitialized())
            py::initialize_interpreter();
    } catch (const std::exception &e) {
        context_->rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("Failed to load python module: ") + e.what());
        return false;
    }
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("python", [ctx]() {
        return std::make_shared<PythonExecutor>(ctx, getPythonOpsMap());
    });
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PythonModule(ctx->shared_from_this()); }
}
