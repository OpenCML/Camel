// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Ending a child process together with everything it started.
 *
 * Killing only the child leaves its own children running: when an outer timeout ends a tool, the
 * checker that tool started keeps going as an orphan, and a checker stuck in a loop then holds a core
 * until someone notices. A child therefore has to be started so that its tree can be named, and the
 * tree has to be ended in one place:
 *
 *   - Windows: `taskkill /pid <pid> /T /F` ends the process and its descendants;
 *   - elsewhere: the child is started `detached`, which makes it the leader of its own process group,
 *     and the group is ended by signalling the negative pid.
 *
 * `spawn(command, args, { ...options, detached: DETACH_TREE })` is the matching start.
 */
import { spawnSync } from 'node:child_process'

const IS_WINDOWS = process.platform === 'win32'

/** The `detached` option that makes a child's tree killable on this platform. */
export const DETACH_TREE = !IS_WINDOWS

/** Ends `child` and every process it started. A child that already exited is not an error. */
export function killProcessTree(child) {
    if (!child || !child.pid) return
    if (IS_WINDOWS) {
        const killed = spawnSync('taskkill', ['/pid', String(child.pid), '/T', '/F'], {
            stdio: 'ignore',
            windowsHide: true,
        })
        if (killed.error || killed.status !== 0) {
            try {
                child.kill('SIGKILL')
            } catch {
                /* already gone */
            }
        }
        return
    }
    try {
        process.kill(-child.pid, 'SIGKILL')
    } catch {
        try {
            child.kill('SIGKILL')
        } catch {
            /* already gone */
        }
    }
}
