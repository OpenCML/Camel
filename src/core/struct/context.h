/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Aug. 18, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

#include "scope.h"
#include "entity.h"

using entity_scope_t = scope_ptr_t<std::string, entity_ptr_t>;

class Context {
    entity_scope_t global_; 
    entity_scope_t scope_;

  public:
    Context() = default;
    virtual ~Context() = default;
};

using context_ptr_t = std::shared_ptr<Context>;