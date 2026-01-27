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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "phot.h"
#include "core/context/context.h"
#include "core/mm/mm.h"
#include "core/operator.h"
#include "core/rtdata/array.h"
#include "core/rtdata/tuple.h"
#include "core/type/composite/array.h"
#include "core/type/type.h"
#include "utils/type.h"

#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;

static py::array __camel_array_to_py__(Array *arr, Context &ctx) {
    TypeCode elemCode = arr->elemType();
    size_t n          = arr->size();

    py::module_ np = py::module_::import("numpy");

    // 根据元素类型选择合适的数据类型
    if (elemCode == TypeCode::Int32 || elemCode == TypeCode::Int64) {
        // 整数类型：转换为int64数组
        std::vector<int64_t> data;
        data.reserve(n);

        if (elemCode == TypeCode::Int32) {
            for (size_t i = 0; i < n; ++i) {
                data.push_back(static_cast<int64_t>(arr->get<Int32>(i)));
            }
        } else {
            for (size_t i = 0; i < n; ++i) {
                data.push_back(static_cast<int64_t>(arr->get<Int64>(i)));
            }
        }

        // 使用numpy创建数组，确保类型正确
        py::array py_arr = py::cast(data);
        return np.attr("array")(py_arr, py::arg("dtype") = np.attr("int64"));
    } else {
        // 浮点类型：转换为float64数组
        std::vector<double> data;
        data.reserve(n);

        if (elemCode == TypeCode::Float32) {
            for (size_t i = 0; i < n; ++i) {
                data.push_back(static_cast<double>(arr->get<Float32>(i)));
            }
        } else if (elemCode == TypeCode::Float64) {
            for (size_t i = 0; i < n; ++i) {
                data.push_back(arr->get<Float64>(i));
            }
        } else {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot: array element type must be numeric");
            return py::array();
        }

        // 使用numpy创建数组，确保类型正确
        py::array py_arr = py::cast(data);
        return np.attr("array")(py_arr, py::arg("dtype") = np.attr("float64"));
    }
}

static std::pair<Array *, Array *>
__py_array_to_camel_complex__(py::array py_arr, Type *tupleTypePtr, Context &ctx) {
    try {
        py::module_ np = py::module_::import("numpy");

        if (!tupleTypePtr || tupleTypePtr->code() != TypeCode::Tuple) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot: expected tuple return type for complex arrays");
            return {nullptr, nullptr};
        }

        auto tupleType = tt::as_ptr<TupleType>(tupleTypePtr);
        if (tupleType->types().size() != 2) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot: expected tuple with 2 arrays (real, imag)");
            return {nullptr, nullptr};
        }

        auto realArrayType = tt::as_ptr<ArrayType>(tupleType->types()[0]);
        auto imagArrayType = tt::as_ptr<ArrayType>(tupleType->types()[1]);

        py::object arr_obj = py::reinterpret_borrow<py::object>(py_arr);
        bool is_complex    = np.attr("iscomplexobj")(arr_obj).cast<bool>();
        arr_obj            = arr_obj.attr("ravel")();

        if (is_complex) {
            // 复数数组：转换为complex128，然后分离实部和虚部
            arr_obj =
                np.attr("ascontiguousarray")(arr_obj, py::arg("dtype") = np.attr("complex128"));
            py::array py_real = np.attr("ascontiguousarray")(
                np.attr("real")(arr_obj),
                py::arg("dtype") = np.attr("float64"));
            py::array py_imag = np.attr("ascontiguousarray")(
                np.attr("imag")(arr_obj),
                py::arg("dtype") = np.attr("float64"));

            py::array_t<double> real_arr = py_real.cast<py::array_t<double>>();
            py::array_t<double> imag_arr = py_imag.cast<py::array_t<double>>();
            auto real_accessor           = real_arr.unchecked<1>();
            auto imag_accessor           = imag_arr.unchecked<1>();
            size_t n                     = real_arr.size();

            Array *realArr = Array::create(realArrayType->layout(), mm::autoSpace(), n);
            Array *imagArr = Array::create(imagArrayType->layout(), mm::autoSpace(), n);

            for (size_t i = 0; i < n; ++i) {
                realArr->set(i, static_cast<Float>(real_accessor(i)));
                imagArr->set(i, static_cast<Float>(imag_accessor(i)));
            }

            return {realArr, imagArr};
        } else {
            // 实数数组：转换为float64
            arr_obj = np.attr("ascontiguousarray")(arr_obj, py::arg("dtype") = np.attr("float64"));
            py::array_t<double> real_arr = arr_obj.cast<py::array_t<double>>();
            auto real_accessor           = real_arr.unchecked<1>();
            size_t n                     = real_arr.size();

            Array *realArr = Array::create(realArrayType->layout(), mm::autoSpace(), n);
            Array *imagArr = Array::create(imagArrayType->layout(), mm::autoSpace(), n);

            for (size_t i = 0; i < n; ++i) {
                realArr->set(i, static_cast<Float>(real_accessor(i)));
                imagArr->set(i, 0.0);
            }

            return {realArr, imagArr};
        }
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot: Python error: ") + e.what());
        return {nullptr, nullptr};
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot: error: ") + e.what());
        return {nullptr, nullptr};
    }
}

