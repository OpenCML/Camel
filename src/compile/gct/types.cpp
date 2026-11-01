// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/gct/types.h"
#include "camel/utils/assert.h"
#include <stdexcept>

namespace camel::compile::gct {

std::string to_string(NodeKind kind) {
    switch (kind) {
    case NodeKind::DECL:
        return "DECL";
    case NodeKind::FUNC:
        return "FUNC";
    case NodeKind::DATA:
        return "DATA";
    case NodeKind::TYPE:
        return "TYPE";
    case NodeKind::NREF:
        return "NREF";
    case NodeKind::DREF:
        return "DREF";
    case NodeKind::VARI:
        return "VARI";
    case NodeKind::LINK:
        return "LINK";
    case NodeKind::WITH:
        return "WITH";
    case NodeKind::TAPP:
        return "TAPP";
    case NodeKind::ACCS:
        return "ACCS";
    case NodeKind::BRCH:
        return "BRCH";
    case NodeKind::CASE:
        return "CASE";
    case NodeKind::CAST:
        return "CAST";
    case NodeKind::ANNO:
        return "ANNO";
    case NodeKind::EXIT:
        return "EXIT";
    case NodeKind::EXEC:
        return "EXEC";
    case NodeKind::EXPT:
        return "EXPT";
    case NodeKind::LOOP:
        return "LOOP";
    case NodeKind::TMTCH:
        return "TMTCH";
    case NodeKind::TCASE:
        return "TCASE";
    case NodeKind::SFOR:
        return "SFOR";
    case NodeKind::SACC:
        return "SACC";
    case NodeKind::SPRD:
        return "SPRD";
    case NodeKind::SCOMP:
        return "SCOMP";
    case NodeKind::SLOOP:
        return "SLOOP";
    default:
        throw std::runtime_error("Unknown NodeKind");
    }
    return "UNKNOWN";
}

} // namespace camel::compile::gct
