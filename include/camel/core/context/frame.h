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
 * Updated: Mar. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/compile/gir.h"
#include "camel/core/rtdata.h"
#include "camel/utils/log.h"

namespace camel::core::context {

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;
namespace mm     = camel::core::mm;

struct FrameMeta {
    size_t frameSize;
    const type::TupleType *runtimeDataType;
    ::Tuple *staticArea;
};

FrameMeta *installFrameMetaInfoForGraph(GIR::Graph *graph);

class FramePool;
class TaskflowFramePool;
class FrameView;

class Frame : public rtdata::Object {
  public:
    GIR::Graph *graph() { return graph_; }
    const GIR::Graph *graph() const { return graph_; }

    type::TypeCode codeAt(GIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicAreaType_->size(),
                std::format(
                    "[{}] Invalid argument index, idx = {}, size = {}",
                    mm::formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    dynamicAreaType_->size()));
            return dynamicAreaType_->codeAt(idx);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "[{}] Invalid static data index, idx = {}, size = {}",
                    mm::formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    staticArea_->size()));
            return graph_->staticDataType()->codeAt(idx);
        }
    }

    template <typename T> T *typeAt(GIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            ASSERT(
                idx < dynamicAreaType_->size(),
                std::format(
                    "[{}] Invalid argument index, idx = {}, size = {}",
                    mm::formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    dynamicAreaType_->size()));
            type::Type *res = graph_->runtimeDataType()->typeAt(idx);
            ASSERT(res != nullptr, std::format("Type at index {} is null.", idx));
            return tt::as_ptr<T>(res);
        } else {
            size_t idx = static_cast<size_t>(-index);
            ASSERT(
                idx < staticArea_->size(),
                std::format(
                    "[{}] Invalid static data index, idx = {}, size = {}",
                    mm::formatAddress(const_cast<Frame *>(this), true),
                    idx,
                    staticArea_->size()));
            type::Type *res = graph_->staticDataType()->typeAt(idx);
            ASSERT(res != nullptr, std::format("Type at index {} is null.", idx));
            return tt::as_ptr<T>(res);
        }
    }

    template <typename T> T get(GIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        T res;
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < dynamicAreaType_->size(),
                    std::format(
                        "[{}] Invalid argument index, idx = {}, size = {}",
                        mm::formatAddress(const_cast<Frame *>(this), true),
                        idx,
                        dynamicAreaType_->size()));
                ASSERT(
                    dynamicArea_[idx] != mm::kDebugUninitializedSlot,
                    std::format(
                        "[{}] Accessing uninitialized slot: idx = {}",
                        mm::formatAddress(const_cast<Frame *>(this), true),
                        index));
            });
            res = rtdata::fromSlot<T>(dynamicArea_[idx]);
        } else {
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "[{}] Invalid static data index, idx = {}, size = {}",
                        mm::formatAddress(const_cast<Frame *>(this), true),
                        idx,
                        staticArea_->size()));
            });
            res = staticArea_->get<T>(idx);
        }
        EXEC_WHEN_DEBUG({
            std::ostringstream oss;
            rtdata::printSlot(oss, rtdata::toSlot(res), typeAt<type::Type>(index));
            GetDefaultLogger().in("Frame").info(
                "[{}] Getting data of graph <{}> at index {} ({}): {}",
                mm::formatAddress(const_cast<Frame *>(this), true),
                graph_->name(),
                index,
                typeCodeToString(codeAt(index)),
                oss.str());
        });
        return res;
    }

    template <typename T> void set(GIR::data_idx_t index, T value) {
        ASSERT(index != 0, "Data index is invalid.");
        EXEC_WHEN_DEBUG({
            std::ostringstream oss;
            rtdata::printSlot(oss, rtdata::toSlot(value), typeAt<type::Type>(index));
            GetDefaultLogger().in("Frame").info(
                "[{}] Setting data of graph <{}> at index {} ({}): {}",
                mm::formatAddress(this, true),
                graph_->name(),
                index,
                typeCodeToString(codeAt(index)),
                oss.str());
        });
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < dynamicAreaType_->size(),
                    std::format(
                        "Invalid argument index, idx = {}, size = {}",
                        idx,
                        dynamicAreaType_->size()));
            });
            dynamicArea_[idx] = rtdata::toSlot(value);
        } else {
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "Invalid static data index, idx = {}, size = {}",
                        idx,
                        staticArea_->size()));
            });
            staticArea_->set<T>(idx, value);
        }
    }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        return false; // Frame 没有实际意义的比较
    }

    virtual rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override {
        return nullptr; // 不支持克隆
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        (void)type;
        os << "Frame(dynamicSize=" << dynamicAreaType_->size()
           << ", staticSize=" << staticArea_->size() << ")";
    }

    virtual void onMoved() override {}

    slot_t *slotBase() { return dynamicArea_; }
    const slot_t *slotBase() const { return dynamicArea_; }

    void printSlotsTo(std::ostream &os) const {
        os << "frame <" << (graph_ ? graph_->name() : "(null)") << "> at "
           << mm::formatAddress(this, true) << ":\n";
        for (size_t i = 1; i < dynamicAreaType_->size(); ++i) {
            slot_t s      = dynamicArea_[i];
            type::Type *t = graph_->runtimeDataType()->typeAt(i);
            os << "  [" << i << "] ";
            rtdata::printSlotSafe(os, s, t);
            os << "\n";
        }
    }

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        (void)type;
        auto codes              = dynamicAreaType_->codes();
        rtdata::Object **refArr = reinterpret_cast<rtdata::Object **>(dynamicArea_);
        for (size_t i = 0; i < dynamicAreaType_->size(); ++i) {
            if (type::isGCTraced(codes[i])) {
                if (rtdata::Object *&ref = refArr[i]) {
                    ref = relocate(ref);
                }
            }
        }
    }

  private:
    friend class FramePool;
    friend class TaskflowFramePool;
    friend class FrameView;

    // 这个构造函数由栈帧池调用
    Frame(GIR::Graph *graph, ::Tuple *staticArea, const type::TupleType *dynamicAreaType)
        : graph_(graph), staticArea_(staticArea), dynamicAreaType_(dynamicAreaType) {
        // 注意，这里不能在构造函数中初始化 dynamicArea_
        // FastVM 的优化依赖于复用刚刚释放的栈帧数据
        // 所以这里要尽量不去动 dynamicArea_，即便在 DEBUG 模式下
        // 这会给 DEBUG 下脏读检测带来一定难度，不过总体来说还是利大于弊的
    }

    GIR::Graph *graph_;
    Frame *next_;
    ::Tuple *staticArea_; // 外部提供的静态区
    const type::TupleType *dynamicAreaType_;
    slot_t dynamicArea_[]; // 紧跟对象后存放动态区
};

