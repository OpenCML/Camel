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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gct/load.h"

#include <iomanip>
#include <sstream>

namespace camel::compile::gct {

static std::string pointerToHex(const void *ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0')
       << reinterpret_cast<uintptr_t>(ptr) << std::dec << std::nouppercase;
    return ss.str();
}

const std::string DataLoad::toString() const {
    std::stringstream ss;
    ss << "DATA: " << pointerToHex(data_.get()) << ", ";
    const auto &type = data_->type();
    if (type) {
        ss << type->toString();
    } else {
        ss << "null";
    }
    ss << ", " << data_->toString();
    return ss.str();
}

const std::string TypeLoad::toString() const {
    std::stringstream ss;
    ss << "TYPE: " << dataType_->toString();
    return ss.str();
}

const std::string DeclLoad::toString() const {
    return "DECL: " + std::string(isFunc_ ? "func " : "type ") + ref_.toString();
}

const std::string FuncLoad::toString() const {
    std::stringstream ss;
    ss << "FUNC: ";
    if (!name_.empty()) {
        ss << name_;
    } else {
        ss << "(anonymous)";
    }
    return ss.str();
}

const std::string NRefLoad::toString() const { return "NREF: " + ref_.toString(); }

const std::string DRefLoad::toString() const { return "DREF: " + ref_.toString(); }

} // namespace camel::compile::gct
