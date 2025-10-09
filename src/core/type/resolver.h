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
 * Created: Oct. 03, 2024
 * Updated: Oct. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "type.h"

#include <functional>
#include <optional>

using Resolver = std::function<std::optional<type_ptr_t>(const type_vec_t &, const type_vec_t &)>;

class FuncTypeResolver {
  public:
    virtual std::optional<func_type_ptr_t>
    resolve(const type_vec_t &with, const type_vec_t &norm) const = 0;
};

class StaticFuncTypeResolver : public FuncTypeResolver {
  public:
    StaticFuncTypeResolver(const func_type_ptr_t &funcType) : funcType_(funcType) {}

    std::optional<func_type_ptr_t>
    resolve(const type_vec_t &with, const type_vec_t &norm) const override;

  private:
    func_type_ptr_t funcType_;
};

class DynamicFuncTypeResolver : public FuncTypeResolver {
  public:
    DynamicFuncTypeResolver(const Resolver &resolver) : resolver_(resolver) {}

    std::optional<func_type_ptr_t>
    resolve(const type_vec_t &with, const type_vec_t &norm) const override;

  private:
    Resolver resolver_;
};