static Array *__py_array_to_camel_real__(py::array py_arr, Type *arrayType, Context &ctx) {
    try {
        py::module_ np = py::module_::import("numpy");

        // 如果数组是多维的，先展平为一维
        py::object arr_obj = py::reinterpret_borrow<py::object>(py_arr);
        int ndim           = arr_obj.attr("ndim").cast<int>();
        if (ndim > 1) {
            arr_obj = arr_obj.attr("flatten")();
            py_arr  = arr_obj.cast<py::array>();
        }

        auto buf = py_arr.request();
        size_t n = buf.size;

        auto resArrayType = tt::as_ptr<ArrayType>(arrayType);
        Array *result     = Array::create(resArrayType->layout(), mm::autoSpace(), n);

        // 尝试转换为int数组（gen_bits返回的是int数组）
        if (py::isinstance<py::array_t<int32_t>>(py_arr)) {
            auto arr = py_arr.cast<py::array_t<int32_t>>();
            auto ptr = arr.unchecked<1>();
            for (size_t i = 0; i < n; ++i) {
                result->set(i, static_cast<Int>(ptr(i)));
            }
        } else if (py::isinstance<py::array_t<int64_t>>(py_arr)) {
            auto arr = py_arr.cast<py::array_t<int64_t>>();
            auto ptr = arr.unchecked<1>();
            for (size_t i = 0; i < n; ++i) {
                result->set(i, static_cast<Int>(ptr(i)));
            }
        } else {
            // 尝试转换为double数组
            auto arr = py_arr.cast<py::array_t<double>>();
            auto ptr = arr.unchecked<1>();
            for (size_t i = 0; i < n; ++i) {
                result->set(i, static_cast<Int>(static_cast<int>(ptr(i))));
            }
        }

        return result;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot: error converting Python array: ") + e.what());
        return nullptr;
    }
}

// phot.config(plot: bool) => void
slot_t __phot_config__(ArgsView &with, ArgsView &norm, Context &ctx) {
    bool plot = norm.get<bool>(0);

    try {
        if (plot) {
            try {
                py::module_ matplotlib = py::module_::import("matplotlib");
                matplotlib.attr("use")("TkAgg");
                py::module_ plt = py::module_::import("matplotlib.pyplot");
                plt.attr("ion")();
            } catch (...) {
                // 忽略后端设置失败
            }
        }

        py::module_ phot = py::module_::import("phot");
        phot.attr("config")(py::arg("plot") = plot);

        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.config error: ") + e.what());
        return NullSlot;
    }
}

