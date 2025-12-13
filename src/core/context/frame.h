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
 * Updated: Dec. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "compile/gir.h"
#include "core/rtdata/data.h"
#include "utils/brpred.h"
#include "utils/log.h"

struct FrameMeta {
    size_t frameSize;
    const TupleTypeLayout *runtimeAreaLayout;
    Tuple *staticArea;
};

FrameMeta *installFrameMetaInfoForGraph(GraphIR::Graph *graph);

class FramePool;

class Frame : public Object {
  public:
    Frame() = delete;

    GraphIR::Graph *graph() { return graph_; }
    const GraphIR::Graph *graph() const { return graph_; }

    TypeCode typeAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicAreaLayout_->size(),
                std::format(
                    "[{}] Invalid argument index, idx = {}, size = {}",
                    formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    dynamicAreaLayout_->size()));
            return dynamicAreaLayout_->typeAt(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "[{}] Invalid static data index, idx = {}, size = {}",
                    formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    staticArea_->size()));
            return staticArea_->typeAt(idx);
        }
    }

    template <typename T> std::shared_ptr<T> typePtrAt(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicAreaLayout_->size(),
                std::format(
                    "[{}] Invalid argument index, idx = {}, size = {}",
                    formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    dynamicAreaLayout_->size()));
            auto res = graph_->runtimeDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "[{}] Invalid static data index, idx = {}, size = {}",
                    formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    staticArea_->size()));
            auto res = graph_->staticDataType()->typeAt(idx);
            ASSERT(res.has_value(), std::format("Type at index {} is null.", idx));
            return tt::as_shared<T>(res.value());
        }
    }

    template <typename T> T get(GraphIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        T res;
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            EXEC_WHEN_DEBUG([&]() {
                ASSERT(
                    idx < dynamicAreaLayout_->size(),
                    std::format(
                        "[{}] Invalid argument index, idx = {}, size = {}",
                        formatAddress(const_cast<Frame *>(this), true),
                        idx,
                        dynamicAreaLayout_->size()));
                ASSERT(
                    dynamicArea_[idx] != kDebugUninitializedSlot,
                    std::format(
                        "[{}] Accessing uninitialized slot: idx = {}",
                        formatAddress(const_cast<Frame *>(this), true),
                        index));
            }());
            res = fromSlot<T>(dynamicArea_[idx]);
        } else {
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG([&]() {
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "[{}] Invalid static data index, idx = {}, size = {}",
                        formatAddress(const_cast<Frame *>(this), true),
                        idx,
                        staticArea_->size()));
            }());
            res = staticArea_->get<T>(idx);
        }
        EXEC_WHEN_DEBUG([&]() {
            std::ostringstream oss;
            printSlot(oss, toSlot(res), typeAt(index));
            l.in("Frame").info(
                "[{}] Getting data of graph <{}> at index {} ({}): {}",
                formatAddress(const_cast<Frame *>(this), true),
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
                "[{}] Setting data of graph <{}> at index {} ({}): {}",
                formatAddress(this, true),
                graph_->name(),
                index,
                typeCodeToString(typeAt(index)),
                oss.str());
        }());
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            EXEC_WHEN_DEBUG([&]() {
                ASSERT(
                    idx < dynamicAreaLayout_->size(),
                    std::format(
                        "Invalid argument index, idx = {}, size = {}",
                        idx,
                        dynamicAreaLayout_->size()));
            }());
            dynamicArea_[idx] = toSlot(value);
        } else {
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG([&]() {
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "Invalid static data index, idx = {}, size = {}",
                        idx,
                        staticArea_->size()));
            }());
            staticArea_->set<T>(idx, value);
        }
    }

    virtual bool equals(const Object *other, bool deep = false) const override {
        return false; // Frame 没有实际意义的比较
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        return nullptr; // 不支持克隆
    }

    virtual void print(std::ostream &os) const override {
        os << "Frame(dynamicSize=" << dynamicAreaLayout_->size()
           << ", staticSize=" << staticArea_->size() << ")";
    }

    virtual void onMoved() override {}

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        const auto &types = dynamicAreaLayout_->elemTypes();
        Object **refArr   = reinterpret_cast<Object **>(dynamicArea_);
        for (size_t i = 0; i < dynamicAreaLayout_->size(); ++i) {
            if (isGCTraced(types[i])) {
                if (Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    friend class FramePool;
    // 只能由 FramePool 调用
    Frame(GraphIR::Graph *graph, Tuple *staticArea, const TupleTypeLayout *dynamicAreaLayout)
        : graph_(graph), staticArea_(staticArea), dynamicAreaLayout_(dynamicAreaLayout) {
        EXEC_WHEN_DEBUG([&]() {
            // 把 dynamic 区所有 slot 写成 魔数，用于检测脏读
            size_t n = dynamicAreaLayout_->size();
            for (size_t i = 0; i < n; ++i) {
                dynamicArea_[i] = kDebugUninitializedSlot;
            }
        }());
    }

    GraphIR::Graph *graph_;
    Frame *nextFrame_;
    Tuple *staticArea_; // 外部提供的静态区
    const TupleTypeLayout *dynamicAreaLayout_;
    slot_t dynamicArea_[]; // 紧跟对象后存放动态区
};

class FramePool {
  public:
    FramePool(size_t totalSize) {
        base_ = reinterpret_cast<std::byte *>(std::malloc(totalSize));
        if (!base_)
            throw std::bad_alloc();
        end_ = base_ + totalSize;
        top_ = base_;
        // 初始时该位置无有效 Frame
        reinterpret_cast<Frame *>(top_)->graph_ = nullptr;
    }

    ~FramePool() { std::free(base_); }

    Frame *acquire(GraphIR::Graph *graph) {
        EXEC_WHEN_DEBUG([&]() {
            l.in("FramePool")
                .info(
                    "[{}] Acquire request for graph <{}>, top = {}, end = {}",
                    formatAddress(this, true),
                    graph ? graph->name() : "(null)",
                    formatAddress(top_, true),
                    formatAddress(end_, true));
        }());

        // 尝试复用
        Frame *lastFrame = reinterpret_cast<Frame *>(top_);
        if (LIKELY(lastFrame->graph_ == graph)) {
            EXEC_WHEN_DEBUG([&]() {
                // 把 dynamic 区所有 slot 写成 魔数，用于检测脏读
                size_t n = lastFrame->dynamicAreaLayout_->size();
                for (size_t i = 0; i < n; ++i) {
                    lastFrame->dynamicArea_[i] = kDebugUninitializedSlot;
                }
                l.in("FramePool")
                    .info(
                        "[{}] Reusing existing frame of graph <{}> at {}",
                        formatAddress(this, true),
                        graph ? graph->name() : "(null)",
                        formatAddress(lastFrame, true));
            }());

            frameObjects_.push_back(lastFrame);
            top_ = reinterpret_cast<std::byte *>(lastFrame->nextFrame_);
            return lastFrame;
        }

        // 分配新 Frame 并初始化
        FrameMeta *meta = graph->getExtra<FrameMeta, 0>();
        if (meta == nullptr) {
            meta = installFrameMetaInfoForGraph(graph);
            EXEC_WHEN_DEBUG([&]() {
                l.in("FramePool")
                    .info(
                        "[{}] Installed FrameMeta for graph <{}>",
                        formatAddress(this, true),
                        graph->name());
            }());
        }
        size_t frameSize = meta->frameSize;
        if (top_ + frameSize > end_) {
            EXEC_WHEN_DEBUG([&]() {
                l.in("FramePool")
                    .error(
                        "[{}] Out of memory: top = {}, need = {}, end = {}",
                        formatAddress(this, true),
                        formatAddress(top_, true),
                        frameSize,
                        formatAddress(end_, true));
            }());
            throw std::bad_alloc{};
        }
        Frame *frame = new (top_) Frame(graph, meta->staticArea, meta->runtimeAreaLayout);

        EXEC_WHEN_DEBUG([&]() {
            l.in("FramePool")
                .info(
                    "[{}] Allocated new Frame for graph <{}> at {}, size = {}",
                    formatAddress(this, true),
                    graph->name(),
                    formatAddress(frame, true),
                    frameSize);
        }());

        // 更新 top 指针
        top_ += frameSize;

        // 避免无效数据误用
        reinterpret_cast<Frame *>(top_)->graph_ = nullptr;

        frameObjects_.push_back(frame);
        return frame;
    }

    void release(Frame *frame) {
        EXEC_WHEN_DEBUG([&]() {
            l.in("FramePool")
                .info(
                    "[{}] Releasing frame of graph <{}> at {}",
                    formatAddress(this, true),
                    frame->graph_ ? frame->graph_->name() : "(null)",
                    formatAddress(frame, true));
            ASSERT(
                reinterpret_cast<std::byte *>(frame) < top_,
                "Trying to release a frame that is already released.");
            Frame *last = reinterpret_cast<Frame *>(frameObjects_.back());
            ASSERT(
                last == frame,
                std::format(
                    "Trying to release a frame that is not on top, top frame of graph <{}> is at "
                    "{}.",
                    last->graph_->name(),
                    formatAddress(last, true)));
        }());

        frame->nextFrame_ = reinterpret_cast<Frame *>(top_);
        top_              = reinterpret_cast<std::byte *>(frame);
        frameObjects_.pop_back();

        EXEC_WHEN_DEBUG([&]() {
            l.in("FramePool")
                .info(
                    "[{}] Frame released. New top = {}",
                    formatAddress(this, true),
                    formatAddress(top_, true));
        }());
    }

    std::vector<Object *> *frameObjects() { return &frameObjects_; }

  private:
    std::byte *base_;
    std::byte *top_;
    std::byte *end_;
    std::vector<Object *> frameObjects_;
};
