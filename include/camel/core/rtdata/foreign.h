// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"
#include "camel/core/mm/alloc/allocator.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Foreign resources are represented by ordinary GC-managed wrapper objects. The wrapper
// participates in tracing and relocation; the native resource behind the control block follows
// explicit dispose/finalize and reference-counted ownership rules.

namespace camel::core::rtdata {

struct ForeignResourceStats {
    uint64_t createdControlBlocks = 0;
    /// The blocks whose kind is the core's reference handle (`RefType::resourceHandleName()`): the
    /// boxes a count reclaims. `createdControlBlocks` counts every foreign block, a python object
    /// and a file handle included, so it cannot be compared against a plan's count of values.
    uint64_t createdCountedBlocks  = 0;
    uint64_t disposedResources     = 0;
    uint64_t finalizedWrappers     = 0;
    uint64_t releasedControlBlocks = 0;
    uint64_t liveControlBlocks     = 0;
    uint64_t createdRootedHandles  = 0;
    uint64_t createdPinnedHandles  = 0;
    uint64_t activeRootedHandles   = 0;
    uint64_t activePinnedHandles   = 0;
};

struct ForeignResourceDescriptor {
    using Deleter = std::function<void(void *)>;
    using Cloner  = std::function<void *(void *)>;
    using Tracer =
        std::function<void(void *, const Object::RefRelocator &, const camel::core::type::Type *)>;
    using MoveObserver = std::function<void(void *)>;

    std::string kind;
    Deleter deleter;
    Cloner clone;
    Tracer trace;
    MoveObserver onMoved;
    bool containsCamelReferences = false;
    bool finalizable             = true;
    bool movable                 = true;
    bool pinned                  = false;
    /// The module that owns the type declares that a count reclaims its resources:
    /// `retainCountNow`/`giveBackCountNow` (below) apply to a block only when its descriptor says
    /// so, and the anchor counts only these blocks. Declared by the type's owner and by nothing
    /// else -- the kind string is a name for diagnostics and is not an identity the count reads
    /// (author review 2026-11-16, item 7: the count recognized `"state.ref"` by comparing `kind`,
    /// which made the module's own name a fact of the runtime and would have counted any other
    /// module's handle the day it was wrapped the same way).
    bool counted = false;
};

class ForeignResourceControlBlock final {
  public:
    ForeignResourceControlBlock(ForeignResourceDescriptor descriptor, void *resource);

    ForeignResourceControlBlock(const ForeignResourceControlBlock &)            = delete;
    ForeignResourceControlBlock &operator=(const ForeignResourceControlBlock &) = delete;

    void retain() noexcept;
    /// The plan's count: another holder of the value. Distinct from `retain`, which is another
    /// wrapper of the handle appearing. The collector may still be holding a wrapper after the
    /// count says no holder is left, and that is the transition the author ruled on 2026-11-15:
    /// the count decides when the **resource** goes, the collector still owns the handle value.
    void retainHolder() noexcept;
    void releaseReference() noexcept;
    void releaseFromWrapperFinalizer() noexcept;
    /// Gives one reference back and says whether it was the last, so a caller that holds a value
    /// learns the count reached zero at that moment (design principles 10.5) instead of learning it
    /// from a later collection. `releaseReference` is the same call without the answer.
    ///
    /// Zero disposes the resource and leaves the block's memory alone: the wrappers that name the
    /// block are released by the collector, and the last of them frees it. Freeing the block here
    /// would need every wrapper to have stopped naming it first, which is only true once the handle
    /// value itself stops living on the collected heap.
    bool releaseCountNow() noexcept;
    void dispose() noexcept;

    void *resource();
    const void *resource() const;
    bool disposed() const noexcept { return disposed_.load(std::memory_order_acquire); }
    uint64_t debugId() const noexcept { return debugId_; }
    size_t refCount() const noexcept { return refCount_.load(std::memory_order_acquire); }
    /// The plan's count: how many holders the value has. Zero means the resource is gone.
    size_t holderCount() const noexcept { return holderCount_.load(std::memory_order_acquire); }
    bool countedOut() const noexcept { return countedOut_.load(std::memory_order_acquire); }
    std::string_view kind() const noexcept { return descriptor_.kind; }
    const ForeignResourceDescriptor &descriptor() const noexcept { return descriptor_; }

    ForeignResourceControlBlock *cloneOrRetain();

