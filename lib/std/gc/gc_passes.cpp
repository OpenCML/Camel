// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The core's allocator passes (`std::gc::*`). Each one reads one fact about the collector of the
 * running program and hands the program on unchanged: `gc.verify` walks the heap, `gc.minor` and
 * `gc.major` collect, `gc.snapshot`, `gc.config`, `gc.summary` and `gc.blocks` print what the
 * profiler knows, and the self-testing ones run a check the allocator carries for the tests. They
 * are named by their declarations in `stdlib/std.cml`; this file is their implementation.
 */

#include "camel/execute/pass/opt.h"
#include "camel/execute/pass/registration.h"

#include "camel/core/array/block_selftest.h"
#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/foreign_handle.h"
#include "camel/core/mm/profiler.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/view_selftest.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <format>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace camel::core::context;

namespace {

std::string
formatGcIssue(const camel::core::mm::GenerationalAllocatorWithGC::HeapVerificationIssue &issue) {
    std::ostringstream os;
    os << issue.message;
    if (!issue.path.empty())
        os << " | path=" << issue.path;
    if (!issue.owner.empty())
        os << " | owner=" << issue.owner;
    if (!issue.slotType.empty())
        os << " | slotType=" << issue.slotType;
    if (!issue.region.empty())
        os << " | region=" << issue.region;
    if (issue.object != 0)
        os << " | object=0x" << std::hex << issue.object << std::dec;
    if (issue.target != 0)
        os << " | target=0x" << std::hex << issue.target << std::dec;
    return os.str();
}

class GcSnapshotPass final : public GraphIRPass {
  public:
    explicit GcSnapshotPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::snapshotToJson() << '\n';
        return nullptr;
    }
};

class GcVerifyPass final : public GraphIRPass {
  public:
    explicit GcVerifyPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        auto issues = camel::core::mm::autoSpace().verifyHeap();
        if (!issues.empty()) {
            constexpr size_t maxIssues = 10;
            std::ostringstream details;
            const size_t shown = std::min(maxIssues, issues.size());
            for (size_t i = 0; i < shown; ++i) {
                details << "\n  [" << i << "] " << formatGcIssue(issues[i]);
            }
            if (issues.size() > shown) {
                details << "\n  ... " << (issues.size() - shown) << " more issue(s)";
            }
            throw std::runtime_error(
                std::format(
                    "GC heap verification failed with {} issue(s):{}",
                    issues.size(),
                    details.str()));
        }
        os << "{\"ok\":true,\"kind\":\"gc.verify\"}\n";
        return program;
    }
};

class GcMinorPass final : public GraphIRPass {
  public:
    explicit GcMinorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().minorGC();
        os << "{\"ok\":true,\"kind\":\"gc.minor\"}\n";
        return program;
    }
};

class GcMajorPass final : public GraphIRPass {
  public:
    explicit GcMajorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().majorGC();
        os << "{\"ok\":true,\"kind\":\"gc.major\"}\n";
        return program;
    }
};

class GcConfigPass final : public GraphIRPass {
  public:
    explicit GcConfigPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::configToJson() << '\n';
        return program;
    }
};

class GcSummaryPass final : public GraphIRPass {
  public:
    explicit GcSummaryPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::summaryToText();
        return program;
    }
};

class GcBlocksPass final : public GraphIRPass {
  public:
    explicit GcBlocksPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::array::debugRunBlockSelfTest();
        os << std::format(
            "BLOCK_SELFTEST ok live={} peak={} count={} creations={}\n",
            camel::core::mm::blockBytesLive(),
            camel::core::mm::blockBytesPeak(),
            camel::core::mm::blockLiveCount(),
            camel::core::mm::blockCreations());
        return program;
    }
};

class GcRememberedSetPass final : public GraphIRPass {
  public:
    explicit GcRememberedSetPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().debugRunRememberedSetSelfTest();
        os << "{\"ok\":true,\"kind\":\"gc.remembered_set\"}\n";
        return program;
    }
};

class GcPoisonPass final : public GraphIRPass {
  public:
    explicit GcPoisonPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().debugRunPoisonSelfTest();
        os << "{\"ok\":true,\"kind\":\"gc.poison\"}\n";
        return program;
    }
};

