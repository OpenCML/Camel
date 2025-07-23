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
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "operator.h"

using namespace std;

unordered_map<string, std::shared_ptr<operator_vec_t>> globalOperators;

void registerOperator(const operator_ptr_t &&op) {
    const auto &name = op->name();
    if (globalOperators.find(name) == globalOperators.end()) {
        globalOperators[name] = make_shared<operator_vec_t>(1, op);
    } else {
        globalOperators[name]->push_back(op);
    }
}