// phot.gen_bits(num_bits: int, bits_per_symbol: int) => (int[], int[])
slot_t __phot_gen_bits__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int num_bits        = norm.get<Int>(0);
    Int bits_per_symbol = norm.get<Int>(1);

    try {
        py::module_ phot = py::module_::import("phot");
        // phot.gen_bits 返回 [bits_x, bits_y]
        py::object result    = phot.attr("gen_bits")(num_bits, bits_per_symbol);
        py::list result_list = result.cast<py::list>();
        py::array py_bits_x  = result_list[0].cast<py::array>();
        py::array py_bits_y  = result_list[1].cast<py::array>();

        // 获取返回类型（应该是元组类型，包含两个数组）
        auto resTupleType      = norm.type(0);
        auto tupleType         = tt::as_ptr<TupleType>(resTupleType);
        auto bits_x_array_type = tupleType->types()[0];
        auto bits_y_array_type = tupleType->types()[1];

        // 转换为Camel数组
        Array *arr_x = __py_array_to_camel_real__(py_bits_x, bits_x_array_type, ctx);
        Array *arr_y = __py_array_to_camel_real__(py_bits_y, bits_y_array_type, ctx);

        if (arr_x && arr_y) {
            Tuple *tuple = Tuple::create(tupleType->layout(), mm::autoSpace());
            tuple->set(0, arr_x);
            tuple->set(1, arr_y);
            return toSlot(tuple);
        } else {
            return NullSlot;
        }
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.gen_bits error: ") + e.what());
        return NullSlot;
    }
}

slot_t __phot_modulation__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *signal_bits_tuple = norm.get<Tuple *>(0);
    Int bits_per_symbol      = with.get<Int>(0);

    try {
        Array *bits_x = signal_bits_tuple->get<Array *>(0);
        Array *bits_y = signal_bits_tuple->get<Array *>(1);

        py::module_ phot = py::module_::import("phot");
        py::list bits_list;
        bits_list.append(__camel_array_to_py__(bits_x, ctx));
        bits_list.append(__camel_array_to_py__(bits_y, ctx));

        py::list mod_result = phot.attr("modulation")(bits_list, bits_per_symbol).cast<py::list>();
        py::module_ np      = py::module_::import("numpy");

        auto resTupleType  = norm.type(0);
        auto tupleType     = tt::as_ptr<TupleType>(resTupleType);
        auto xPolTupleType = tt::as_ptr<TupleType>(tupleType->types()[0]);

        py::object signal_obj_x = mod_result[0];
        if (!np.attr("iscomplexobj")(signal_obj_x).cast<bool>()) {
            signal_obj_x = np.attr("array")(signal_obj_x, py::arg("dtype") = np.attr("complex128"));
        }
        auto [realArr_x, imagArr_x] =
            __py_array_to_camel_complex__(signal_obj_x.cast<py::array>(), xPolTupleType, ctx);

        auto yPolTupleType      = tt::as_ptr<TupleType>(tupleType->types()[1]);
        py::object signal_obj_y = mod_result[1];
        if (!np.attr("iscomplexobj")(signal_obj_y).cast<bool>()) {
            signal_obj_y = np.attr("array")(signal_obj_y, py::arg("dtype") = np.attr("complex128"));
        }
        auto [realArr_y, imagArr_y] =
            __py_array_to_camel_complex__(signal_obj_y.cast<py::array>(), yPolTupleType, ctx);

        if (!realArr_x || !imagArr_x || !realArr_y || !imagArr_y) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot.modulation: failed to convert arrays");
            return NullSlot;
        }

        Tuple *outer_tuple = Tuple::create(tupleType->layout(), mm::autoSpace());
        Tuple *xPol_tuple  = Tuple::create(xPolTupleType->layout(), mm::autoSpace());
        xPol_tuple->set(0, realArr_x);
        xPol_tuple->set(1, imagArr_x);
        Tuple *yPol_tuple = Tuple::create(yPolTupleType->layout(), mm::autoSpace());
        yPol_tuple->set(0, realArr_y);
        yPol_tuple->set(1, imagArr_y);

        outer_tuple->set(0, xPol_tuple);
        outer_tuple->set(1, yPol_tuple);

        return toSlot(outer_tuple);
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.modulation error: ") + e.what());
        return NullSlot;
    }
}

