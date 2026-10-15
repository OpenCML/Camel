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
 * Created: May. 05, 2024
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gct/types.h"
#include "camel/utils/assert.h"

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
    case NodeKind::WAIT:
        return "WAIT";
    case NodeKind::LINK:
        return "LINK";
    case NodeKind::WITH:
        return "WITH";
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
    default:
        ASSERT(false, "Unknown NodeKind");
    }
    return "UNKNOWN";
}

} // namespace camel::compile::gct
