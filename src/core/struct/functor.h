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
#include "stmt.h"

class Functor;
using functor_ptr_t = std::shared_ptr<const Functor>;

enum class FunctorModifier {
    INNER,
    OUTER,
    ATOMIC,
    SYNC,
    STATIC,
};

class Functor : Entity {
  protected:
    bool inner_ = false;
    bool outer_ = false;
    bool atomic_ = false;
    bool sync_ = false;
    bool static_ = false;

    entity_ptr_t input;
    entity_ptr_t output;

  public:
    Functor() = delete;
    Functor(const entity_ptr_t &input, const entity_ptr_t &output, const value_ptr_t &data = nullptr)
        : Entity(nullptr, data), input(input), output(output) {}
    virtual ~Functor() = default;

    template <typename T> void setFlag(bool value) {
        if constexpr (std::is_same_v<T, FunctorModifier::INNER>) {
            inner_ = value;
        } else if constexpr (std::is_same_v<T, FunctorModifier::OUTER>) {
            outer_ = value;
        } else if constexpr (std::is_same_v<T, FunctorModifier::ATOMIC>) {
            atomic_ = value;
        } else if constexpr (std::is_same_v<T, FunctorModifier::SYNC>) {
            sync_ = value;
        } else if constexpr (std::is_same_v<T, FunctorModifier::STATIC>) {
            static_ = value;
        } else {
            static_assert(always_false<T>::value, "Unsupported flag type");
        }
    }

    virtual void execute() = 0;
};

class InnerFunctor : Functor {
  public:
    InnerFunctor(const entity_ptr_t &input, const entity_ptr_t &output, const value_ptr_t &data = nullptr)
        : Functor(input, output, data) {
        inner_ = true;
    }
    virtual ~InnerFunctor() = default;

    virtual void execute() = 0;
};

class OuterFunctor : Functor {
  protected:
    std::string name_;
    std::string path_;

  public:
    OuterFunctor(const entity_ptr_t &input, const entity_ptr_t &output, const value_ptr_t &data = nullptr)
        : Functor(input, output, data) {
        outer_ = true;
    }
    virtual ~OuterFunctor() = default;

    virtual void execute() = 0;
};