slot_t __phot_up_sample__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *signals_tuple   = norm.get<Tuple *>(0);
    Int up_sampling_factor = with.get<Int>(0);

    try {
        Tuple *x_pol_tuple = signals_tuple->get<Tuple *>(0);
        Tuple *y_pol_tuple = signals_tuple->get<Tuple *>(1);
        Array *x_real_arr  = x_pol_tuple->get<Array *>(0);
        Array *x_imag_arr  = x_pol_tuple->get<Array *>(1);
        Array *y_real_arr  = y_pol_tuple->get<Array *>(0);
        Array *y_imag_arr  = y_pol_tuple->get<Array *>(1);

        py::module_ np = py::module_::import("numpy");
        py::object j   = py::eval("1j");

        py::array py_x_real = __camel_array_to_py__(x_real_arr, ctx);
        py::array py_x_imag = __camel_array_to_py__(x_imag_arr, ctx);
        py::array py_x_complex =
            np.attr("array")(py_x_real + j * py_x_imag, py::arg("dtype") = np.attr("complex128"));
        py_x_complex = py_x_complex.attr("ravel")().cast<py::array>();

        py::array py_y_real = __camel_array_to_py__(y_real_arr, ctx);
        py::array py_y_imag = __camel_array_to_py__(y_imag_arr, ctx);
        py::array py_y_complex =
            np.attr("array")(py_y_real + j * py_y_imag, py::arg("dtype") = np.attr("complex128"));
        py_y_complex = py_y_complex.attr("ravel")().cast<py::array>();

        py::module_ phot = py::module_::import("phot");
        py::list signals_list;
        signals_list.append(py_x_complex);
        signals_list.append(py_y_complex);

        py::list py_result =
            phot.attr("up_sample")(signals_list, up_sampling_factor).cast<py::list>();

        auto resTupleType  = norm.type(0);
        auto tupleType     = tt::as_ptr<TupleType>(resTupleType);
        auto xPolTupleType = tt::as_ptr<TupleType>(tupleType->types()[0]);
        auto yPolTupleType = tt::as_ptr<TupleType>(tupleType->types()[1]);

        py::array py_result_x = py_result[0].cast<py::array>();
        py::array py_result_y = py_result[1].cast<py::array>();

        auto [realArr_x, imagArr_x] =
            __py_array_to_camel_complex__(py_result_x, xPolTupleType, ctx);
        auto [realArr_y, imagArr_y] =
            __py_array_to_camel_complex__(py_result_y, yPolTupleType, ctx);

        if (!realArr_x || !imagArr_x || !realArr_y || !imagArr_y) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot.up_sample: failed to convert arrays");
            return NullSlot;
        }

        Tuple *outer_tuple = Tuple::create(tupleType->layout(), mm::autoSpace());
        Tuple *xPol_tuple  = Tuple::create(xPolTupleType->layout(), mm::autoSpace());
        xPol_tuple->set(0, realArr_x);
        xPol_tuple->set(1, imagArr_x);
        Tuple *yPol_tuple = Tuple::create(yPolTupleType->layout(), mm::autoSpace());
        yPol_tuple->set(0, realArr_y);
        yPol_tuple->set(1, imagArr_y);

        outer_tuple->set(0, xPol_tuple);
        outer_tuple->set(1, yPol_tuple);

        return toSlot(outer_tuple);
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.up_sample error: ") + e.what());
        return NullSlot;
    }
}

