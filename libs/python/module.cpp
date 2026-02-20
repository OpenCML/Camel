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
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/utils/log.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <optional>

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
            throw CamelRuntimeException(
                RuntimeExceptionCode::InvalidURI,
                std::format("Invalid URI: {}", uri));
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
            "eval",
            {
                {
                    ":python/eval",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {-1, {}}},
                        "(fn: string, ...args: PyObject[]) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm,
                           const ModifierSet &) -> std::optional<Type *> {
                            if (norm.size() < 1)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            Type *pyObj = getPyObjectType();
                            for (size_t i = 1; i < norm.size(); ++i) {
                                if (!pyObj->assignable(norm[i]))
                                    return std::nullopt;
                            }
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "to_py",
            {
                {
                    ":python/to_py",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {1, {false}}},
                        "<T> (x: T) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm,
                           const ModifierSet &) -> std::optional<Type *> {
                            if (norm.size() != 1)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "from_py_int",
            {
                {
                    ":python/from_py_int",
                    StaticFuncTypeResolver::create(
                        {},
                        {{getPyObjectType(), false}},
                        Type::Int64()),
                },
            }),
        OperatorGroup::create(
            "from_py_float",
            {
                {
                    ":python/from_py_float",
                    StaticFuncTypeResolver::create(
                        {},
                        {{getPyObjectType(), false}},
                        Type::Float64()),
                },
            }),
        OperatorGroup::create(
            "from_py_bool",
            {
                {
                    ":python/from_py_bool",
                    StaticFuncTypeResolver::create(
                        {},
                        {{getPyObjectType(), false}},
                        Type::Bool()),
                },
            }),
        OperatorGroup::create(
            "from_py_string",
            {
                {
                    ":python/from_py_string",
                    StaticFuncTypeResolver::create(
                        {},
                        {{getPyObjectType(), false}},
                        Type::String()),
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

module_ptr_t PythonModule::create(context_ptr_t ctx) {
    return std::make_shared<PythonModule>(ctx);
}

bool PythonModule::load() {
    if (loaded_)
        return true;
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

Module *camel_module_create(Context *ctx) {
    return new PythonModule(ctx->shared_from_this());
}

}