class FrameView {
  public:
    FrameView(const Frame *frame)
        : staticArea_(frame->staticArea_), dynamicArea_(const_cast<slot_t *>(frame->dynamicArea_)) {
    }

    template <typename T> T get(GIR::data_idx_t index) const {
        ASSERT(index != 0, "Data index is invalid.");
        T res;
        if (index > 0) {
            size_t idx = static_cast<size_t>(index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    dynamicArea_[idx] != mm::kDebugUninitializedSlot,
                    std::format("Accessing uninitialized slot: idx = {}", index));
            });
            res = rtdata::fromSlot<T>(dynamicArea_[idx]);
        } else { // 静态区：index < 0
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "Invalid static data index, idx = {}, size = {}",
                        idx,
                        staticArea_->size()));
            });
            res = staticArea_->get<T>(idx);
        }
        return res;
    }

    template <typename T> void set(GIR::data_idx_t index, T value) {
        ASSERT(index != 0, "Data index is invalid.");
        if (index > 0) {
            size_t idx        = static_cast<size_t>(index);
            dynamicArea_[idx] = rtdata::toSlot(value);
        } else { // 静态区
            size_t idx = static_cast<size_t>(-index);
            EXEC_WHEN_DEBUG({
                ASSERT(
                    idx < staticArea_->size(),
                    std::format(
                        "Invalid static data index, idx = {}, size = {}",
                        idx,
                        staticArea_->size()));
            });
            staticArea_->set<T>(idx, value);
        }
    }

  private:
    ::Tuple *staticArea_;
    slot_t *dynamicArea_;
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

    inline Frame *_acquire(GIR::Graph *graph) {
        EXEC_WHEN_DEBUG({
            GetDefaultLogger()
                .in("FramePool")
                .info(
                    "[{}] Acquire request for graph <{}>, top = {}, end = {}",
                    mm::formatAddress(this, true),
                    graph ? graph->name() : "(null)",
                    mm::formatAddress(top_, true),
                    mm::formatAddress(end_, true));
        });

        // 尝试复用
        Frame *lastFrame = reinterpret_cast<Frame *>(top_);
        if (lastFrame->graph_ == graph) {
            EXEC_WHEN_DEBUG({
                GetDefaultLogger()
                    .in("FramePool")
                    .info(
                        "[{}] Reusing existing frame of graph <{}> at {}",
                        mm::formatAddress(this, true),
                        graph ? graph->name() : "(null)",
                        mm::formatAddress(lastFrame, true));
                frames_.push_back(lastFrame);
            });

            top_ = reinterpret_cast<std::byte *>(lastFrame->next_);
            return lastFrame;
        }

        // 分配新 Frame 并初始化
        FrameMeta *meta = graph->getExtra<FrameMeta, 0>();
        if (meta == nullptr) {
            meta = installFrameMetaInfoForGraph(graph);
            EXEC_WHEN_DEBUG({
                GetDefaultLogger()
                    .in("FramePool")
                    .info(
                        "[{}] Installed FrameMeta for graph <{}>",
                        mm::formatAddress(this, true),
                        graph->name());
            });
        }
        size_t frameSize = meta->frameSize;
        if (top_ + frameSize > end_) {
            EXEC_WHEN_DEBUG({
                GetDefaultLogger()
                    .in("FramePool")
                    .error(
                        "[{}] Out of memory: top = {}, need = {}, end = {}",
                        mm::formatAddress(this, true),
                        mm::formatAddress(top_, true),
                        frameSize,
                        mm::formatAddress(end_, true));
            });
            throw std::bad_alloc{};
        }
        Frame *frame = new (top_) Frame(graph, meta->staticArea, meta->runtimeDataType);

        EXEC_WHEN_DEBUG({
            GetDefaultLogger()
                .in("FramePool")
                .info(
                    "[{}] Allocated new Frame for graph <{}> at {}, size = {}",
                    mm::formatAddress(this, true),
                    graph->name(),
                    mm::formatAddress(frame, true),
                    frameSize);
        });

        top_ += frameSize;

        EXEC_WHEN_DEBUG({ frames_.push_back(frame); });

        return frame;
    }

    inline void _resetTop() { reinterpret_cast<Frame *>(top_)->graph_ = nullptr; }

    inline Frame *acquire(GIR::Graph *graph) {
        Frame *frame = _acquire(graph);
        _resetTop();
        return frame;
    }

    inline void release(Frame *frame) {
        EXEC_WHEN_DEBUG({
            GetDefaultLogger()
                .in("FramePool")
                .info(
                    "[{}] Releasing frame of graph <{}> at {}",
                    mm::formatAddress(this, true),
                    frame->graph_ ? frame->graph_->name() : "(null)",
                    mm::formatAddress(frame, true));
            ASSERT(
                reinterpret_cast<std::byte *>(frame) < top_,
                "Trying to release a frame that is already released.");
            Frame *last = frames_.back();
            ASSERT(
                last == frame,
                std::format(
                    "Trying to release a frame that is not on top, top frame of graph <{}> is at "
                    "{}.",
                    last->graph_->name(),
                    mm::formatAddress(last, true)));
        });

        frame->next_ = reinterpret_cast<Frame *>(top_);
        top_         = reinterpret_cast<std::byte *>(frame);

        EXEC_WHEN_DEBUG({
            frames_.pop_back();
            GetDefaultLogger()
                .in("FramePool")
                .info(
                    "[{}] Frame released. New top = {}",
                    mm::formatAddress(this, true),
                    mm::formatAddress(top_, true));
        });
    }

    void foreach (const std::function<void(Frame *)> &fn) const {
        for (Frame *frame = reinterpret_cast<Frame *>(base_); frame != nullptr;
             frame        = frame->next_) {
            fn(frame);
        }
    }

  private:
    std::byte *base_;
    std::byte *top_;
    std::byte *end_;
