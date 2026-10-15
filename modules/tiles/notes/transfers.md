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