    void trace(const Object::RefRelocator &relocate, const camel::core::type::Type *typeInfo);
    void onWrapperMoved() noexcept;

  private:
    ~ForeignResourceControlBlock() = default;

    bool release() noexcept;

    ForeignResourceDescriptor descriptor_;
    void *resource_   = nullptr;
    uint64_t debugId_ = 0;
    std::atomic<size_t> refCount_{1};
    /// The plan's holders: what the +1 and the -1 entries count. It starts at one, because making
    /// the value made a holder.
    std::atomic<size_t> holderCount_{1};
    std::atomic<bool> countedOut_{false};
    std::atomic<bool> disposed_{false};
    mutable std::mutex mutex_;
};

class ForeignResourceObject final : public Object {
  public:
    ForeignResourceObject(const ForeignResourceObject &)            = delete;
    ForeignResourceObject &operator=(const ForeignResourceObject &) = delete;

    static ForeignResourceObject *create(
        camel::core::mm::IAllocator &allocator, ForeignResourceDescriptor descriptor,
        void *resource);

    static ForeignResourceObject *createWithControlBlock(
        camel::core::mm::IAllocator &allocator, ForeignResourceControlBlock *control);

    ForeignResourceControlBlock *control() noexcept { return control_; }
    const ForeignResourceControlBlock *control() const noexcept { return control_; }

    bool disposed() const noexcept { return !control_ || control_->disposed(); }
    void dispose() noexcept;
    void *resource();
    const void *resource() const;
    std::string_view kind() const noexcept;
    uint64_t debugId() const noexcept;

    template <typename T> T *resourceAs() { return static_cast<T *>(resource()); }
    template <typename T> const T *resourceAs() const { return static_cast<const T *>(resource()); }

    bool equals(const Object *other, const camel::core::type::Type *type, bool deep) const override;
    Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override;
    void finalize() noexcept override;
    void updateRefs(const RefRelocator &relocate, const camel::core::type::Type *type) override;

  private:
    explicit ForeignResourceObject(ForeignResourceControlBlock *control) : control_(control) {}

