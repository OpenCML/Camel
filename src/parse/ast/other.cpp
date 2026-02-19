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
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/parse/ast/other.h"
#include "camel/utils/str.h"

namespace AbstractSyntaxTree {

const std::string ParamDeclLoad::geneCode() const {
    return (isVar_ ? std::string("var ") : "") + ref_.toString();
}

const std::string ParamDataLoad::geneCode() const { return ref_.toString(); }

} // namespace AbstractSyntaxTree
