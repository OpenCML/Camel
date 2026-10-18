/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Exercise exceptional cleanup, concurrent completion and large pinned transfers.
#include "tiles/internal/cuda_submission.h"
#include "tiles/internal/submission.h"
#include <atomic>
#include <barrier>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace camel::tiles;
using namespace camel::tiles::cuda_detail;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Private backend, single-threaded injection. The original API is restored even
// when construction or event recording throws. No shared backend is modified.
struct FailureProbe {
    Api &api;
    decltype(Api::cuEventCreate) create;
    decltype(Api::cuEventRecord) record;
    decltype(Api::cuEventQuery) query;
    decltype(Api::cuStreamSynchronize) drain;
    bool failCreate = false, failRecord = false, pendingQuery = false;
    bool monitor = false, ownerLostBeforeDrain = false;
    int drains = 0;
    std::weak_ptr<const Program> program;
    std::weak_ptr<Compiled> module;
    inline static thread_local FailureProbe *active = nullptr;
    explicit FailureProbe(Api &a)
        : api(a), create(a.cuEventCreate), record(a.cuEventRecord), query(a.cuEventQuery),
          drain(a.cuStreamSynchronize) {
        active                  = this;
        api.cuEventCreate       = createEvent;
        api.cuEventRecord       = recordEvent;
        api.cuEventQuery        = queryEvent;
        api.cuStreamSynchronize = drainStream;
    }
    ~FailureProbe() {
        api.cuEventCreate       = create;
        api.cuEventRecord       = record;
        api.cuEventQuery        = query;
        api.cuStreamSynchronize = drain;
        active                  = nullptr;
    }
    static CUresult CUDAAPI createEvent(CUevent *event, unsigned flags) {
        auto &p = *active;
        return p.failCreate ? CUDA_ERROR_OUT_OF_MEMORY : p.create(event, flags);
    }
    static CUresult CUDAAPI recordEvent(CUevent event, CUstream stream) {
        auto &p = *active;
        return p.failRecord ? CUDA_ERROR_INVALID_VALUE : p.record(event, stream);
    }
    static CUresult CUDAAPI queryEvent(CUevent event) {
        auto &p = *active;
        return p.pendingQuery ? CUDA_ERROR_NOT_READY : p.query(event);
    }
    static CUresult CUDAAPI drainStream(CUstream stream) {
        auto &p = *active;
        if (p.monitor && (p.program.expired() || p.module.expired()))
            p.ownerLostBeforeDrain = true;
        ++p.drains;
        return p.drain(stream);
    }
};

ModuleLease compile(const std::shared_ptr<Backend> &backend, const std::string &source) {
    Current current(backend->api, backend->context);
    return backend->module(source);
}

void testFailedEvents() {
    for (bool createFailure : {true, false}) {
        auto backend = std::make_shared<Backend>();
        auto &api    = backend->api;
        FailureProbe probe(api);
        auto lease =
            compile(backend, "extern \"C\" __global__ void event_probe(unsigned* p){*p=42;}");
        auto program   = std::make_shared<const Program>();
        probe.program  = program;
        probe.module   = lease.compiled;
        auto resources = std::make_unique<Resources>(backend);
        auto pointer   = resources->allocate(sizeof(unsigned));
        {
            Current current(api, backend->context);
            CUfunction kernel;
            api.check(
                api.cuModuleGetFunction(&kernel, lease.compiled->module, "event_probe"),
                "resolve failed-event probe");
            void *args[] = {&pointer};
            api.check(
                api.cuLaunchKernel(kernel, 1, 1, 1, 1, 1, 1, 0, resources->stream, args, nullptr),
                "enqueue failed-event probe");
        }
        backend->cache.clear();
        probe.monitor    = true;
        probe.failCreate = createFailure;
        bool rejected    = false;
        std::shared_ptr<Submission> submission;
        try {
            submission = std::make_shared<Submission>(
                backend,
                std::move(resources),
                std::move(program),
                std::move(lease));
            probe.failRecord = true;
            submission->finish();
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected, "injected event failure was not reported");
        // The failed operation drains before any retained executable/input dies.
        check(
            probe.drains == 1 && !probe.ownerLostBeforeDrain,
            "event failure released owners before stream draining");
        check(
            probe.program.expired() && probe.module.expired(),
            "event failure retained program or module");
        probe.monitor = false;
        submission.reset();
    }
    std::cout << "tiles async failures: event creation and record errors drain before releasing "
                 "owners passed\n";
}

