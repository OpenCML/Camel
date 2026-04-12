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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/rtdata.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

namespace camel::core::context {

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;
namespace mm     = camel::core::mm;

class FramePool;
class TaskflowFramePool;
class FrameView;

class Frame : public rtdata::Object {
  public:
    static consteval size_t runtimeGraphOffset() { return 0; }
    static consteval size_t nextOffset() { return sizeof(void *); }
    static consteval size_t staticAreaOffset() { return sizeof(void *) * 2; }
    static consteval size_t dynamicAreaTypeOffset() { return sizeof(void *) * 3; }
    static consteval size_t dynamicAreaOffset() { return sizeof(void *) * 4; }

    // Runtime graph identity is the primary execution carrier.
    camel::runtime::GCGraph *graph() { return runtimeGraph_; }
    const camel::runtime::GCGraph *graph() const { return runtimeGraph_; }
    camel::runtime::GCGraph *runtimeGraph() { return runtimeGraph_; }
    const camel::runtime::GCGraph *runtimeGraph() const { return runtimeGraph_; }
    bool hasRuntimeGraph() const { return runtimeGraph_ != nullptr; }
    const type::TupleType *runtimeDataLayout() const {
        ASSERT(runtimeGraph_ != nullptr, "Runtime frame has no runtime graph for data layout.");
        return runtimeGraph_->runtimeDataType();
    }
    const type::TupleType *staticDataLayout() const {
        ASSERT(runtimeGraph_ != nullptr, "Runtime frame has no runtime graph for static layout.");
        return runtimeGraph_->staticDataType();
    }
    const std::string &graphName() const {
        ASSERT(runtimeGraph_ != nullptr, "Runtime frame has no runtime graph name.");
        return runtimeGraph_->name();
    }

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
            return staticDataLayout()->codeAt(idx);
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
            type::Type *res = runtimeDataLayout()->typeAt(idx);
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
            type::Type *res = staticDataLayout()->typeAt(idx);
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
            CAMEL_LOG_INFO_S(
                "Frame",
                "[{}] Getting data of graph <{}> at index {} ({}): {}",
                mm::formatAddress(const_cast<Frame *>(this), true),
                graphName(),
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
            CAMEL_LOG_INFO_S(
                "Frame",
                "[{}] Setting data of graph <{}> at index {} ({}): {}",
                mm::formatAddress(this, true),
                graphName(),
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
        return false; // Frames are execution carriers and have no value semantics.
    }

    virtual rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override {
        return nullptr; // Frames are pool-managed and intentionally non-cloneable.
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
        os << "frame <" << (runtimeGraph_ ? graphName() : "(null)") << "> at "
           << mm::formatAddress(this, true) << ":\n";
        for (size_t i = 1; i < dynamicAreaType_->size(); ++i) {
            slot_t s      = dynamicArea_[i];
            type::Type *t = runtimeDataLayout()->typeAt(i);
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

    // Only frame pools construct frames directly.
    Frame(
        camel::runtime::GCGraph *runtimeGraph, ::Tuple *staticArea,
        const type::TupleType *dynamicAreaType)
        : runtimeGraph_(runtimeGraph), next_(nullptr), staticArea_(staticArea),
          dynamicAreaType_(dynamicAreaType) {
        // Do not eagerly initialize the dynamic area here. FastVM relies on
        // reusing freshly released frame memory on the hot path, and forcing
        // constructor-time initialization would add avoidable churn. Debug-mode
        // uninitialized-slot diagnostics therefore happen at access sites.
    }

    camel::runtime::GCGraph *runtimeGraph_;
    Frame *next_;
    ::Tuple *staticArea_; // Borrowed static area owned by the graph carrier.
    const type::TupleType *dynamicAreaType_;
    slot_t dynamicArea_[]; // Flexible tail storage for dynamic slots.
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
        } else { // Static area
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
        } else { // Static area
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

static_assert(sizeof(camel::runtime::GCGraph *) == sizeof(void *));
static_assert(sizeof(Frame *) == sizeof(void *));
static_assert(sizeof(::Tuple *) == sizeof(void *));
static_assert(sizeof(const type::TupleType *) == sizeof(void *));
static_assert(Frame::runtimeGraphOffset() == 0);
static_assert(Frame::nextOffset() == sizeof(void *));
static_assert(Frame::dynamicAreaOffset() == sizeof(void *) * 4);

class FramePool {
  public:
    FramePool(size_t totalSize) {
        base_ = reinterpret_cast<std::byte *>(std::malloc(totalSize));
        if (!base_)
            throw std::bad_alloc();
        end_ = base_ + totalSize;
        top_ = base_;
        // Initialize the sentinel frame slot.
        reinterpret_cast<Frame *>(top_)->runtimeGraph_ = nullptr;
    }

    ~FramePool() { std::free(base_); }

    inline void _resetTop() { reinterpret_cast<Frame *>(top_)->runtimeGraph_ = nullptr; }

    inline Frame *_acquire(camel::runtime::GCGraph *graph) {
        EXEC_WHEN_DEBUG({
            CAMEL_LOG_INFO_S(
                "FramePool",
                "[{}] Acquire request for runtime graph <{}>, top = {}, end = {}",
                mm::formatAddress(this, true),
                graph ? graph->name() : "(null)",
                mm::formatAddress(top_, true),
                mm::formatAddress(end_, true));
        });

        Frame *lastFrame = reinterpret_cast<Frame *>(top_);
        if (LIKELY(lastFrame->runtimeGraph_ == graph)) {
            EXEC_WHEN_DEBUG({
                CAMEL_LOG_INFO_S(
                    "FramePool",
                    "[{}] Reusing existing frame of runtime graph <{}> at {}",
                    mm::formatAddress(this, true),
                    graph ? graph->name() : "(null)",
                    mm::formatAddress(lastFrame, true));
                frames_.push_back(lastFrame);
            });

            top_ = reinterpret_cast<std::byte *>(lastFrame->next_);
            return lastFrame;
        }

        ASSERT(
            graph->hasFrameLayout(),
            std::format("Runtime graph '{}' has no finalized frame layout.", graph->name()));
        size_t frameSize = graph->frameSize();
        if (top_ + frameSize > end_) {
            CAMEL_LOG_FATAL_S(
                "FramePool",
                "[{}] Out of memory for runtime graph <{}>: top = {}, need = {}, end = {}, "
                "runtimeDataSize = {}",
                mm::formatAddress(this, true),
                graph ? graph->name() : "(null)",
                mm::formatAddress(top_, true),
                frameSize,
                mm::formatAddress(end_, true),
                graph && graph->runtimeDataType() ? graph->runtimeDataType()->size() : 0);
            throw std::bad_alloc{};
        }
        Frame *frame = new (top_) Frame(graph, graph->staticArea(), graph->runtimeDataType());

        EXEC_WHEN_DEBUG({
            CAMEL_LOG_INFO_S(
                "FramePool",
                "[{}] Allocated new Frame for runtime graph <{}> at {}, size = {}",
                mm::formatAddress(this, true),
                graph->name(),
                mm::formatAddress(frame, true),
                frameSize);
        });

        top_ += frameSize;

        EXEC_WHEN_DEBUG({ frames_.push_back(frame); });

        return frame;
    }

    inline Frame *acquire(camel::runtime::GCGraph *graph) {
        Frame *frame = _acquire(graph);
        _resetTop();
        return frame;
    }

    inline void release(Frame *frame) {
        EXEC_WHEN_DEBUG({
            CAMEL_LOG_INFO_S(
                "FramePool",
                "[{}] Releasing frame of graph <{}> at {}",
                mm::formatAddress(this, true),
                frame->runtimeGraph_ ? frame->runtimeGraph_->name() : "(null)",
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
                    last->runtimeGraph_ ? last->runtimeGraph_->name() : "(null)",
                    mm::formatAddress(last, true)));
        });

        frame->next_ = reinterpret_cast<Frame *>(top_);
        top_         = reinterpret_cast<std::byte *>(frame);

        EXEC_WHEN_DEBUG({
            frames_.pop_back();
            CAMEL_LOG_INFO_S(
                "FramePool",
                "[{}] Frame released. New top = {}",
                mm::formatAddress(this, true),
                mm::formatAddress(top_, true));
        });
    }

    void *topAddr() { return &top_; }

    inline bool isActive(Frame *frame) const {
        if (!frame)
            return false;
        if (reinterpret_cast<const std::byte *>(frame) >= top_)
            return false;
        return true;
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
 * ArgsView backed by slot_t*. Used by JIT call sites to build OPER arguments on the C++ stack
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
