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
 * Author: Yuxuan Zheng
 * Created: Dec. 22, 2025
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "phot.h"
#include "core/context/context.h"
#include "utils/type.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;

using namespace std;

// 确保 Python 解释器能够找到虚拟环境中的包
static void ensure_python_path() {
    try {
        if (!Py_IsInitialized()) {
            return;
        }

        py::module_ sys  = py::module_::import("sys");
        py::module_ site = py::module_::import("site");
        py::module_ os   = py::module_::import("os");

        // 尝试从环境变量获取虚拟环境路径
        std::string venv_path;
#ifdef _WIN32
        char *venv_env = nullptr;
        size_t len     = 0;
        if (_dupenv_s(&venv_env, &len, "VIRTUAL_ENV") == 0 && venv_env != nullptr) {
            venv_path = std::string(venv_env);
            free(venv_env);
        }
#else
        const char *venv_env = std::getenv("VIRTUAL_ENV");
        if (venv_env) {
            venv_path = std::string(venv_env);
        }
#endif

        // 如果找到虚拟环境，设置 sys.executable 和 sys.prefix
        if (!venv_path.empty()) {
            std::string python_exe = venv_path;
#ifdef _WIN32
            python_exe += "\\Scripts\\python.exe";
#else
            python_exe += "/bin/python";
#endif

            if (os.attr("path").attr("exists")(python_exe).cast<bool>()) {
                sys.attr("executable") = python_exe;
                sys.attr("prefix")     = venv_path;
            }
        }

        // 使用 site.main() 自动初始化虚拟环境（最重要）
        site.attr("main")();

        // 确保所有 site-packages 都在 sys.path 中
        py::list site_packages = site.attr("getsitepackages")().cast<py::list>();
        py::list path          = sys.attr("path");
        for (size_t i = 0; i < site_packages.size(); ++i) {
            std::string pkg_path = py::str(site_packages[i]).cast<std::string>();
            bool exists          = false;
            for (size_t j = 0; j < path.size(); ++j) {
                std::string path_str = py::str(path[j]).cast<std::string>();
                // 使用路径规范化比较（处理大小写和路径分隔符）
                if (pkg_path == path_str ||
                    os.attr("path").attr("normpath")(pkg_path).cast<std::string>() ==
                        os.attr("path").attr("normpath")(path_str).cast<std::string>()) {
                    exists = true;
                    break;
                }
            }
            if (!exists && os.attr("path").attr("exists")(pkg_path).cast<bool>()) {
                path.insert(0, pkg_path);
            }
        }
    } catch (...) {
        // 如果失败，继续尝试导入
    }
}

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "config",
            {
                {
                    ":phot/config",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(plot: bool) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (norm.size() < 1 || norm.size() > 1)
                                return nullopt;
                            if (norm[0]->code() != TypeCode::Bool)
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
        OperatorGroup::create(
            "gen_bits",
            {
                {
                    ":phot/gen_bits",
                    StaticFuncTypeResolver::create(
                        {},
                        {{Type::Int64(), false}, {Type::Int64(), false}},
                        TupleType::create(
                            {ArrayType::create(Type::Int64()), ArrayType::create(Type::Int64())})),
                },
            }),
        OperatorGroup::create(
            "modulation",
            {
                {
                    ":phot/modulation",
                    StaticFuncTypeResolver::create(
                        {{Type::Int64(), false}},
                        {{TupleType::create(
                              {ArrayType::create(Type::Int64()), ArrayType::create(Type::Int64())}),
                          false}},
                        // 返回 ((double[], double[]), (double[], double[]))，即 X_Pol 和 Y_Pol
                        // 的实部和虚部
                        TupleType::create({
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}), // X_Pol (real, imag)
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}) // Y_Pol (real, imag)
                        })),
                },
            }),
        OperatorGroup::create(
            "up_sample",
            {
                {
                    ":phot/up_sample",
                    StaticFuncTypeResolver::create(
                        {{Type::Int64(), false}},
                        // 输入：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol
                        // 的实部和虚部
                        {{TupleType::create({
                              TupleType::create(
                                  {ArrayType::create(Type::Float64()),
                                   ArrayType::create(Type::Float64())}), // X_Pol
                              TupleType::create(
                                  {ArrayType::create(Type::Float64()),
                                   ArrayType::create(Type::Float64())}) // Y_Pol
                          }),
                          false}},
                        // 返回：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol
                        // 的实部和虚部
                        TupleType::create({
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}), // X_Pol
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}) // Y_Pol
                        })),
                },
            }),
        OperatorGroup::create(
            "pulse_shaper",
            {
                {
                    ":phot/pulse_shaper",
                    StaticFuncTypeResolver::create(
                        {{Type::Int64(), false},
                         {Type::Float64(), false},
                         {Type::Float64(), false}},
                        // 输入：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol
                        // 的实部和虚部
                        {{TupleType::create({
                              TupleType::create(
                                  {ArrayType::create(Type::Float64()),
                                   ArrayType::create(Type::Float64())}), // X_Pol
                              TupleType::create(
                                  {ArrayType::create(Type::Float64()),
                                   ArrayType::create(Type::Float64())}) // Y_Pol
                          }),
                          false}},
                        // 返回：((double[], double[]), (double[], double[])) 即 X_Pol 和 Y_Pol
                        // 的实部和虚部
                        TupleType::create({
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}), // X_Pol
                            TupleType::create(
                                {ArrayType::create(Type::Float64()),
                                 ArrayType::create(Type::Float64())}) // Y_Pol
                        })),
                },
            }),
        OperatorGroup::create(
            "constellation_diagram",
            {
                {
                    ":phot/constellation_diagram",
                    DynamicFuncTypeResolver::create(
                        {{-1, {}}, {1, {false}}},
                        "<is_plot?: bool, isdata?: bool> (signals: ((double[], double[]), "
                        "(double[], double[]))) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> optional<Type *> {
                            if (with.size() < 1 || with.size() > 3)
                                return nullopt;
                            // 第一个参数必须是元组，包含两个元组（X_Pol 和
                            // Y_Pol），每个元组包含两个double数组（real 和 imag）
                            if (norm[0]->code() != TypeCode::Tuple)
                                return nullopt;
                            auto outerTupleType = tt::as_ptr<TupleType>(norm[0]);
                            if (outerTupleType->types().size() != 2)
                                return nullopt;
                            // 检查 X_Pol 和 Y_Pol 是否都是元组
                            if (outerTupleType->types()[0]->code() != TypeCode::Tuple ||
                                outerTupleType->types()[1]->code() != TypeCode::Tuple)
                                return nullopt;
                            auto xPolTupleType = tt::as_ptr<TupleType>(outerTupleType->types()[0]);
                            auto yPolTupleType = tt::as_ptr<TupleType>(outerTupleType->types()[1]);
                            // 检查每个元组是否包含两个数组（real 和 imag）
                            if (xPolTupleType->types().size() != 2 ||
                                yPolTupleType->types().size() != 2)
                                return nullopt;
                            if (xPolTupleType->types()[0]->code() != TypeCode::Array ||
                                xPolTupleType->types()[1]->code() != TypeCode::Array ||
                                yPolTupleType->types()[0]->code() != TypeCode::Array ||
                                yPolTupleType->types()[1]->code() != TypeCode::Array)
                                return nullopt;
                            // 可选参数必须是bool
                            if (with.size() >= 1 && with[0]->code() != TypeCode::Bool)
                                return nullopt;
                            if (with.size() >= 2 && with[1]->code() != TypeCode::Bool)
                                return nullopt;
                            return Type::Void();
                        }),
                },
            }),
    };

    return groups;
}

PhotBuiltinModule::PhotBuiltinModule(context_ptr_t ctx) : BuiltinModule("phot", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

module_ptr_t PhotBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<PhotBuiltinModule>(ctx);
}

bool PhotBuiltinModule::load() {
    if (loaded_) {
        return true;
    }

    try {
        // 初始化 Python 解释器
        if (!Py_IsInitialized()) {
            try {
                py::initialize_interpreter();
            } catch (const std::exception &e) {
                context_->rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit(std::string("Failed to initialize Python interpreter: ") + e.what());
                return false;
            }
        }
        // 确保 Python 解释器能够找到虚拟环境中的包
        ensure_python_path();
    } catch (const std::exception &e) {
        context_->rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("Failed to load phot module: ") + e.what());
        return false;
    }

    loaded_ = true;
    return true;
}