void testConcurrentObservers() {
    for (bool fault : {false, true}) {
        Program p;
        if (fault) {
            p.nodes = {
                {.op = "input", .scalarBits = std::bit_cast<uint64_t>(1.0), .dtype = DType::F64},
                {.op = "input", .scalarBits = std::bit_cast<uint64_t>(0.0), .dtype = DType::F64},
                {.op = "scalar_div", .inputs = {0, 1}, .dtype = DType::F64}};
            p.outputs = {2};
        } else {
            p.nodes = {
                {.op = "input", .shape = {2}, .data = {1, 3}},
                {.op = "add", .shape = {2}, .inputs = {0, 0}}};
            p.outputs = {1};
        }
        generate(p);
        auto submitted = submitProgram(std::make_shared<const Program>(std::move(p)));
        std::barrier start(3);
        std::atomic<int> observed{0};
        auto observe = [&](bool query) {
            start.arrive_and_wait();
            try {
                if (query)
                    (void)submitted.completion->ready();
                submitted.completion->wait();
                if (!fault)
                    ++observed;
            } catch (const std::exception &error) {
                if (fault && std::string(error.what()).find("division by zero at Tiles node") !=
                                 std::string::npos)
                    ++observed;
            }
        };
        std::thread waiter(observe, false), reader(observe, true);
        start.arrive_and_wait();
        waiter.join();
        reader.join();
        check(observed == 2, "concurrent ready/wait observers disagreed");
        if (!fault) {
            const auto bytes       = downloadValue(*submitted.outputs.at(0));
            const float expected[] = {2, 6};
            check(
                bytes.size() == sizeof(expected) &&
                    std::memcmp(bytes.data(), expected, sizeof(expected)) == 0,
                "concurrent completion changed result bits");
        }
    }
    std::cout
        << "tiles async observers: concurrent ready and wait preserve results and faults passed\n";
}

void testLargeStaging() {
    constexpr unsigned count = 8 * 1024 * 1024 + 1;
    constexpr size_t size    = size_t(count) * sizeof(float);
    auto backend             = std::make_shared<Backend>();
    auto &api                = backend->api;
    FailureProbe probe(api);
    auto lease = compile(
        backend,
        "extern \"C\" __global__ void staged_add(const float* x,float* y,unsigned n){"
        "unsigned i=blockIdx.x*blockDim.x+threadIdx.x;if(i<n)y[i]=x[i]+1.0f;}");
    auto program                                 = std::make_shared<const Program>();
    std::weak_ptr<const Program> programLifetime = program;
    auto inputOwner                              = std::make_shared<Resources>(backend);
    std::weak_ptr<Resources> inputLifetime       = inputOwner;
    auto input                                   = inputOwner->allocate(size);
    auto stagingMarker                           = std::make_shared<int>(1);
    std::weak_ptr<int> stagingLifetime           = stagingMarker;
    auto staging                                 = std::make_unique<Resources>(backend);
    auto pinned = static_cast<float *>(staging->allocateHost(size));
    for (unsigned i = 0; i < count; ++i)
        pinned[i] = float(i % 251) * 0.25f;
    {
        Current current(api, backend->context);
        api.check(
            api.cuMemcpyHtoDAsync(input, pinned, size, staging->stream),
            "enqueue large pinned upload");
    }
    auto consumerLease = lease;
    auto producer =
        std::make_shared<Submission>(backend, std::move(staging), program, std::move(lease));
    producer->keepAlive(inputOwner);
    producer->keepAlive(stagingMarker);
    producer->finish();
    auto outputOwner = std::make_shared<Resources>(backend);
    auto output      = outputOwner->allocate(size);
    auto consumer    = std::make_shared<Submission>(
        backend,
        std::make_unique<Resources>(backend),
        program,
        std::move(consumerLease));
    // Deterministically exercise the not-yet-observed path without timing claims
    // or a spinning GPU gate. Real DMA and arithmetic still run on separate streams.
    probe.pendingQuery = true;
    consumer->keepAlive(inputOwner);
    consumer->depend(producer);
    consumer->keepAlive(outputOwner);
    {
        Current current(api, backend->context);
        CUfunction kernel;
        api.check(
            api.cuModuleGetFunction(&kernel, backend->cache.back()->module, "staged_add"),
            "resolve large staged kernel");
        unsigned n   = count;
        void *args[] = {&input, &output, &n};
        api.check(
            api.cuLaunchKernel(
                kernel,
                (count + 255) / 256,
                1,
                1,
                256,
                1,
                1,
                0,
                consumer->resources().stream,
                args,
                nullptr),
            "enqueue large staged consumer");
    }
    consumer->finish();
    producer.reset();
    program.reset();
    inputOwner.reset();
    stagingMarker.reset();
    check(!programLifetime.expired(), "unobserved submission lost its program owner");
    check(
        !inputLifetime.expired() && !stagingLifetime.expired(),
        "unobserved submission lost input or staging owners");
    probe.pendingQuery = false;
    consumer->wait();
    check(programLifetime.expired(), "completed large transfer retained program snapshots");
    check(
        inputLifetime.expired() && stagingLifetime.expired(),
        "completed transfer retained input or staging owners");
    std::vector<float> result(count);
    {
        Current current(api, backend->context);
        api.check(api.cuMemcpyDtoH(result.data(), output, size), "read large staged result");
    }
    for (unsigned i = 0; i < count; ++i)
        check(
            std::bit_cast<uint32_t>(result[i]) ==
                std::bit_cast<uint32_t>(float(i % 251) * 0.25f + 1.0f),
            "large staged transfer changed result bits");
    std::cout << "tiles async staging: over 32 MiB distinct-stream transfer matches every output "
                 "bit passed\n";
}

