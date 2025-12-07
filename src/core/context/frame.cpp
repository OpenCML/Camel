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
 * Created: Sep. 16, 2025
 * Updated: Dec. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"

#include "compile/gir.h"

FrameTemplate::FrameTemplate(
    GraphIR::Graph *graph, IAllocator &staticAllocator, IAllocator &dynamicAllocator)
    : graph_(graph), staticAllocator_(staticAllocator), dynamicAllocator_(dynamicAllocator) {
    // 创建静态区 Tuple
    staticArea_ = GCTuple::create(layout_, graph_->staticDataSize(), staticAllocator_);
}

GCTuple *FrameTemplate::makeDynamicArea() const {
    return GCTuple::create(layout_, graph_->argsCount(), dynamicAllocator_);
}

inline std::string formatAddress(void *ptr) {
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);

    std::stringstream ss;
    ss << std::hex << std::uppercase << addr;
    std::string hexStr = ss.str();

    if (hexStr.length() < 16) {
        hexStr = std::string(16 - hexStr.length(), '0') + hexStr;
    }

    std::string formatted;
    for (size_t i = 0; i < hexStr.length(); ++i) {
        formatted += hexStr[i];
        if ((i + 1) % 4 == 0 && i + 1 != hexStr.length())
            formatted += '\'';
    }

    return "0x" + formatted;
}

Frame::Frame(const CompositeDataLayout *layout, GraphIR::Graph *graph, IAllocator &allocator)
    : graph_(graph), allocator_(&allocator), layout_(layout) {
    ASSERT(layout_ != nullptr, "Frame layout is null");

    // 指针数组区大小
    size_t ptrArraySize = sizeof(void *) * layout_->size();

    // 总大小 = 指针数组区 + 数据区
    size_t totalSize = alignUp(ptrArraySize, layout_->align()) + layout_->size();

    data_ = static_cast<void **>(allocator_->alloc(totalSize, layout_->align()));

    // 数据区起始地址
    uint8_t *dataRegion =
        reinterpret_cast<uint8_t *>(data_) + alignUp(ptrArraySize, layout_->align());

    // 初始化指针数组
    for (size_t i = 0; i < layout_->size(); ++i) {
        data_[i] = dataRegion + layout_->offset(i);
    }

    EXEC_WHEN_DEBUG([&]() {
        l.in("Frame").debug(
            "Created Frame for Graph: {} | Total Size: {} bytes",
            graph_->name(),
            totalSize);
        for (size_t i = 0; i < layout_->size(); ++i) {
            l.in("Frame").debug(
                "  Arg[{}] -> Data Ptr: {} | Offset: {} bytes",
                i,
                formatAddress(data_[i]),
                layout_->offset(i));
        }
    }());
}

Frame::~Frame() {
    if (allocator_ && data_) {
        allocator_->free(data_);
    }
}

std::string Frame::toString() const {
    std::stringstream ss;
    ss << "Frame for Graph: " << (graph_ ? graph_->name() : "<null>") << "\n";
    if (graph_) {
        size_t argsCount = graph_->argsCount();
        ss << "Total Args: " << argsCount << "\n";
        for (size_t i = 0; i < argsCount; ++i) {
            ss << "  Arg[" << i << "] -> Data Ptr: " << formatAddress(data_[i]) << "\n";
        }
    } else {
        ss << "Graph is null.\n";
    }
    return ss.str();
}
