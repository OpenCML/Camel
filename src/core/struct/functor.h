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

#include "data.h"
#include "entity.h"
#include "function.h"

class Functor : public Entity {
    func_ptr_t func_;

  public:
    Functor() = delete;
    Functor(const func_ptr_t &func, const data_ptr_t &with = nullptr) : Entity(with), func_(func) { isFunc_ = true; }

    virtual entity_ptr_t clone(bool deep = false) const override {
        entity_ptr_t entity = std::make_shared<Functor>(func_, data_->clone(deep));
        return entity;
    }

    func_ptr_t func() const { return func_; }

    virtual ~Functor() = default;
};

// definition below is forwarded to type.h
// using functor_ptr_t = std::shared_ptr<Functor>;
// using functor_wptr_t = std::weak_ptr<Functor>;
