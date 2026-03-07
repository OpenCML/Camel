/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
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
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string(fname) + " not supported for type " + typeCodeToString(code));
    }
}

slot_t __plot__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    TypeCode elemCode        = arrType->elemTypeCode();

    std::vector<double> data = __array_to_vector_with_type__(arr, elemCode, ctx, "<plot>");
    if (data.empty() && arr->size() > 0) {
        return NullSlot;
    }

    std::string filename = "plot.png";
    if (norm.size() > 1) {
        String *filenameObj = norm.get<String *>(1);
        filename            = filenameObj->toString();
    }

    try {
        py::module_ plt = py::module_::import("matplotlib.pyplot");
        py::module_ np  = py::module_::import("numpy");

        py::array_t<double> py_data = py::cast(data);

        plt.attr("figure")();
        plt.attr("plot")(py_data);
        plt.attr("title")("Data Plot");
        plt.attr("xlabel")("Index");
        plt.attr("ylabel")("Value");
        plt.attr("grid")(true);

        plt.attr("savefig")(filename);
        plt.attr("close")();

        return NullSlot;
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("plot error: ") + e.what());
    }
}

std::unordered_map<std::string, operator_t> getPyplotOpsMap() { return {{"plot", __plot__}}; }
