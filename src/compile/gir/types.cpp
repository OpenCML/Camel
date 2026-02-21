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
 * Created: Aug. 17, 2024
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/types.h"
#include "camel/utils/assert.h"

namespace GraphIR {

std::string to_string(NodeType type) {
    switch (type) {
    case NodeType::DATA:
        return "DATA";
    case NodeType::PORT:
        return "PORT";
    case NodeType::CAST:
        return "CAST";
    case NodeType::COPY:
        return "COPY";
    case NodeType::FILL:
        return "FILL";
    case NodeType::ACCS:
        return "ACCS";
    case NodeType::BRCH:
        return "BRCH";
    case NodeType::JOIN:
        return "JOIN";
    case NodeType::CALL:
        return "CALL";
    case NodeType::BIND:
        return "BIND";
    case NodeType::FUNC:
        return "FUNC";
    case NodeType::OPER:
        return "OPER";
    case NodeType::EXIT:
        return "EXIT";
    case NodeType::DREF:
        return "DREF";
    case NodeType::SYNC:
        return "SYNC";
    case NodeType::NREF:
        return "NREF";
    }
    ASSERT(false, "Unknown NodeType");
    return "Unknown";
}

std::string to_string(LinkType type) {
    switch (type) {
    case LinkType::Norm:
        return "Norm";
    case LinkType::With:
        return "With";
    case LinkType::Ctrl:
        return "Ctrl";
    }
    ASSERT(false, "Unknown LinkType");
    return "Unknown";
}

} // namespace GraphIR
