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
allocated-byte metric describes device storage only. Pending submissions keep
their pinned staging and output storage alive through their completion events, so
removing the synchronous wait does not remove those dependencies. Draining a
`Resources` does not necessarily hand its blocks back to the driver: ready blocks
return to the backend-private pool described in `README.md`, and the owning
backend frees whatever is still idle when it is destroyed.

Execution objects own an immutable native program snapshot through shared
ownership. The snapshot retains borrowed device inputs and launch metadata until
after stream draining and resource cleanup. The module boundary moves the compiled
program into that snapshot; it does not copy its input tensor payloads to establish
ownership. Host result reconstruction shares the same snapshot, and benchmarking
retains separate optimized/reference snapshots. This establishes program lifetime
independence from the caller's stack; public execution is still synchronous. Completion
events, cross-stream dependencies, deferred fault propagation and context-safe
cleanup belong to the native submission layer described below. Public operators
still wait before publishing their results.

## Pending-submission ownership constraints

The native `Submission` owns a completion event together with its program,
module lease, argument table, fault word, stream and pinned staging allocations.
An event alone does not retain any of those objects. Every exit path after the
first enqueue, including construction failure, must drain or transfer ownership
of the submitted work before releasing its resources. `submitProgram` is an
internal interface: `run_preferred_resident` and resident downloads observe its
completion before returning to Camel. Completion events use their retained
backend context for recording, querying, waiting and destruction.

Device output allocations and submission state must have separate ownership:
output values may retain their producer submission, but the submission must not
retain those output values in turn. Dependency edges retain producer submissions
until their input reads complete. Cleanup should release completed dependencies
and staging rather than retaining an entire training history through the final
device value. The current completion implementation releases its program,
temporary resources, retained allocation owners and dependency references after
observing completion and caching success or failure. Its event and backend remain
available for a later consumer to establish stream ordering.

Ordering and fault observation are separate responsibilities. A consumer stream
can wait on a producer event without blocking the host. That event establishes
device execution order; it does not establish that the producer's scalar fault
word reports success. Observing a consumer result must also observe failures from
its required producers. Identity results, duplicate outputs and empty outputs
must preserve those obligations even when they enqueue no arithmetic kernel.

Storage reuse is a backend-private concern. A submission retains a shared lease
for every borrowed input and for each materialized output it publishes; the last
share returns the block to its own pool. Before any share is dropped along a
failure path -- a failed stream drain or a failed context activation during
cleanup -- the backend disables reuse on both pools, so an outstanding block is
abandoned instead of being cached or freed while it may still be the target of
pending device work. A disabled pool rejects further checkouts. Exported values
and borrowed inputs therefore never re-enter a pool after a failed drain.

`Resources` now owns its backend and activates its context for allocation and
cleanup, restoring the caller's context afterward. Its native regression queues
an upload and destroys the owner on a second thread, checking successful stream
draining, stream destruction, context restoration, and that the released blocks
reach the driver through the backend's final trim rather than through the
draining scope itself. Timing-event cleanup still relies on its synchronous
caller's context and must be replaced or extended before use in a pending
submission. Finalizers must not throw or acquire Camel allocator locks; a failed
CUDA context activation must not trigger use of handles in an unrelated current
context.

Acceptance evidence must include producer/consumer work on distinct streams,
dropping host references before completion, cache eviction while work is pending,
cross-thread teardown, chained producer faults, and identities/empty outputs.
The existing synchronous residency tests do not establish these properties.
