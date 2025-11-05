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
 * Updated: Nov. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "frame.h"
#include "compile/gir.h"

using namespace GraphIR;

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

size_t getTypeSize(const type_ptr_t &type);  // 返回类型占用的字节数
size_t getTypeAlign(const type_ptr_t &type); // 返回类型对齐字节数

FrameTemplate::FrameTemplate(GraphIR::Graph *g) : graph(g) {
    // 收集 Graph 中的全部参数节点 (normPorts, withPorts, closure)
    std::vector<node_ptr_t> allArgs;
    allArgs.insert(allArgs.end(), g->normPorts().begin(), g->normPorts().end());
    allArgs.insert(allArgs.end(), g->withPorts().begin(), g->withPorts().end());
    allArgs.insert(allArgs.end(), g->closure().begin(), g->closure().end());

    size_t argsCount = allArgs.size();

    // 指针数组区大小: 每个参数一个 void*
    dataOffset = argsCount * sizeof(void *);

    // 分配 offsets 数组
    offsets.resize(argsCount);

    size_t currentOffset = 0;
    for (size_t i = 0; i < argsCount; ++i) {
        // 获取当前参数的类型信息
        type_ptr_t type = allArgs[i]->dataType();

        // 获取类型大小和对齐
        size_t typeSize = getTypeSize(type);
        size_t typeAlign = getTypeAlign(type);

        // 对齐当前 offset
        currentOffset = (currentOffset + (typeAlign - 1)) & ~(typeAlign - 1);

        // 记录该参数在数据区的偏移
        offsets[i] = currentOffset;

        // 增加该参数类型大小
        currentOffset += typeSize;
    }

    // 总大小 = 指针数组区 + 数据区大小
    totalSize = dataOffset + currentOffset;

    // 总体对齐到 64 字节（cache line 对齐）
    constexpr size_t FRAME_ALIGN = 64;
    totalSize = (totalSize + FRAME_ALIGN - 1) & ~(FRAME_ALIGN - 1);
}

Frame::Frame(const FrameTemplate &temp, IAllocator &allocator)
    : graph_(temp.graph), allocator_(&allocator) {
    // 分配整个 Frame 内存块（指针数组区 + 数据区）
    char *block = static_cast<char *>(allocator.alloc(temp.totalSize));

    // data_ 指向指针数组区的起始位置
    data_ = reinterpret_cast<void **>(block);

    // dataPtr 指向数据区起始位置
    char *dataPtr = block + temp.dataOffset;

    // 初始化指针数组，把每个参数的指针指向数据区对应偏移
    for (size_t i = 0; i < temp.argsCount(); ++i) {
        data_[i] = dataPtr + temp.offsets[i];
    }

    EXEC_WHEN_DEBUG([&]() {
        l.in("Frame").debug(
            "Created Frame for Graph: {} | Total Size: {} bytes | Data Offset: {} bytes",
            graph_->name(),
            temp.totalSize,
            temp.dataOffset);
        for (size_t i = 0; i < temp.argsCount(); ++i) {
            l.in("Frame").debug(
                "  Arg[{}] -> Data Ptr: {} | Offset: {} bytes",
                i,
                formatAddress(data_[i]),
                temp.offsets[i]);
        }
        // 将 data block 格式化为 NULL，以便调试查看
        std::fill_n(dataPtr, temp.totalSize - temp.dataOffset, 0);
    }());
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
