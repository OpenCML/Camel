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

#include <iomanip>
#include <iostream>
#include <sstream>

#include "sem.h"

std::string pointerToHex(const void *ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(sizeof(void *) * 2) << std::setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

const std::string SemanticNode::typeStr() const {
    switch (type_) {
    case SemNodeType::DATA:
        return "DATA";
    case SemNodeType::TYPE:
        return "TYPE";
    case SemNodeType::FUNC:
        return "FUNC";
    case SemNodeType::COPY:
        return "COPY";
    case SemNodeType::NREF:
        return "NREF";
    case SemNodeType::DREF:
        return "DREF";
    case SemNodeType::ASSN:
        return "ASSN";
    case SemNodeType::ANNO:
        return "ANNO";
    case SemNodeType::LINK:
        return "LINK";
    case SemNodeType::CALL:
        return "CALL";
    case SemNodeType::RETN:
        return "RETN";
    case SemNodeType::EXEC:
        return "EXEC";
    default:
        return "UNKNOWN";
    }
}

const std::string DataNode::toString() const {
    std::stringstream ss;
    ss << "DATA: " << pointerToHex(this) << ", " << entity_->typeStr() << ", " << entity_->dataStr();
    return ss.str();
}

const std::string TypeNode::toString() const {
    std::stringstream ss;
    ss << "TYPE: " << pointerToHex(this) << ", " << type_->toString();
    return ss.str();
}