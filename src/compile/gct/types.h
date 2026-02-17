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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

/** GCT 枚举与基础类型：LoadType、类型别名。 */

#include <memory>
#include <string>

namespace GraphConstructTree {

class Load;
class Node;

using load_ptr_t = std::shared_ptr<Load>;
using node_ptr_t = std::shared_ptr<Node>;

enum class LoadType {
    DECL,
    FUNC,
    DATA,
    TYPE,
    NREF,
    DREF,
    VARI,
    WAIT,
    LINK,
    WITH,
    ACCS,
    BRCH,
    CASE,
    CAST,
    ANNO,
    EXIT,
    EXEC,
    EXPT,
};

std::string to_string(LoadType type);

} // namespace GraphConstructTree
