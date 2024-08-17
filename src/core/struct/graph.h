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
 * Created: Aug. 13, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <list>
#include <any>

class Operation;
class GraphNode;

class GraphNode {
protected:
    std::any data;
    Operation* operation;
    std::list<GraphNode*> inputs;
    std::list<GraphNode*> outputs;
    bool computed = false;

public:
    GraphNode() = default;
    virtual ~GraphNode() = default;

    virtual void run() = 0;
};