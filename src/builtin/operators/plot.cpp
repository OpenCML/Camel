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
 * Author: zyx
 * Created: Dec. 19, 2025
 * Updated: Dec. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "plot.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <vector>
#include <string>

namespace py = pybind11;

// 将Array转换为std::vector<double>
template <typename T>
static std::vector<double> __array_to_vector__(Array *arr) {
    size_t n = arr->size();
    std::vector<double> vec;
    vec.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        vec.push_back(static_cast<double>(arr->get<T>(i)));
    }
    return vec;
}

static std::vector<double>
__array_to_vector_with_type__(Array *arr, TypeCode code, Context &ctx, std::string_view fname) {
    switch (code) {
    case TypeCode::Int:
        return __array_to_vector__<Int>(arr);
    case TypeCode::Long:
        return __array_to_vector__<Long>(arr);
    case TypeCode::Float:
        return __array_to_vector__<Float>(arr);
    case TypeCode::Double:
        return __array_to_vector__<Double>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return {};
    }
}

void __plot__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    // 获取数组参数
    Array *arr = frame.get<Array *>(nargs[0]);
    TypeCode elemCode = arr->elemType();

    // 检查元素类型是否支持
    if (elemCode != TypeCode::Int && elemCode != TypeCode::Long &&
        elemCode != TypeCode::Float && elemCode != TypeCode::Double) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("plot: array element type must be numeric (int, long, float, or double)");
        frame.set(self, NullSlot);
        return;
    }

    // 转换为vector
    std::vector<double> data = __array_to_vector_with_type__(arr, elemCode, ctx, "<plot>");
    if (data.empty() && arr->size() > 0) {
        // 转换失败
        frame.set(self, NullSlot);
        return;
    }

    // 获取可选的文件名参数
    std::string filename = "plot.png";
    if (nargs.size > 1) {
        String *filenameObj = frame.get<String *>(nargs[1]);
        filename = filenameObj->toString();
    }

    try {
        // 初始化Python解释器（如果还没有初始化）
        if (!Py_IsInitialized()) {
            py::initialize_interpreter();
        }

        // 导入matplotlib
        py::module_ plt = py::module_::import("matplotlib.pyplot");
        py::module_ np = py::module_::import("numpy");

        // 将数据转换为numpy数组
        py::array_t<double> py_data = py::cast(data);

        // 绘制图形
        plt.attr("figure")();
        plt.attr("plot")(py_data);
        plt.attr("title")("Data Plot");
        plt.attr("xlabel")("Index");
        plt.attr("ylabel")("Value");
        plt.attr("grid")(true);

        // 保存图片
        plt.attr("savefig")(filename);
        plt.attr("close")();

        // 设置返回值为void
        frame.set(self, NullSlot);
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("plot error: ") + e.what());
        frame.set(self, NullSlot);
    }
}

