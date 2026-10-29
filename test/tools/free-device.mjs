#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Which card a GPU test runs on, for a test that does not name one itself.
 *
 * A plan that runs "on the device" used to mean device 0, and that is a bet about the host. On a
 * machine many people share, the lowest-numbered card is often the one another tenant has already
 * filled: `cuDevicePrimaryCtxRetain` then answers `CUDA_ERROR_OUT_OF_MEMORY` and the run dies with
 * "CUDA device 0 has no free memory for a context", which reads like a defect in the code under
 * test and is not one. Naming a card by hand (`CUDA_VISIBLE_DEVICES=5`) made the same plan pass,
 * so the plan was not wrong about the code; it was wrong about the host.
 *
 * So the runner asks the host which cards are idle and gives the child exactly one of them. Idle
 * is `memory.used` below `FREE_MEMORY_MIB`: a card with a tenant's weights on it is not idle at
 * any utilisation, and a card nobody has opened holds a few MiB of driver state at most.
 *
 * The rule that must not bend: only a selection that finds no idle card SKIPS the test. Once a
 * card is chosen the test runs and fails like any other test, because an out-of-memory error
 * after a card was chosen is the code under test's and a skip there would be a red turned green.
 * A host that lists no card at all is not that case either: nothing is occupied, the test simply
 * has no device to be given, and it runs as it always did (the caller decides what that means).
 *
 * `nvidia-smi` is asked one card at a time rather than through a CUDA query of our own: the
 * runner is a node process, and the reading must be of the machine's own accounting, not of
 * what a build without the CUDA back end can see.
 */

import { spawn } from 'node:child_process'
import { pathToFileURL } from 'node:url'

/** A card with less than this much memory in use is idle enough to take a context of ours. */
export const FREE_MEMORY_MIB = 1024

/**
 * The variable CUDA reads to order the cards, and the one value that makes its ordinals the ones
 * `nvidia-smi` prints. The runner sets both with the chosen card: by default CUDA orders devices
 * fastest-first, which on a host of mixed cards is not PCI order, and then "card 5" would name a
 * different card to CUDA than to the tool the number came from.
 */
export const CUDA_DEVICE_ORDER = 'CUDA_DEVICE_ORDER'
export const PCI_BUS_ID = 'PCI_BUS_ID'

const NVIDIA_SMI = 'nvidia-smi'
const QUERY_ARGS = [
    '--query-gpu=index,uuid,memory.used,memory.total,name',
    '--format=csv,noheader,nounits'
]
const PROBE_TIMEOUT_MS = 20000

/**
 * The cards of an `nvidia-smi --query-gpu=index,uuid,memory.used,memory.total,name` listing, in
 * ordinal order. A line whose fields are not numbers is dropped rather than guessed at: some
 * drivers print `[N/A]` for a card they cannot read, and a card whose memory is unreadable cannot
 * be called idle.
 */
export function parseCards(text) {
    const cards = []
    for (const raw of String(text).split(/\r?\n/)) {
        const line = raw.trim()
        if (!line) continue
        const fields = line.split(',').map((field) => field.trim())
        if (fields.length < 4) continue
        const ordinal = Number(fields[0])
        const usedMiB = Number(fields[2])
        const totalMiB = Number(fields[3])
        if (!Number.isInteger(ordinal) || ordinal < 0) continue
        if (!Number.isFinite(usedMiB) || usedMiB < 0) continue
        if (!Number.isFinite(totalMiB) || totalMiB <= 0) continue
        cards.push({ ordinal, uuid: fields[1], usedMiB, totalMiB, name: fields.slice(4).join(', ') })
    }
    return cards.sort((left, right) => left.ordinal - right.ordinal)
}

/**
 * The threshold in use. `CAMEL_TEST_GPU_FREE_MIB` moves it, which is how a host with idle cards
 * can be made to answer "none are idle" without touching the code: the skip state is one a person
 * should be able to see a plan report, and a skip that cannot be reproduced on purpose is a skip
 * nobody has read.
 */
export function freeMemoryLimitMib(env = process.env) {
    const raw = Number(env.CAMEL_TEST_GPU_FREE_MIB)
    return Number.isFinite(raw) && raw >= 0 ? raw : FREE_MEMORY_MIB
}

