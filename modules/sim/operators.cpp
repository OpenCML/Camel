/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"

#include <cmath>
#include <random>

using namespace camel::core::error;
using namespace camel::core::context;

std::unordered_map<std::string, operator_t> getSimOpsMap() {
    return {
        {"pi_hits", __sim_pi_hits__},
        {"asian_call_price", __sim_asian_call_price__},
    };
}

slot_t __sim_pi_hits__(ArgsView &with, ArgsView &norm, Context &ctx) {
    int64_t samples = norm.get<int64_t>(0);
    int64_t seed    = norm.get<int64_t>(1);
    if (samples < 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<pi_hits> requires samples >= 0");
    }

    std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(seed));
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    int64_t hits = 0;
    for (int64_t i = 0; i < samples; ++i) {
        double x = dist(rng);
        double y = dist(rng);
        if (x * x + y * y <= 1.0) {
            ++hits;
        }
    }
    return camel::core::rtdata::toSlot(hits);
}

slot_t __sim_asian_call_price__(ArgsView &with, ArgsView &norm, Context &ctx) {
    int64_t paths = norm.get<int64_t>(0);
    int64_t steps = norm.get<int64_t>(1);
    int64_t seed  = norm.get<int64_t>(2);
    if (paths < 0 || steps <= 0) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<asian_call_price> requires paths >= 0 and steps > 0");
    }

    constexpr double s0    = 100.0;
    constexpr double k     = 100.0;
    constexpr double r     = 0.05;
    constexpr double sigma = 0.2;
    constexpr double t     = 1.0;
    double dt              = t / static_cast<double>(steps);
    double drift           = (r - 0.5 * sigma * sigma) * dt;
    double vol             = sigma * std::sqrt(dt);

    std::mt19937_64 rng(static_cast<std::mt19937_64::result_type>(seed));
    std::normal_distribution<double> normal(0.0, 1.0);

    double payoff_sum = 0.0;
    for (int64_t p = 0; p < paths; ++p) {
        double s   = s0;
        double avg = 0.0;
        for (int64_t step = 0; step < steps; ++step) {
            double z = normal(rng);
            s *= std::exp(drift + vol * z);
            avg += s;
        }
        avg /= static_cast<double>(steps);
        double payoff = avg > k ? (avg - k) : 0.0;
        payoff_sum += payoff;
    }

    double price = std::exp(-r * t) * (payoff_sum / static_cast<double>(paths == 0 ? 1 : paths));
    return camel::core::rtdata::toSlot(price);
}
