// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/utils/env.h"
#include "camel/utils/path.h"

#include <cstdlib>
#include <filesystem>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;

slot_t __env_get_env__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *key = norm.get<String *>(0);
    // The variable's name and value are UTF-8; `camel::utils::getEnv` is the one place that reads
    // them through the wide interface on Windows, where a Chinese name or value is not text in
    // the process code page.
    const std::string val = ::getEnv(key->c_str());
    if (val.empty())
        return toSlot(String::from("", mm::autoSpace()));
    return toSlot(checkedString(val, mm::autoSpace(), "env.get_env"));
}

slot_t __env_get_cwd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const std::string cwd = camel::utils::currentDirectory();
    return toSlot(checkedString(cwd, mm::autoSpace(), "env.get_cwd"));
}

std::unordered_map<std::string, operator_t> getEnvOpsMap() {
    return {{"get_env", __env_get_env__}, {"get_cwd", __env_get_cwd__}};
}