#ifndef NDEBUG
    std::vector<Frame *> frames_;
#endif
};

class FrameArgsView : public ArgsView {
  private:
    Frame &frame_;
    const data_arr_t &indices_;

  public:
    FrameArgsView(Frame &frame, const data_arr_t &indices) : frame_(frame), indices_(indices) {}

    size_t size() const override { return indices_.size(); }

    slot_t slot(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        return frame_.get<slot_t>(dataIdx);
    }

    void setSlot(size_t index, slot_t value) override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        frame_.set(dataIdx, value);
    }

    type::TypeCode code(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        return frame_.codeAt(dataIdx);
    }

    type::Type *type(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        return frame_.typeAt<type::Type>(dataIdx);
    }
};

/**
 * 基于 slot_t* 的 ArgsView，用于 JIT 调用 OPER 时在 C++ 栈上构造，无需 Frame
 */
class SlotArgsView : public ArgsView {
  private:
    slot_t *slots_;
    ::Tuple *staticArea_;
    const type::TupleType *runtimeDataType_;
    const type::TupleType *staticDataType_;
    const data_arr_t &indices_;

  public:
    SlotArgsView(
        slot_t *slots, ::Tuple *staticArea, const type::TupleType *runtimeDataType,
        const type::TupleType *staticDataType, const data_arr_t &indices)
        : slots_(slots), staticArea_(staticArea), runtimeDataType_(runtimeDataType),
          staticDataType_(staticDataType), indices_(indices) {}

    size_t size() const override { return indices_.size(); }

    slot_t slot(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        if (dataIdx > 0)
            return slots_[dataIdx];
        return staticArea_->get<slot_t>(static_cast<size_t>(-dataIdx));
    }

    void setSlot(size_t index, slot_t value) override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        if (dataIdx > 0)
            slots_[dataIdx] = value;
        else
            staticArea_->set<slot_t>(static_cast<size_t>(-dataIdx), value);
    }

    type::TypeCode code(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        if (dataIdx > 0)
            return runtimeDataType_->codeAt(static_cast<size_t>(dataIdx));
        return staticDataType_->codeAt(static_cast<size_t>(-dataIdx));
    }

    type::Type *type(size_t index) const override {
        ASSERT(index < indices_.size(), "ArgsView index out of range");
        GIR::data_idx_t dataIdx = indices_[index];
        if (dataIdx > 0)
            return runtimeDataType_->typeAt(static_cast<size_t>(dataIdx));
        return staticDataType_->typeAt(static_cast<size_t>(-dataIdx));
    }
};

} // namespace camel::core::context
