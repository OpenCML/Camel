// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/env.h"
#include "camel/utils/path.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <system_error>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

slot_t __env_get_env__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::nullable(Type::String());
    String *key = norm.get<String *>(0);
    // The variable's name and value are UTF-8; `camel::utils::getEnvIfSet` is the one place that
    // reads them through the wide interface on Windows, where a Chinese name or value is not
    // text in the process code page. A variable that is not set is null; one set to the empty
    // text is the empty text.
    const std::optional<std::string> val = ::getEnvIfSet(key->c_str());
    if (!val)
        return NullSlot;
    return makeSome(toSlot(checkedString(*val, mm::autoSpace(), "env.get_env")), resultType);
}

slot_t __env_get_cwd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::String());
    // The directory may have been removed under the process, or may not be readable.
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) {
        const bool denied = ec == std::errc::permission_denied;
        return makeError(
            denied ? "PermissionDenied" : "NotFound",
            "the working directory cannot be read: " + ec.message(),
            "<env::get_cwd>");
    }
    return makeOk(
        toSlot(checkedString(camel::utils::pathToUtf8(cwd), mm::autoSpace(), "env.get_cwd")),
        resultType);
}

std::unordered_map<std::string, operator_t> getEnvOpsMap() {
    return {{"get_env", __env_get_env__}, {"get_cwd", __env_get_cwd__}};
}