class GcTupleMembersPass final : public GraphIRPass {
  public:
    GcTupleMembersPass(const context_ptr_t &ctx, bool mismatch)
        : GraphIRPass(ctx), mismatch_(mismatch) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        // The collector trusts a value's static type, so a value carrying more members than its
        // type declares is a memory-safety bug. The reading that put this guard here had five
        // members against a type that declared zero (a residuals tuple typed by the operator
        // resolver's empty placeholder); here the disagreement is three against two, which is the
        // same bug without depending on `split`.
        //
        // The pass has two modes and the case runs both. `agree` is the control: two members, a
        // type of two, the walk runs and the program goes on. `mismatch` is the guard: it must stop
        // the process with the two counts in the message, and must not return -- a guard that
        // reports a disagreement and then reads anyway is worse than none, because the read it
        // allows may be taken as data.
        const type::TupleType *pair = type::TupleType::create(
            std::vector<type::Type *>{type::Type::Float(), type::Type::Float()});
        const size_t members = mismatch_ ? 3 : 2;
        ::Tuple *value       = ::Tuple::create(members, camel::core::mm::autoSpace());
        const auto relocate  = [](camel::core::rtdata::Object *object,
                                  const camel::core::type::Type *,
                                  const camel::core::rtdata::RefTraceInfo &) { return object; };
        value->updateRefs(relocate, pair);
        os << "{\"ok\":true,\"kind\":\"gc.tuple_members\",\"members\":" << members
           << ",\"declared\":2}\n";
        return program;
    }

  private:
    bool mismatch_;
};

class GcForeignResourcePass final : public GraphIRPass {
  public:
    explicit GcForeignResourcePass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::debugRunForeignResourceSelfTest(camel::core::mm::autoSpace());
        os << "{\"ok\":true,\"kind\":\"gc.foreign_resource\"}\n";
        return program;
    }
};

class GcViewsPass final : public GraphIRPass {
  public:
    explicit GcViewsPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::rtdata::debugRunViewSelfTest() << '\n';
        return program;
    }
};

class GcViewsEscapePass final : public GraphIRPass {
  public:
    explicit GcViewsEscapePass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        // A debug build aborts inside the store; a release build prints why it did not.
        os << camel::core::rtdata::debugRunViewEscapeSelfTest() << '\n';
        return program;
    }
};

// --------------------------------------------------------------------------------------------
// The kernels. A pass is a function (design principles 9.3): the name each line registers is the
// name its declaration in stdlib/std.cml gives it, and what follows it is the implementation.
// --------------------------------------------------------------------------------------------

using camel::execute::makePass;
using camel::execute::PassKernel;

const PassKernel gcSnapshotKernel("std::gc::snapshot", makePass<GcSnapshotPass>());
const PassKernel gcVerifyKernel("std::gc::verify", makePass<GcVerifyPass>());
const PassKernel gcMinorKernel("std::gc::minor", makePass<GcMinorPass>());
const PassKernel gcMajorKernel("std::gc::major", makePass<GcMajorPass>());
const PassKernel gcConfigKernel("std::gc::config", makePass<GcConfigPass>());
const PassKernel gcSummaryKernel("std::gc::summary", makePass<GcSummaryPass>());
const PassKernel gcBlocksKernel("std::gc::blocks", makePass<GcBlocksPass>());
const PassKernel gcViewsKernel("std::gc::views", makePass<GcViewsPass>());
const PassKernel gcViewsEscapeKernel("std::gc::views_escape", makePass<GcViewsEscapePass>());
const PassKernel gcRememberedSetKernel(
    "std::gc::remembered_set", makePass<GcRememberedSetPass>());
const PassKernel gcForeignResourceKernel(
    "std::gc::foreign_resource", makePass<GcForeignResourcePass>());
const PassKernel gcPoisonKernel("std::gc::poison", makePass<GcPoisonPass>());
const PassKernel gcTupleMembersKernel("std::gc::tuple_members", makePass<GcTupleMembersPass>(true));
const PassKernel gcTupleMembersAgreeKernel(
    "std::gc::tuple_members_agree",
    makePass<GcTupleMembersPass>(false));

} // namespace
