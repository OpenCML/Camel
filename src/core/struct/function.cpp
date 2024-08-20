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
 * Created: Aug. 10, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "function.h"

#include <stdexcept>

FunctionModifier str2modifier(const std::string &str) {
    if (str == "inner") {
        return FunctionModifier::INNER;
    } else if (str == "outer") {
        return FunctionModifier::OUTER;
    } else if (str == "atomic") {
        return FunctionModifier::ATOMIC;
    } else if (str == "static") {
        return FunctionModifier::STATIC;
    } else if (str == "sync") {
        return FunctionModifier::SYNC;
    } else {
        throw std::runtime_error("Unknown modifier: " + str);
    }
}

std::string modifier2str(FunctionModifier modifier) {
    switch (modifier) {
    case FunctionModifier::INNER:
        return "inner";
    case FunctionModifier::OUTER:
        return "outer";
    case FunctionModifier::ATOMIC:
        return "atomic";
    case FunctionModifier::STATIC:
        return "static";
    case FunctionModifier::SYNC:
        return "sync";
    default:
        throw std::runtime_error("Unknown modifier: " + std::to_string(static_cast<int>(modifier)));
    }
}