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

#include "data.h"
#include "entity.h"

#include <string>

enum class FunctionModifier {
    INNER = 0b00000001,
    OUTER = 0b00000010,
    ATOMIC = 0b00000100,
    STATIC = 0b00001000,
    SYNC = 0b00010000,
};

FunctionModifier str2modifier(const std::string &str);
std::string modifier2str(FunctionModifier modifier);

class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;

class Function {
  protected:
    unsigned char flags_ = 0;
    graph_ptr_t graph_;

  public:
    Function(const graph_ptr_t &graph) : graph_(graph) {};
    virtual ~Function() = default;

    unsigned char flags() const { return flags_; }

    graph_ptr_t graph() const { return graph_; }

    bool inner() const { return flags_ & static_cast<unsigned char>(FunctionModifier::INNER); }
    bool outer() const { return flags_ & static_cast<unsigned char>(FunctionModifier::OUTER); }
    bool atomic() const { return flags_ & static_cast<unsigned char>(FunctionModifier::ATOMIC); }
    bool static_() const { return flags_ & static_cast<unsigned char>(FunctionModifier::STATIC); }
    bool sync() const { return flags_ & static_cast<unsigned char>(FunctionModifier::SYNC); }

    void setFlags(unsigned char value) { flags_ = value; }

    graph_ptr_t constructGraph();
};