    ForeignResourceControlBlock *control_ = nullptr;
};

/// ---- The count a counted value is held by ------------------------------------------------
//
// Design principles 10.5: an immutable value is freed by counting alone, the count is the number of
// its holders, and the count comes from one last-use analysis on the execution plan that every back
// end follows. These two functions are the whole runtime surface for that count -- the author's
// ruling of 2026-11-15 is that the compiler emits the duplicate and the drop and the runtime
// provides nothing else (docs/handover/reviews/
// 2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md) -- and where they are
// called is the plan's business (camel::runtime::ExecPlan::refGiveBacksAt names the positions and
// the references).
//
// A `Ref` handle is an ordinary traced wrapper whose control block is the box, and the slot's type
// is what says a slot holds one (`camel::core::type::RefType`, whose code is the `state::Ref` other
// type -- not `TypeCode::Ref`, which is the compile-time reference a `RefData` carries and which no
// value of a graph has), so the filter in front of the two calls below is a slot-type comparison
// and not a cast per value. `retainCountNow` takes one reference for a new holder and answers false
// for a value that is not a counted block (every other foreign resource keeps its present lifetime:
// released by its wrapper's finalizer). `giveBackCountNow` gives one back and answers true when the
// count reached zero, so the caller is the one that knows the box was released at that moment --
// which is the point of the count, and the reason the answer is returned rather than inferred from
// a collection later.
//
// Who calls them: one entry point shared by all four back ends,
// `camel::runtime::applyRefEntriesForNode`, which reads the plan's rows for the position the node
// has just run at (`camel/runtime/exec_plan.h`); its own only caller is
// `camel::execute::callOperator` (`camel/execute/operator_call.h`), the one place a back end calls
// an operator. Nothing else calls them: a count that is applied in more than one place is a count
// whose balance no single table can be read against.
bool retainCountNow(Object *value) noexcept;
bool giveBackCountNow(Object *value) noexcept;

/// ---- The anchor: a box that was made has to be a value the plan counts -----------------------
//
// The balance the plan prints is an identity between columns one walk produced, so a reference
// missed on both sides at once leaves it balanced. The anchor asks the question the walk cannot:
// at the moment a box is made, does the plan name the node that is running as one that makes a
// value? An executor answers it with the pair below, once per node whose execution made boxes --
// `execPlan().refProducedAt` is the plan's answer, and it is read by the executor because that is
// the only place that has both the plan and the node.
//
// A row of the plan can be made many times (a node in a loop makes a box per round), so the counts
// are not expected to line up one to one. What must hold is `missing == 0`: every creation is
// explained. The third reading is the other direction -- a row that no creation ever exercised,
// which would be a producing value the table promises and the program never makes.
//
// The two sides are measured independently on purpose. `checks` is what the hook asked about, and
// `creations` is what the allocator made; a hook that quietly stopped being called, or that a back
// end never reached, reads as `checks < creations` instead of agreeing with itself.
struct CountedCreationAnchor {
    /// The boxes that were made, counted by the allocator that made them (`createdCountedBlocks`),
    /// and not by the hook that asks about them: the anchor is worth something only while its two
    /// sides are measured independently, so this number is read from the allocator every time the
    /// anchor is reported (`countedCreationAnchor`, `countedBlocksMade`).
    uint64_t creations = 0;
    /// The boxes the anchor asked about: one question per box made, asked by the executor at the
    /// site that made it. `checks < creations` is a site the hook never reached, and
    /// `checks > creations` is one it reached twice.
    uint64_t checks  = 0;
    uint64_t found   = 0;
    uint64_t missing = 0;
    /// How many of the plan's producing rows were exercised at least once, summed over the graphs
    /// that ran, and how many rows the plans had. `unseen` is the second minus the first.
    uint64_t rowsSeen = 0;
    uint64_t rows     = 0;
};
/// The answer for `boxes` boxes that one node execution made, reported once per creation: whether
/// the plan names the node that is running as one that produces a value. Reports only -- the caller
/// has already run the node, and nothing here can refuse, undo or reorder anything.
void recordCountedCreation(bool namedByThePlan, uint64_t boxes = 1) noexcept;
/// The boxes a count reclaims that exist so far. The creation side of the anchor: an executor site
/// reads it before and after the operator it runs and hands the difference to the anchor.
uint64_t countedBlocksMade() noexcept;
/// The same for the boxes this thread made. What an executor site brackets an operator call with
/// on a scheduler that runs nodes on several threads, where the program-wide difference would also
/// hold a neighbour's box and read a node as having made more than it did.
uint64_t countedBlocksMadeOnThisThread() noexcept;
/// How many boxes the anchor has already been told about on this thread. A site subtracts the
/// growth of this from the growth of the count above: an operator that runs a graph (map, foreach,
/// a loop body) is bracketed too, and its callee's boxes are reported by the callee's own sites,
/// which are inside that bracket.
uint64_t countedCreationsReportedOnThisThread() noexcept;
/// The plan's rows: one call per graph that a run executes, with its count of producing positions.
void recordPlanRows(uint64_t rows, uint64_t rowsSeen) noexcept;
CountedCreationAnchor countedCreationAnchor() noexcept;
void resetCountedCreationAnchorForTests() noexcept;

ForeignResourceStats foreignResourceStats();
/// Releases every external resource that is still held, newest first, and returns how many were
/// released. For the end of a run (design principles 3.9: before a program ends, however it
/// ends, its external resources are released): the collector releases a resource when it finds
/// its wrapper dead, which for the wrappers alive at the end would be never. The wrappers stay
/// valid objects; using one afterwards is the "disposed" error. Must not run while the program
/// is still running.
/// DESIGN-QUESTION(attempt-finalizers): a resource dropped inside a fault-isolation boundary is
/// released by the next collection that finds its wrapper dead, at the latest by this call.
size_t disposeAllForeignResources() noexcept;
void resetForeignResourceStatsForTests();
/// Releases the resources of the wrappers the collector found dead. The collector only
/// queues them (ForeignResourceObject::finalize runs inside the collection, with the allocator
/// locked); the allocator runs the queue at the end of the safepoint, after it has unlocked, on
/// the thread that reached the safepoint (mm/deferred.h; data blocks use the same
/// queue). A deleter therefore runs with no runtime lock held: it may take locks of its own
/// (the Python GIL) without ordering them against the allocator's.

void noteForeignRootedHandleCreated() noexcept;
void noteForeignRootedHandleDestroyed() noexcept;
void noteForeignPinnedHandleCreated() noexcept;
void noteForeignPinnedHandleDestroyed() noexcept;

} // namespace camel::core::rtdata
