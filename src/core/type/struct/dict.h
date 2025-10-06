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
 * Created: Oct. 06, 2024
 * Updated: Oct. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "struct.h"

class DictType : public StructType {
  private:
    // field name -> field type with default value
    std::unordered_map<std::string, type_ptr_t> fields_;

  public:
    DictType();

    std::string toString() const override;

    std::optional<type_ptr_t> typeAt(struct_idx_t idx) const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    bool add(const std::string &name, const type_ptr_t &type);
    bool del(const std::string &name);
    bool has(const std::string &name) const;
    void set(const std::string &name, const type_ptr_t &type);
    type_ptr_t get(const std::string &name) const;

    void clear();

    type_ptr_t operator|(const DictType &other) const;
    type_ptr_t operator&(const DictType &other) const;

    CastSafety castSafetyTo(const Type &other) const override;
};
