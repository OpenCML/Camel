#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Which CUDA devices a test's Python child may see. One definition, two readers: the plan runner
 * (`run-tests.mjs`) applies the decision to the child it starts, and the guard
 * (`check-cuda-visibility.mjs`) reads the same decision back out of the plans.
 *
 * Why a Python child is hidden from the cards unless it asks for them: a CPU-only PyTorch
 * program reaches a device without ever naming one. `torch.optim.Adam(...).step()` calls torch's
 * `_accelerator_graph_capture_health_check()` (torch's own comment: `# Currently needed by Adam
 * and AdamW`), which asks for the current accelerator and its current stream; that retains the
 * primary context of every card on the machine. `cuDevicePrimaryCtxRetain` returns
 * `CUDA_ERROR_UNKNOWN` (999) while another tenant holds a card, and the process dies before its
 * first assertion. `SGD` does not make the call, so the red set moves with who else is on the
 * host. The plan-side rule this implements is in `docs/test-plans.md`, "CUDA visibility".
 *
 * Hidden is the default because a test that measures what the CPU does must not depend on who
 * else is on the host, and because a test that wants a device is a different, deliberate thing:
 * it says so with `cuda_devices = true`, in its own entry or in the plan's `[defaults]`, and the
 * runner then leaves the machine's `CUDA_VISIBLE_DEVICES` exactly as it found it -- or with
 * `cuda_free_device = true`, where the runner names one idle card of its own choosing instead
 * (`test/tools/free-device.mjs`). Either way the child sees a device it was given, not a list of
 * cards that answers to whoever else is on the host.
 *
 * This must not be copied onto a test that really uses a device (56.3): it hides the card, it
 * does not free it, and a device test that adopted it would stop telling "there is no device"
 * from "the device is held by someone else".
 */

/** The variable CUDA reads to decide which cards a process may open. */
export const CUDA_VISIBLE_DEVICES = 'CUDA_VISIBLE_DEVICES'

/** What the runner writes to hide every card: CUDA's own "names no device" spelling. */
export const CUDA_HIDDEN = ''

/**
 * The values that name no device at all. CUDA reads `-1` as a device list naming nothing, and
 * the empty value names nothing either; every other value (`0`, `0,1`, a GPU UUID) names a card.
 */
export function hidesEveryDevice(value) {
    const text = String(value).trim()
    return text === '' || text === '-1'
}

/**
 * The `CUDA_VISIBLE_DEVICES` the test's own `envs` set, or null when it sets none. A test that
 * writes the variable has made the decision itself, next to its other environment values, and the
 * runner does not overrule it.
 */
export function declaredVisibility(test) {
    for (const entry of test.envs || []) {
        const text = String(entry)
        const eq = text.indexOf('=')
        if (eq <= 0) continue
        if (text.slice(0, eq).trim() === CUDA_VISIBLE_DEVICES) return text.slice(eq + 1)
    }
    return null
}

/**
 * Whether the test declares that its child wants a device. `cuda_free_device` is the same promise
 * written more strictly -- "one idle card, the runner picks it" -- so it declares too: the child is
 * handed a card by the runner, and hiding the cards from it would leave it measuring the CPU.
 */
export function declaresDevices(test) {
    return test.cuda_devices === true || test.cuda_free_device === true
}

/**
 * Whether the runner hides the cards from this test's Python child. It does, unless the test
 * declared `cuda_devices = true` or named the variable itself with a value that keeps a card.
 */
export function hidesCudaDevices(test) {
    if (declaresDevices(test)) return false
    const own = declaredVisibility(test)
    if (own === null) return true
    return hidesEveryDevice(own)
}

/**
 * How a test reaches Python. `tool` is true when the test process itself is the interpreter;
 * `precondition` is true when its `skip_unless` command is `${python}`. Both are entry points the
 * runner wraps with the rule above. A Python program reached any other way -- a `.py` case under
 * another tool, an interpreter named in `args` -- is outside the runner's reach, and the guard
 * requires such a test to state the visibility itself.
 */
export function pythonEntryPoints(test) {
    const skip = (test.skip_unless || []).map((word) => String(word))
    return {
        tool: (test.tool || 'camel') === 'python',
        precondition: skip.some((word) => word === '${python}'),
    }
}