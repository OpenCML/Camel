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

#include "stmt.h"

std::string stmtTypeToString(StmtType type) {
    switch (type) {
    case StmtType::DATA:
        return "DATA";
    case StmtType::TYPE:
        return "TYPE";
    case StmtType::FUNC:
        return "FUNC";
    case StmtType::COPY:
        return "COPY";
    case StmtType::NREF:
        return "NREF";
    case StmtType::PACK:
        return "PACK";
    case StmtType::UNPK:
        return "UNPK";
    case StmtType::ANNO:
        return "ANNO";
    case StmtType::LINK:
        return "LINK";
    case StmtType::WITH:
        return "WITH";
    default:
        return "UNKNOWN";
    }
}