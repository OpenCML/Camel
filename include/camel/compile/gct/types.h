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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

/** GCT enums and basic types: LoadType and aliases. */

#include <memory>
#include <string>

namespace camel::compile::gct {

class Load;
class Node;
class Tree;

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

/// What selects the arm of a BRCH node (design principles 2.9; all are the same structure on
/// the graph).
///  - Cond: `if` (a boolean) and `match` (a value compared with the cases, or the tag of a
///    `T?` / `T!` with the fixed patterns). Children: condition, CASE...
///  - NullThen: `a ?? b`. Children: a, CASE(True)[EXEC b].
///  - ErrorThen: `a !! b`. Children: a, CASE(True)[EXEC b].
///  - ErrorPass: `x ?-> f`. Children: x, CASE(Else)[EXEC call], where the call takes
///    DREF kPassedValueName as its argument.
enum class BranchKind { Cond, NullThen, ErrorThen, ErrorPass };

/// The pattern of a CASE node.
///  - Value: a value the scrutinee is compared with. Children: the value, the body.
///  - True / Else: the arms of an `if`. One child, the body.
///  - Bind: `case v` over a tagged scrutinee (design principles 2.9): the value taken out of
///    a `T?` / `T!` under the name binding(). One child, the body.
///  - Error: `case error e`: the error of a failed `T!` under that name. One child, the body.
enum class CaseType { Value, True, Else, Bind, Error };

} // namespace camel::compile::gct