void testConcurrentSubmissions() {
    std::shared_ptr<const Program> programs[2];
    std::string source;
    for (int i = 0; i < 2; ++i) {
        Program p;
        p.nodes = {
            {.op = "input", .shape = {2}, .data = {float(2 + i * 5), float(5 + i * 6)}},
            {.op = "add", .shape = {2}, .inputs = {0, 0}}};
        p.outputs = {1};
        generate(p);
        // A private source identity guarantees a cold key relative to earlier
        // tests while keeping both concurrent callers' programs identical.
        p.source += "\n// async native concurrent publication acceptance\n";
        if (i == 0)
            source = p.source;
        else
            check(source == p.source, "different input payloads changed the compiled source");
        programs[i] = std::make_shared<const Program>(std::move(p));
    }
    SubmittedProgram submitted[2];
    std::exception_ptr failures[2];
    std::barrier start(3);
    auto submit = [&](int i) {
        start.arrive_and_wait();
        try {
            submitted[i] = submitProgram(programs[i]);
        } catch (...) {
            failures[i] = std::current_exception();
        }
    };
    std::thread first(submit, 0), second(submit, 1);
    start.arrive_and_wait();
    first.join();
    second.join();
    for (int i = 0; i < 2; ++i) {
        if (failures[i])
            std::rethrow_exception(failures[i]);
        submitted[i].completion->wait();
        const auto bytes       = downloadValue(*submitted[i].outputs.at(0));
        const float expected[] = {float(4 + i * 10), float(10 + i * 12)};
        check(
            bytes.size() == sizeof(expected) &&
                std::memcmp(bytes.data(), expected, sizeof(expected)) == 0,
            "concurrent submissions shared or corrupted input allocations");
    }
    auto backend = sharedBackend();
    {
        std::lock_guard lock(backend->mutex);
        size_t matches = 0;
        for (const auto &entry : backend->cache)
            if (entry->source == source)
                ++matches;
        check(matches == 1, "concurrent compilation published duplicate cache entries");
    }
    std::cout << "tiles async submit: concurrent same-source inputs and cache publication passed\n";
}
} // namespace

void testAsyncFailuresAndConcurrency() {
    testFailedEvents();
    testConcurrentObservers();
    testLargeStaging();
    testConcurrentSubmissions();
}
