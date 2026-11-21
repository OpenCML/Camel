#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the tests that pin the **vector** tensor unit's bits
// (`skip_unless = ["node", "test/tools/have-vector-tensor-unit.mjs"]`).
//
// The fused GELU gradient has a portable unit and an AVX2+FMA one, and the wider unit is compiled
// only for x86-64 (`modules/tensor/CMakeLists.txt`, the `x86_64|AMD64|...` branch); the other
// processor targets build the portable unit alone. Which one a run uses is decided once, from the
// processor, before the first kernel (`modules/tensor/kernels/cpu.cpp`). So on every arm64 build --
// Apple Silicon is the one that matters here -- the default unit is the portable one, and the three
// digests `gelu_grad.bits` pins are the ones `gelu_grad.bits.generic_unit` pins with
// `--tensor-isa generic`. The entry that expects the vector unit's digests has no reading there:
// its red would be a fact about the processor, not about the gradient.
//
// The question is asked of the build and the processor, not of the gradient: a gradient that moved
// would still red the entry on a host that passes here (the probe does not look at the digests).
// Exit 0 where the default unit is the vector unit, 1 with the reason elsewhere.

import fs from 'node:fs'
import { spawnSync } from 'node:child_process'

/** The processor's feature words, as far as this platform exposes them to Node; null when it
 * exposes none. Linux writes them in /proc/cpuinfo; macOS in two `sysctl` keys (the base features
 * and leaf 7); Windows has no list Node can read. */
function cpuFlags() {
    if (process.platform === 'linux') {
        try {
            const text = fs.readFileSync('/proc/cpuinfo', 'utf8')
            const line = text.split('\n').find((entry) => entry.startsWith('flags'))
            return line ? line.slice(line.indexOf(':') + 1).trim().split(/\s+/) : null
        } catch {
            return null
        }
    }
    if (process.platform === 'darwin') {
        const sysctl = (key) => {
            const proc = spawnSync('/usr/sbin/sysctl', ['-n', key], { encoding: 'utf8' })
            return proc.status === 0 ? proc.stdout.trim().split(/\s+/).filter(Boolean) : []
        }
        const flags = [...sysctl('machdep.cpu.features'), ...sysctl('machdep.cpu.leaf7_features')]
        return flags.length > 0 ? flags : null
    }
    return null
}

if (process.arch !== 'x64') {
    console.log(
        `the AVX2/AVX-512 tensor units are compiled for x86-64 only ` +
            `(modules/tensor/CMakeLists.txt); on ${process.arch} the default unit is the portable one`
    )
    process.exit(1)
}

const flags = cpuFlags()
if (flags === null) {
    // Windows: Node has no CPUID and no feature list to read. The build and release baseline is
    // x86-64-v3 (AVX2 and FMA: docs/design-principles.cn.md 9), so the unit a run of that build
    // selects is the vector one; a hand-built binary for an older x86-64 would red the entry here,
    // which is the same red it would give today.
    if (process.platform === 'win32') process.exit(0)
    console.log(`this ${process.platform} host exposes no processor feature list, so whether the default unit is the vector one cannot be read`)
    process.exit(1)
}
if (!flags.includes('avx2') || !flags.includes('fma')) {
    console.log(
        `this x86-64 processor reports ${flags.includes('avx2') ? '' : 'no avx2 '}` +
            `${flags.includes('fma') ? '' : 'no fma'}: the default tensor unit is the portable one`
    )
    process.exit(1)
}
process.exit(0)