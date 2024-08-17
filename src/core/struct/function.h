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

#pragma once

#include "entity.h"
#include "value.h"
#include <string>

enum class FunctionModifier {
    INNER,
    OUTER,
    ATOMIC,
    STATIC,
    SYNC,
};

FunctionModifier str2modifier(const std::string &str);
std::string modifier2str(FunctionModifier modifier);

class Function {
  protected:
    bool inner_ = false;
    bool outer_ = false;
    bool atomic_ = false;
    bool static_ = false;
    bool sync_ = false;

  public:
    Function() = default;
    virtual ~Function() = default;

    template <typename T> void setFlag(bool value) {
        if constexpr (std::is_same_v<T, FunctionModifier::INNER>) {
            inner_ = value;
        } else if constexpr (std::is_same_v<T, FunctionModifier::OUTER>) {
            outer_ = value;
        } else if constexpr (std::is_same_v<T, FunctionModifier::ATOMIC>) {
            atomic_ = value;
        } else if constexpr (std::is_same_v<T, FunctionModifier::STATIC>) {
            static_ = value;
        } else if constexpr (std::is_same_v<T, FunctionModifier::SYNC>) {
            sync_ = value;
        } else {
            static_assert(std::always_false<T>, "Unsupported flag type");
        }
    }

    virtual void apply() = 0;
};

class InnerFunction : Function {
  public:
    InnerFunction() { inner_ = true; }
    virtual ~InnerFunction() = default;

    virtual void apply() = 0;
};

class OuterFunction : Function {
  protected:
    std::string name_;
    std::string path_;

  public:
    OuterFunction() { outer_ = true; }
    virtual ~OuterFunction() = default;

    virtual void apply() = 0;
};