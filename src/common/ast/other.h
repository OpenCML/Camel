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

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "base.h"
#include "common/ref.h"

namespace AbstractSyntaxTree {

class ParamDeclLoad : public Load {
  public:
    ParamDeclLoad(const Reference &ref, bool isVar = false) : Load(LoadType::Type), ref_(ref), isVar_(isVar) {}
    const std::string toString() const override { return "ParamDecl: " + (isVar_ ? std::string("var ") : "") + ref_.toString(); }

  private:
    Reference ref_;
    bool isVar_;
};

class ParamDataLoad : public Load {
  public:
    ParamDataLoad(const Reference &ref) : Load(LoadType::ParamData), ref_(ref) {}
    const std::string toString() const override { return "ParamData: " + ref_.toString(); }

  private:
    Reference ref_;
};

} // namespace AbstractSyntaxTree