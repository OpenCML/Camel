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

#include "context.h"
#include "data.h"

#include <functional>
#include <unordered_map>

using Operator = std::function<data_ptr_t(Context &, data_lst_t &&)>;

extern std::unordered_map<std::string, Operator> operators;

void registerOperator(const std::string &name, Operator &&op);