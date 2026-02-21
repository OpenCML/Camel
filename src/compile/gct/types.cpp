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
 * Updated: Feb. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gct/types.h"
#include "camel/utils/assert.h"

namespace GraphConstructTree {

std::string to_string(LoadType type) {
    switch (type) {
    case LoadType::DECL:
        return "DECL";
    case LoadType::FUNC:
        return "FUNC";
    case LoadType::DATA:
        return "DATA";
    case LoadType::TYPE:
        return "TYPE";
    case LoadType::NREF:
        return "NREF";
    case LoadType::DREF:
        return "DREF";
    case LoadType::VARI:
        return "VARI";
    case LoadType::WAIT:
        return "WAIT";
    case LoadType::LINK:
        return "LINK";
    case LoadType::WITH:
        return "WITH";
    case LoadType::ACCS:
        return "ACCS";
    case LoadType::BRCH:
        return "BRCH";
    case LoadType::CASE:
        return "CASE";
    case LoadType::CAST:
        return "CAST";
    case LoadType::ANNO:
        return "ANNO";
    case LoadType::EXIT:
        return "EXIT";
    case LoadType::EXEC:
        return "EXEC";
    case LoadType::EXPT:
        return "EXPT";
    default:
        ASSERT(false, "Unknown LoadType");
    }
    return "UNKNOWN";
}

} // namespace GraphConstructTree
