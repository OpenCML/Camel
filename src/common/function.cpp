/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Aug. 10, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "function.h"
#include "graph.h"

#include <stdexcept>

using namespace std;

FunctionModifier str2modifier(const string &str) {
    if (str == "inner") {
        return FunctionModifier::INNER;
    } else if (str == "outer") {
        return FunctionModifier::OUTER;
    } else if (str == "atomic") {
        return FunctionModifier::ATOMIC;
    } else if (str == "static") {
        return FunctionModifier::SHARED;
    } else if (str == "sync") {
        return FunctionModifier::SYNC;
    } else {
        throw runtime_error("Unknown modifier: " + str);
    }
}

string modifier2str(FunctionModifier modifier) {
    switch (modifier) {
    case FunctionModifier::INNER:
        return "inner";
    case FunctionModifier::OUTER:
        return "outer";
    case FunctionModifier::ATOMIC:
        return "atomic";
    case FunctionModifier::SHARED:
        return "static";
    case FunctionModifier::SYNC:
        return "sync";
    default:
        throw runtime_error("Unknown modifier: " + to_string(static_cast<int>(modifier)));
    }
}

graph_ptr_t Function::constructGraph() { return nullptr; }
