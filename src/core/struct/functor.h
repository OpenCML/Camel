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

#pragma once

#include "entity.h"
#include "value.h"

class Functor : Entity {
    value_ptr_t input;
    value_ptr_t output;

  public:
    Functor() = delete;
    Functor(const value_ptr_t &input, const value_ptr_t &output, const value_ptr_t &with = nullptr)
        : Entity(with), input(input), output(output) {}
    virtual ~Functor() = default;

    virtual void apply() = 0;
};

// definition below is forwarded to type.h
// using functor_ptr_t = std::shared_ptr<Functor>;
// using functor_wptr_t = std::weak_ptr<Functor>;
