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
#include <iterator>
#include <sstream>

#include "ast.h"

using namespace std;
using namespace ast;

string pointerToHex(const void *ptr) {
    stringstream ss;
    ss << "0x" << hex << setw(sizeof(void *) * 2) << setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

const string Load::typeStr() const {
    switch (type_) {
    case NodeType::DATA:
        return "DATA";
    case NodeType::VARI:
        return "VARI";
    case NodeType::TYPE:
        return "TYPE";
    case NodeType::FUNC:
        return "FUNC";
    case NodeType::NREF:
        return "NREF";
    case NodeType::DREF:
        return "DREF";
    case NodeType::WAIT:
        return "WAIT";
    case NodeType::ANNO:
        return "ANNO";
    case NodeType::LINK:
        return "LINK";
    case NodeType::WITH:
        return "WITH";
    case NodeType::RETN:
        return "RETN";
    case NodeType::EXEC:
        return "EXEC";
    case NodeType::FROM:
        return "FROM";
    default:
        return "REF";
    }
}

const string DataLoad::toString() const {
    stringstream ss;
    ss << "DATA: " << pointerToHex(data_.get()) << ", ";
    const auto &type = data_->type();
    if (type) {
        ss << type->toString();
    } else {
        ss << "NULL";
    }
    ss << ", " << data_->toString();
    return ss.str();
}

const string TypeLoad::toString() const {
    stringstream ss;
    ss << "TYPE: " << type_->toString();
    return ss.str();
}

const string NRefLoad::toString() const { return "NREF: " + ident_; }

const string DRefLoad::toString() const { return "DREF: " + ident_; }

const string WaitLoad::toString() const {
    ostringstream oss;
    oss << "WAIT: ";
    if (!idents_.empty()) {
        copy(idents_.begin(), idents_.end() - 1, ostream_iterator<string>(oss, ", "));
        oss << idents_.back();
    }
    return oss.str();
}

const string FromLoad::toString() const {
    ostringstream oss;
    oss << "FROM: '" << path_ << "' USE { ";
    if (idents_.empty()) {
        oss << "*";
    } else {
        copy(idents_.begin(), idents_.end() - 1, ostream_iterator<string>(oss, ", "));
        oss << idents_.back();
    }
    oss << " }";
    return oss.str();
}