slot_t __phot_pulse_shaper__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *signals_tuple   = norm.get<Tuple *>(0);
    Int up_sampling_factor = with.get<Int>(0);
    Float rolloff          = with.get<Float>(1);
    Float baud             = with.get<Float>(2);

    try {
        Tuple *x_pol_tuple = signals_tuple->get<Tuple *>(0);
        Tuple *y_pol_tuple = signals_tuple->get<Tuple *>(1);
        Array *x_real_arr  = x_pol_tuple->get<Array *>(0);
        Array *x_imag_arr  = x_pol_tuple->get<Array *>(1);
        Array *y_real_arr  = y_pol_tuple->get<Array *>(0);
        Array *y_imag_arr  = y_pol_tuple->get<Array *>(1);

        py::module_ np = py::module_::import("numpy");
        py::object j   = py::eval("1j");

        py::array py_x_real = __camel_array_to_py__(x_real_arr, ctx);
        py::array py_x_imag = __camel_array_to_py__(x_imag_arr, ctx);
        py::array py_x_complex =
            np.attr("array")(py_x_real + j * py_x_imag, py::arg("dtype") = np.attr("complex128"));
        py_x_complex = py_x_complex.attr("ravel")().cast<py::array>();

        py::array py_y_real = __camel_array_to_py__(y_real_arr, ctx);
        py::array py_y_imag = __camel_array_to_py__(y_imag_arr, ctx);
        py::array py_y_complex =
            np.attr("array")(py_y_real + j * py_y_imag, py::arg("dtype") = np.attr("complex128"));
        py_y_complex = py_y_complex.attr("ravel")().cast<py::array>();

        py::module_ phot = py::module_::import("phot");
        py::list signals_list;
        signals_list.append(py_x_complex);
        signals_list.append(py_y_complex);

        py::list py_result =
            phot.attr("pulse_shaper")(signals_list, up_sampling_factor, rolloff, baud)
                .cast<py::list>();

        py::array py_result_x = py_result[0].cast<py::array>();
        py::array py_result_y = py_result[1].cast<py::array>();

        auto resTupleType  = norm.type(0);
        auto tupleType     = tt::as_ptr<TupleType>(resTupleType);
        auto xPolTupleType = tt::as_ptr<TupleType>(tupleType->types()[0]);
        auto yPolTupleType = tt::as_ptr<TupleType>(tupleType->types()[1]);

        auto [realArr_x, imagArr_x] =
            __py_array_to_camel_complex__(py_result_x, xPolTupleType, ctx);
        auto [realArr_y, imagArr_y] =
            __py_array_to_camel_complex__(py_result_y, yPolTupleType, ctx);

        if (!realArr_x || !imagArr_x || !realArr_y || !imagArr_y) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("phot.pulse_shaper: failed to convert arrays");
            return NullSlot;
        }

        Tuple *outer_tuple = Tuple::create(tupleType->layout(), mm::autoSpace());
        Tuple *xPol_tuple  = Tuple::create(xPolTupleType->layout(), mm::autoSpace());
        xPol_tuple->set(0, realArr_x);
        xPol_tuple->set(1, imagArr_x);
        Tuple *yPol_tuple = Tuple::create(yPolTupleType->layout(), mm::autoSpace());
        yPol_tuple->set(0, realArr_y);
        yPol_tuple->set(1, imagArr_y);

        outer_tuple->set(0, xPol_tuple);
        outer_tuple->set(1, yPol_tuple);

        return toSlot(outer_tuple);
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.pulse_shaper error: ") + e.what());
        return NullSlot;
    }
}

slot_t __phot_constellation_diagram__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *signals_tuple = norm.get<Tuple *>(0);
    bool is_plot         = (with.size() > 0) ? with.get<bool>(0) : true;
    bool isdata          = (with.size() > 1) ? with.get<bool>(1) : false;

    try {
        Tuple *x_pol_tuple = signals_tuple->get<Tuple *>(0);
        Tuple *y_pol_tuple = signals_tuple->get<Tuple *>(1);
        Array *x_real_arr  = x_pol_tuple->get<Array *>(0);
        Array *x_imag_arr  = x_pol_tuple->get<Array *>(1);
        Array *y_real_arr  = y_pol_tuple->get<Array *>(0);
        Array *y_imag_arr  = y_pol_tuple->get<Array *>(1);

        py::module_ np = py::module_::import("numpy");
        py::object j   = py::eval("1j");

        py::array py_x_real = __camel_array_to_py__(x_real_arr, ctx);
        py::array py_x_imag = __camel_array_to_py__(x_imag_arr, ctx);
        py::array py_x_complex =
            np.attr("array")(py_x_real + j * py_x_imag, py::arg("dtype") = np.attr("complex128"));
        py_x_complex = py_x_complex.attr("ravel")().cast<py::array>();

        py::array py_y_real = __camel_array_to_py__(y_real_arr, ctx);
        py::array py_y_imag = __camel_array_to_py__(y_imag_arr, ctx);
        py::array py_y_complex =
            np.attr("array")(py_y_real + j * py_y_imag, py::arg("dtype") = np.attr("complex128"));
        py_y_complex = py_y_complex.attr("ravel")().cast<py::array>();

        py::module_ phot = py::module_::import("phot");
        py::list signals_list;
        signals_list.append(py_x_complex);
        signals_list.append(py_y_complex);

        phot.attr("constellation_diagram")(
            signals_list,
            py::arg("is_plot") = is_plot,
            py::arg("isdata")  = isdata);

        if (is_plot) {
            py::module_ plt = py::module_::import("matplotlib.pyplot");
            plt.attr("draw")();
            plt.attr("show")(py::arg("block") = true);
        }

        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("phot.constellation_diagram error: ") + e.what());
        return NullSlot;
    }
}
