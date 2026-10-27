// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operators of the standard modules that the programs std::cpp writes carry out themselves
 * (each as the module's implementation named with it does), so that using them does not make
 * a program depend on the runtime.
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/opt.h"
#include "camel/standalone/str.h"
#include "camel/utils/env.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace camel::standalone {

/// `time.now()` (time:now, modules/time/operators.cpp): seconds since the epoch.
inline double now() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

/// `env.get_env(name)` (env:get_env, modules/env/operators.cpp): the variable's value, null when it
/// is not set; a variable set to the empty text gives the empty text.
inline Opt<Str> getEnv(const Str &name) {
    // The one place that reads the environment (camel/utils/env.h): through the wide interface
    // on Windows, so that a value the process code page cannot express is not lost here either.
    const std::optional<std::string> value = ::getEnvIfSet(name.str().c_str());
    return value ? Opt<Str>(Str(*value)) : Opt<Str>();
}

} // namespace camel::standalone
