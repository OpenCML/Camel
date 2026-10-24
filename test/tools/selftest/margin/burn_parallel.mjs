// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/load-class/run.mjs: work whose wall clock is bounded by
// construction and whose CPU time is not.
//
// The margin rule reads the CPU time of a test's process tree, so the fixture that has to cross it
// is by definition a test with no room for a busy host -- and a single thread burning that figure
// is killed by its own liveness bound on a loaded machine before the rule can speak, because
// burning CPU means waiting to be scheduled. Measured inside a -Jobs 8 gate on the shared host:
// 400 ms of CPU on one thread took 970-1040 ms of wall against a 1000 ms bound, and the fixture
// went red for the wrong reason. So the wall here is a deadline each worker computes for itself
// (`wallMs` after it starts), which no amount of load can stretch into the bound, and the CPU
// figure is the sum over the workers -- which is what the reading is (test/tools/child-cpu.mjs
// sums the tree). The workers' start-up CPU is part of that sum, so a host that hands the process
// a fraction of one core still charges more than its share of the work.
import { Worker, isMainThread, workerData } from 'node:worker_threads'

if (isMainThread) {
    const wallMs = Number(process.argv[2] ?? 400)
    const workers = Number(process.argv[3] ?? 8)
    await Promise.all(
        Array.from(
            { length: workers },
            () =>
                new Promise((resolve, reject) => {
                    const worker = new Worker(new URL(import.meta.url), { workerData: { wallMs } })
                    worker.on('exit', resolve)
                    worker.on('error', reject)
                })
        )
    )
} else {
    const deadline = Date.now() + workerData.wallMs
    let sink = 0
    while (Date.now() < deadline) {
        for (let index = 0; index < 100000; index++) sink += Math.sqrt(index)
    }
    // Keep the loop's result observable, so no engine can fold the work away.
    if (!Number.isFinite(sink)) process.exit(3)
}