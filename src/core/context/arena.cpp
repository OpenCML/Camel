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
 * Created: Jul. 21, 2025
 * Updated: Sep. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "arena.h"

std::string DataArray::toString() const {
    std::string dataPreview;
    if (dataArr_.empty()) {
        dataPreview = "[]";
    } else {
        dataPreview = "[";
        size_t previewCount = std::min(dataArr_.size(), static_cast<size_t>(10));
        for (size_t i = 0; i < previewCount; ++i) {
            if (dataArr_[i]) {
                dataPreview += dataArr_[i]->toString();
            } else {
                dataPreview += "none";
            }
            if (i < previewCount - 1) {
                dataPreview += ", ";
            }
        }
        if (dataArr_.size() > previewCount) {
            dataPreview += ", ...";
        }
        dataPreview += "]";
    }
    return std::format("DataArray({}, {}): {}", std::string(type_), dataArr_.size(), dataPreview);
}
