// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * CMake options - single source for build scripts and config
 */
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { BASEDIR, effectiveCmakeOptions } from './common.js'

const __filename = fileURLToPath(import.meta.url)
const OPTIONS_FILE = path.join(BASEDIR, 'cmake', 'options.json')
const USER_OPTS_FILE = path.join(BASEDIR, '.camel-opts.json')

export function loadOptions() {
    const data = JSON.parse(fs.readFileSync(OPTIONS_FILE, 'utf-8'))
    return data.options
}

export function loadUserOpts() {
    try {
        return JSON.parse(fs.readFileSync(USER_OPTS_FILE, 'utf-8'))
    } catch {
        return {}
    }
}

/**
 * Ensure .camel-opts.json exists; create from options.json defaults on first run
 */
export function ensureDefaultOpts() {
    if (fs.existsSync(USER_OPTS_FILE)) return
    const opts = loadOptions()
    const defaultOpts = Object.fromEntries(opts.map((o) => [o.name, o.default]))
    fs.writeFileSync(USER_OPTS_FILE, JSON.stringify(defaultOpts, null, 2) + '\n')
}

/** Value from .camel-opts or options.json default (no env override) */
export function getBaselineValue(opt, baselineOpts) {
    if (baselineOpts[opt.name] !== undefined) return baselineOpts[opt.name]
    return opt.default
}

export function getEffectiveValue(opt, userOpts) {
    if (userOpts[opt.name] !== undefined) return userOpts[opt.name]
    return opt.default
}

/**
 * Build -D flags for cmake. Reads from .camel-opts.json (or options.json default)
 *
 * The values come from `common.js: effectiveCmakeOptions`, the same reader the build stamp uses:
 * the `-D` flags that reach the compiler and the option set recorded in `out/<tag>/build-info.json`
 * are then one reading, and `-NoBuild` compares the install against what a build would pass now.
 */
export function getCmakeOptionFlags() {
    const flags = effectiveCmakeOptions().map((opt) => `-D${opt.name}=${opt.value}`)
    return flags.join(' ')
}
