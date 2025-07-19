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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"
#include "utils/str.h"

namespace AbstractSyntaxTree {

std::string to_string(LoadType type) {
    switch (type) {
        case LoadType::Module: return "Module";
        case LoadType::Import: return "Import";
        case LoadType::Export: return "Export";
        case LoadType::Stmt: return "Stmt";
        case LoadType::Data: return "Data";
        case LoadType::Type: return "Type";
        case LoadType::NamedData: return "NamedData";
        case LoadType::NamedType: return "NamedType";
        case LoadType::NamedPair: return "NamedPair";
        case LoadType::Repeated: return "Repeated";
        case LoadType::Optional: return "Optional";
        default: throw std::runtime_error("Unknown LoadType");
    }
}

} // namespace AbstractSyntaxTree