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
 * Created: Oct. 14, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operators of the standard modules that the programs std::cpp writes carry out themselves
 * (each as the module's implementation named with it does), so that using them does not make
 * a program depend on the runtime.
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/str.h"
#include "camel/utils/env.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

namespace camel::standalone {

/// `time.now()` (time:now, modules/time/operators.cpp): seconds since the epoch.
inline double now() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

/// `env.get_env(name)` (env:get_env, modules/env/operators.cpp): the variable's value, or the
/// empty string when it is not set.
inline Str getEnv(const Str &name) {
    // The one place that reads the environment (camel/utils/env.h): through the wide interface
    // on Windows, so that a value the process code page cannot express is not lost here either.
    const std::string value = ::getEnv(name.str().c_str());
    return value.empty() ? Str() : Str(value);
}

/// `statistics.mean(xs)` (statistics:mean, modules/statistics/operators.cpp): the sum in double,
/// element by element, over the count; 0 for the empty array.
template <typename T> double statisticsMean(const Array<T> &xs) {
    if (xs.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const T &x : xs) {
        sum += static_cast<double>(x);
    }
    return sum / static_cast<double>(xs.size());
}

/// `statistics.stdev(xs)` (statistics:stdev): the sample standard deviation (over n - 1); 0 for
/// fewer than two elements.
template <typename T> double statisticsStdev(const Array<T> &xs) {
    if (xs.size() <= 1) {
        return 0.0;
    }
    const double mean = statisticsMean(xs);
    double squares    = 0.0;
    for (const T &x : xs) {
        const double d = static_cast<double>(x) - mean;
        squares += d * d;
    }
    return std::sqrt(squares / static_cast<double>(xs.size() - 1));
}

} // namespace camel::standalone
