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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "compile/gir.h"
#include "core/rtdata/data.h"
#include "utils/brpred.h"
#include "utils/log.h"

class Frame;
using frame_rptr_t = Frame *;

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

class FrameTemplate {
  public:
    FrameTemplate() = delete;
    FrameTemplate(GraphIR::Graph *graph, IAllocator &staticAllocator, IAllocator &runtimeAllocator);

    GraphIR::Graph *graph() const { return graph_; }

    Tuple *staticArea() const { return staticArea_; }
    Tuple *makeDynamicArea() const;

    IAllocator &staticAllocator() const { return staticAllocator_; }
    IAllocator &runtimeAllocator() const { return runtimeAllocator_; }

  private:
    GraphIR::Graph *graph_;
    IAllocator &staticAllocator_;
    IAllocator &runtimeAllocator_;
    const TupleTypeLayout *runtimeDataLayout_;
    Tuple *staticArea_ = nullptr;
};

class Frame {
  public:
    Frame() = delete;
    Frame(const FrameTemplate &temp)
        : graph_(temp.graph()), allocator_(&temp.runtimeAllocator()),
          staticArea_(temp.staticArea()) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(staticArea_ != nullptr, "Static tuple is null.");
        dynamicArea_ = temp.makeDynamicArea();
    }
    // 不允许拷贝但可移动
    Frame(const Frame &) = delete;
    Frame(Frame &&other) noexcept
        : graph_(other.graph_), allocator_(other.allocator_), staticArea_(other.staticArea_),
          dynamicArea_(other.dynamicArea_) {
        // 防止已被移动的对象意外释放数据
        other.graph_       = nullptr;
        other.allocator_   = nullptr;
        other.staticArea_  = nullptr;
        other.dynamicArea_ = nullptr;
    }

    Frame &operator=(const Frame &other) = delete;
    Frame &operator=(Frame &&other) noexcept {
        if (this != &other) {
            // 先释放自己的资源
            if (dynamicArea_) {
                allocator_->free(dynamicArea_);
            }
            // 转移资源
            graph_       = other.graph_;
            allocator_   = other.allocator_;
            staticArea_  = other.staticArea_;
            dynamicArea_ = other.dynamicArea_;
            // 防止已被移动的对象意外释放数据
            other.graph_       = nullptr;
            other.allocator_   = nullptr;
            other.staticArea_  = nullptr;
            other.dynamicArea_ = nullptr;
        }
        return *this;
    }

    ~Frame() {
        if (dynamicArea_) {
            // dynamicArea_->~GCTuple();
            allocator_->free(dynamicArea_);
            dynamicArea_ = nullptr;
        }
        // staticArea_ 由外部管理，不释放。
    }

    GraphIR::Graph *graph() { return graph_; }
    const GraphIR::Graph *graph() const { return graph_; }

    TypeCode typeAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicArea_->size(),
                std::format(
                    "Invalid argument index, idx = {}, size = {}",
                    idx,
                    dynamicArea_->size()));
            return dynamicArea_->typeAt(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "Invalid static data index, idx = {}, size = {}",
                    idx,
                    staticArea_->size()));
            return staticArea_->typeAt(idx);
        }
    }

    template <typename T> std::shared_ptr<T> typePtrAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicArea_->size(),
                std::format(
                    "Invalid argument index, idx = {}, size = {}",
                    idx,
                    dynamicArea_->size()));
            auto res = graph_->runtimeDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "Invalid static data index, idx = {}, size = {}",
                    idx,
                    staticArea_->size()));
            auto res = graph_->staticDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        }
    }

    template <typename T> T get(GraphIR::data_idx_t index) {
        ASSERT(index != 0, "Data index is invalid.");
        T res;
        if (LIKELY(index > 0)) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicArea_->size(),
                std::format(
                    "Invalid argument index, idx = {}, size = {}",
                    idx,
                    dynamicArea_->size()));
            res = dynamicArea_->get<T>(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "Invalid static data index, idx = {}, size = {}",
                    idx,
                    staticArea_->size()));
            res = staticArea_->get<T>(idx);
        }
        EXEC_WHEN_DEBUG([&]() {
            std::ostringstream oss;
            printSlot(oss, toSlot(res), typeAt(index));
            l.in("Frame").info(
                "[{}] Getting data of <{}> at index {} ({}): {}",
                formatAddress(this).substr(17, 4),
                graph_->name(),
                index,
                typeCodeToString(typeAt(index)),
                oss.str());
        }());
        return res;
    }

    template <typename T> void set(GraphIR::data_idx_t index, T value) {
        ASSERT(index != 0, "Data index is invalid.");
        EXEC_WHEN_DEBUG([&]() {
            std::ostringstream oss;
            printSlot(oss, toSlot(value), typeAt(index));
            l.in("Frame").info(
                "[{}] Setting data of <{}> at index {} ({}): {}",
                formatAddress(this).substr(17, 4),
                graph_->name(),
                index,
                typeCodeToString(typeAt(index)),
                oss.str());
        }());
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicArea_->size(),
                std::format(
                    "Invalid argument index, idx = {}, size = {}",
                    idx,
                    dynamicArea_->size()));
            dynamicArea_->set<T>(idx, value);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "Invalid static data index, idx = {}, size = {}",
                    idx,
                    staticArea_->size()));
            staticArea_->set<T>(idx, value);
        }
    }

  private:
    GraphIR::Graph *graph_ = nullptr;
    IAllocator *allocator_ = nullptr;
    Tuple *staticArea_     = nullptr;
    Tuple *dynamicArea_    = nullptr;
};
