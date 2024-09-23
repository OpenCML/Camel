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

#include "ast.h"

std::string pointerToHex(const void *ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(sizeof(void *) * 2) << std::setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

const std::string ASTNodeLoad::typeStr() const {
    switch (type_) {
    case ASTNodeType::DATA:
        return "DATA";
    case ASTNodeType::VARI:
        return "VARI";
    case ASTNodeType::TYPE:
        return "TYPE";
    case ASTNodeType::FUNC:
        return "FUNC";
    case ASTNodeType::NREF:
        return "NREF";
    case ASTNodeType::DREF:
        return "DREF";
    case ASTNodeType::WAIT:
        return "WAIT";
    case ASTNodeType::ANNO:
        return "ANNO";
    case ASTNodeType::LINK:
        return "LINK";
    case ASTNodeType::WITH:
        return "WITH";
    case ASTNodeType::RETN:
        return "RETN";
    case ASTNodeType::EXEC:
        return "EXEC";
    default:
        return "UNKNOWN";
    }
}

const std::string DataASTLoad::toString() const {
    std::stringstream ss;
    ss << "DATA: " << pointerToHex(data_.get()) << ", " << data_->type()->toString() << ", " << data_->toString();
    return ss.str();
}

const std::string TypeASTLoad::toString() const {
    std::stringstream ss;
    ss << "TYPE: " << type_->toString();
    return ss.str();
}

const std::string NRefASTLoad::toString() const { return "NREF: " + ident_; }

const std::string DRefASTLoad::toString() const { return "DREF: " + ident_; }

const std::string WaitASTLoad::toString() const {
    std::stringstream ss;
    ss << "WAIT: ";
    for (const auto &ident : idents_) {
        ss << ident << ", ";
    }
    std::string res = std::move(ss.str());
    res.pop_back();
    res.pop_back();
    return res;
}