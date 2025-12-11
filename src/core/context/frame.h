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
    // 构造一个空 Frame，仅供移动使用
    Frame() : graph_(nullptr), allocator_(nullptr), staticArea_(nullptr), dynamicArea_(nullptr) {};
    Frame(const FrameTemplate &temp)
        : graph_(temp.graph()), allocator_(&temp.runtimeAllocator()),
          staticArea_(temp.staticArea()) {
        ASSERT(graph_ != nullptr, "Frame graph is null.");
        ASSERT(staticArea_ != nullptr, "Static tuple is null.");
        dynamicArea_ = temp.makeDynamicArea();
        EXEC_WHEN_DEBUG(l.in("Frame").info(
            "[{}] Created Frame({}) for Graph <{}>",
            formatAddress(this, true),
            formatAddress(dynamicArea_, true),
            graph_->name()));
    }
    // 不允许拷贝但可移动
    Frame(const Frame &) = delete;
    Frame(Frame &&other) noexcept
        : graph_(other.graph_), allocator_(other.allocator_), staticArea_(other.staticArea_),
          dynamicArea_(other.dynamicArea_) {
        ASSERT(graph_ == nullptr, "Only allow move construction from an empty Frame.");
        EXEC_WHEN_DEBUG(l.in("Frame").info(
            "[{}] Moved Frame({}) for Graph <{}>",
            formatAddress(this, true),
            formatAddress(dynamicArea_, true),
            graph_->name()));
        // 防止已被移动的对象意外释放数据
        other.graph_       = nullptr;
        other.allocator_   = nullptr;
        other.staticArea_  = nullptr;
        other.dynamicArea_ = nullptr;
    }

    ~Frame() {
        EXEC_WHEN_DEBUG(l.in("Frame").info(
            "[{}] Destroyed Frame({}) for Graph <{}>",
            formatAddress(this, true),
            formatAddress(dynamicArea_, true),
            graph_ ? graph_->name() : "null"));
        if (dynamicArea_) {
            allocator_->free(dynamicArea_);
            dynamicArea_ = nullptr;
        }
    }

    Frame &operator=(const Frame &other) = delete;
    Frame &operator=(Frame &&other) noexcept {
        ASSERT(graph_ == nullptr, "Only allow move construction from an empty Frame.");
        if (this != &other) {
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
                formatAddress(this, true),
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
                formatAddress(this, true),
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
