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
 * Created: Dec. 19, 2025
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "plot.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace py = pybind11;

// 将Array转换为std::vector<double>
template <typename T> static std::vector<double> __array_to_vector__(Array *arr) {
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
    case TypeCode::Int32:
        return __array_to_vector__<Int32>(arr);
    case TypeCode::Int64:
        return __array_to_vector__<Int64>(arr);
    case TypeCode::Float32:
        return __array_to_vector__<Float32>(arr);
    case TypeCode::Float64:
        return __array_to_vector__<Float64>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return {};
    }
}

slot_t __plot__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // 获取数组参数
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    TypeCode elemCode        = arrType->elemTypeCode();

    // 转换为vector
    std::vector<double> data = __array_to_vector_with_type__(arr, elemCode, ctx, "<plot>");
    if (data.empty() && arr->size() > 0) {
        // 转换失败
        return NullSlot;
    }

    // 获取可选的文件名参数
    std::string filename = "plot.png";
    if (norm.size() > 1) {
        String *filenameObj = norm.get<String *>(1);
        filename            = filenameObj->toString();
    }

    try {
        // 导入matplotlib
        py::module_ plt = py::module_::import("matplotlib.pyplot");
        py::module_ np  = py::module_::import("numpy");

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
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("plot error: ") + e.what());
        return NullSlot;
    }
}
