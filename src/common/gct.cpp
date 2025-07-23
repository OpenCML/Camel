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
 * Created: May. 05, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iterator>

#include "gct.h"
#include "utils/log.h"

using namespace std;

namespace GraphConstructTree {

std::string to_string(LoadType type) {
    switch (type) {
    case LoadType::DATA:
        return "DATA";
    case LoadType::VARI:
        return "VARI";
    case LoadType::TYPE:
        return "TYPE";
    case LoadType::DECL:
        return "DECL";
    case LoadType::FUNC:
        return "FUNC";
    case LoadType::NREF:
        return "NREF";
    case LoadType::DREF:
        return "DREF";
    case LoadType::WAIT:
        return "WAIT";
    case LoadType::ANNO:
        return "ANNO";
    case LoadType::LINK:
        return "LINK";
    case LoadType::WITH:
        return "WITH";
    case LoadType::BIND:
        return "BIND";
    case LoadType::EXIT:
        return "EXIT";
    case LoadType::EXEC:
        return "EXEC";
    case LoadType::ACCS:
        return "ACCS";
    case LoadType::BRCH:
        return "BRCH";
    default:
        ASSERT(false, "Unknown NodeType");
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
    ss << "TYPE: " << dataType_->toString();
    return ss.str();
}

const string DeclLoad::toString() const { return "DECL: " + std::string(isFunc_ ? "func " : "type ") + ref_.toString(); }

const string FuncLoad::toString() const {
    stringstream ss;
    ss << "FUNC: ";
    if (!name_.empty()) {
        ss << name_;
    } else {
        ss << "(anonymous)";
    }
    return ss.str();
}

const string NRefLoad::toString() const { return "NREF: " + ref_.toString(); }

const string DRefLoad::toString() const { return "DREF: " + ref_.toString(); }

} // namespace GraphConstructTree
