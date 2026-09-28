/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Registration of the tensor module's operator catalog.
 */

#include "catalog.h"
#include "registry.h"

#include <mutex>

namespace camel::tensor::ops {

void registerTensorOps() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::vector<OpDef> defs;
        for (auto family :
             {elementwiseOps, creationOps, layoutOps, reductionOps, linalgOps, utilityOps}) {
            for (OpDef &def : family()) {
                defs.push_back(std::move(def));
            }
        }
        OpRegistry::instance().add("tensor", std::move(defs));
    });
}

} // namespace camel::tensor::ops
