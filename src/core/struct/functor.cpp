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
 * Created: May. 5, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "functor.h"

FunctorModifier str2modifier(const std::string &str) {
    if (str == "inner") {
        return FunctorModifier::INNER;
    } else if (str == "outer") {
        return FunctorModifier::OUTER;
    } else if (str == "atomic") {
        return FunctorModifier::ATOMIC;
    } else if (str == "static") {
        return FunctorModifier::STATIC;
    } else if (str == "sync") {
        return FunctorModifier::SYNC;
    } else {
        throw std::runtime_error("Unknown modifier: " + str);
    }
}

std::string modifier2str(FunctorModifier modifier) {
    switch (modifier) {
    case FunctorModifier::INNER:
        return "inner";
    case FunctorModifier::OUTER:
        return "outer";
    case FunctorModifier::ATOMIC:
        return "atomic";
    case FunctorModifier::STATIC:
        return "static";
    case FunctorModifier::SYNC:
        return "sync";
    default:
        throw std::runtime_error("Unknown modifier: " + std::to_string(static_cast<int>(modifier)));
    }
}