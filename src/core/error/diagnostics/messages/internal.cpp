// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/error/diagnostics/messages/internal.h"

namespace camel::core::error {

const std::unordered_map<InternalDiag, DiagInfo> &getInternalDiagInfoMap() {
    static const std::unordered_map<InternalDiag, DiagInfo> infoMap = {
        {
            InternalDiag::UnknownInternalError,
            {
                "UnknownInternalError",
                "An error has occurred: {0}.",
                "",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
