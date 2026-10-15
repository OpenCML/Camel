# Transfer completion contract

Inputs and the kernel argument table are copied from owned snapshots into pinned
host buffers. Transfers use `cuMemcpyHtoDAsync` on the same stream as kernels.
The current synchronous API waits for upload completion; benchmarking therefore
reports completed upload time rather than just enqueue/staging time. Pinned
buffers remain alive until the stream drains, including exception cleanup.

An earlier implementation used pageable `cuMemcpyHtoD` with a nonblocking kernel
stream. The host call can return after staging, before the DMA finishes. This
does not establish the dependency required by the other stream. See NVIDIA's
[driver synchronization contract](https://docs.nvidia.com/cuda/archive/12.0.0/cuda-driver-api/api-sync-behavior.html).

The issue appeared when validating a 65537x128 reduction: the last two rows could
read zeros instead of the uploaded data. Same-stream pinned transfers eliminated
the observed error. A dedicated regression repeats an elementwise computation
over 8,388,736 f32 values (slightly over 32 MiB) and compares every output to CPU.
Small-input successes are insufficient evidence for correct transfer ordering.

Pinned staging allocations are included in host setup time, but `inspect`'s
allocated-byte metric describes device storage only. The future asynchronous
API must keep pinned buffers and device storage alive through completion events;
it must not remove these dependencies when removing the synchronous wait.

Execution objects own an immutable native program snapshot through shared
ownership. The snapshot retains borrowed device inputs and launch metadata until
after stream draining and resource cleanup. The module boundary moves the compiled
program into that snapshot; it does not copy its input tensor payloads to establish
ownership. Host result reconstruction shares the same snapshot, and benchmarking
retains separate optimized/reference snapshots. This establishes program lifetime
independence from the caller's stack; execution is still synchronous. Completion
events, cross-stream dependencies, deferred fault propagation and context-safe
cleanup remain prerequisites for asynchronous submission.

## Pending-submission ownership constraints

The next execution layer must own a completion event together with its program,
module lease, argument table, fault word, stream and pinned staging allocations.
An event alone does not retain any of those objects. Every exit path after the
first enqueue, including construction failure, must drain or transfer ownership
of the submitted work before releasing its resources.

Device output allocations and submission state must have separate ownership:
output values may retain their producer submission, but the submission must not
retain those output values in turn. Dependency edges retain producer submissions
until their input reads complete. Cleanup should release completed dependencies
and staging rather than retaining an entire training history through the final
device value.

Ordering and fault observation are separate responsibilities. A consumer stream
can wait on a producer event without blocking the host. That event establishes
device execution order; it does not establish that the producer's scalar fault
word reports success. Observing a consumer result must also observe failures from
its required producers. Identity results, duplicate outputs and empty outputs
must preserve those obligations even when they enqueue no arithmetic kernel.

Current `Resources` cleanup and timing-event cleanup assume that the caller has
made the owning CUDA context current. That assumption is valid for the existing
synchronous call scope but cannot be retained for cross-thread destruction of a
pending submission. Activation, cleanup and restoration of the prior context
must be internal to that ownership boundary. Finalizers must not throw or acquire
Camel allocator locks; a failed CUDA context activation must not trigger use of
handles in an unrelated current context.

Acceptance evidence must include producer/consumer work on distinct streams,
dropping host references before completion, cache eviction while work is pending,
cross-thread teardown, chained producer faults, and identities/empty outputs.
The existing synchronous residency tests do not establish these properties.