/** Ask the host for its cards: `{ cards }`, or `{ cards: [], error }` where it cannot answer. */
export function probeCards({ spawnImpl = spawn, env = process.env } = {}) {
    // `CAMEL_TEST_GPU_CARDS` is the listing to read instead of asking a driver. It exists for the
    // runner's own self-test, which must see both answers -- one idle card and none -- on a host
    // that has whatever cards it has; a listing that says a card is idle only leads the test to
    // run and fail, so it cannot turn a red green.
    if (typeof env.CAMEL_TEST_GPU_CARDS === 'string') {
        return Promise.resolve({ cards: parseCards(env.CAMEL_TEST_GPU_CARDS), error: null })
    }
    return new Promise((resolve) => {
        let child
        try {
            child = spawnImpl(NVIDIA_SMI, QUERY_ARGS, { stdio: ['ignore', 'pipe', 'pipe'] })
        } catch (error) {
            resolve({ cards: [], error: error.message })
            return
        }
        let stdout = ''
        let stderr = ''
        let settled = false
        const finish = (value) => {
            if (settled) return
            settled = true
            clearTimeout(timer)
            resolve(value)
        }
        const timer = setTimeout(() => {
            if (child && typeof child.kill === 'function') child.kill()
            finish({ cards: [], error: `${NVIDIA_SMI} did not answer within ${PROBE_TIMEOUT_MS} ms` })
        }, PROBE_TIMEOUT_MS)
        if (typeof timer.unref === 'function') timer.unref()
        // `error` before the streams: a command that could not be started reports that through
        // `error`, and its streams may never deliver a byte.
        child.on('error', (error) => {
            // No `nvidia-smi` on this host, or no permission to run it: there is no answer, and
            // the caller must not read that as "the cards are busy".
            finish({ cards: [], error: error.message })
        })
        if (child.stdout) {
            child.stdout.on('data', (data) => {
                stdout += data.toString()
            })
        }
        if (child.stderr) {
            child.stderr.on('data', (data) => {
                stderr += data.toString()
            })
        }
        child.on('close', (code) => {
            if (code !== 0) {
                const detail = stderr.trim().split(/\r?\n/)[0]
                finish({ cards: [], error: detail || `${NVIDIA_SMI} exited ${code}` })
                return
            }
            finish({ cards: parseCards(stdout), error: null })
        })
    })
}

/** The reason a run gives for skipping: the cards, their numbers, and the bar they missed. */
export function noIdleCardReason(cards, limitMiB) {
    const listed = cards
        .map((card) => `card ${card.ordinal} ${card.usedMiB}/${card.totalMiB} MiB`)
        .join(', ')
    const count =
        cards.length === 1
            ? 'the only card on this host is held by another process'
            : `all ${cards.length} cards on this host are held by another process`
    return (
        `no idle CUDA device: ${count} ` +
        `(memory.used >= ${limitMiB} MiB each): ${listed}. The test needs a card of its own, so it is ` +
        `skipped rather than run on a card it would have to share`
    )
}

/**
 * The card a test should be given:
 *
 *   `{ kind: 'device', ... }` -- the lowest-numbered idle card;
 *   `{ kind: 'busy', reason }` -- the host has cards and none is idle; the test must skip;
 *   `{ kind: 'absent', reason }` -- the host answered nothing (no `nvidia-smi`, or no card); the
 *   test runs as it would have, because nothing is occupied and no card is being taken from
 *   anyone.
 */
export async function chooseFreeDevice(options = {}) {
    const {
        env = process.env,
        limitMiB = freeMemoryLimitMib(env),
        probe = probeCards
    } = options
    const { cards, error } = await probe({ env })
    if (error) return { kind: 'absent', reason: `${NVIDIA_SMI} could not be asked: ${error}` }
    if (cards.length === 0) return { kind: 'absent', reason: `${NVIDIA_SMI} lists no card` }
    const idle = cards.filter((card) => card.usedMiB < limitMiB)
    if (idle.length === 0) return { kind: 'busy', reason: noIdleCardReason(cards, limitMiB), cards }
    const card = idle[0]
    return { ...card, kind: 'device', cards }
}

/**
 * A person asking the host the same question the runner asks: which card would a GPU test get?
 * Exit 0 with the card, 1 when none is idle (the reason is the one a plan would print), 2 when the
 * host has nothing to offer.
 */
const isMain = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href
if (isMain) {
    const chosen = await chooseFreeDevice()
    if (chosen.kind === 'device') {
        console.log(
            `card ${chosen.ordinal} is idle (${chosen.usedMiB}/${chosen.totalMiB} MiB): ${chosen.name}`
        )
        process.exit(0)
    }
    console.log(chosen.reason)
    process.exit(chosen.kind === 'busy' ? 1 : 2)
}