// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The launcher that `bin/<tool>` becomes on Linux: the one place that can speak before the dynamic
 * loader refuses the install.
 *
 * `collect-out` copies the toolchain's `libstdc++.so.6` into `libs/` and the tools carry a `$ORIGIN`
 * rpath, so an install starts anywhere without any caller exporting `LD_LIBRARY_PATH`. What that
 * does not answer is the install that finds a *too old* copy -- `libs/` missing (an install from
 * before 26888783c, or a `bin/` copied without its `libs/`) or `libs/libstdc++.so.6` replaced by the
 * distribution's.
 *
 * A check inside the program cannot answer it. The versions a binary requires from
 * `libstdc++.so.6` are ELF version requirements, and the loader resolves them before `main`:
 * measured 2026-11-04 on the Linux host, with `libs/libstdc++.so.6` replaced by the distribution's
 * 3.4.30, `bin/camel --version` printed seven `version `GLIBCXX_3.4.3x' not found` lines and exited
 * 1 -- and not one line of them came from Camel (`docs/technical/35_install_cxx_runtime.md` has the
 * transcript). Nothing that runs as part of that process can precede it, so the sentence has to come
 * from the only thing that runs before the process: a launcher.
 *
 * The launcher does not reimplement the loader's search. It asks the loader: `ld.so --list` maps
 * exactly what an `exec` would map and names the same file, so the verdict cannot disagree with the
 * run it is about to start. Every other loader complaint is passed through untouched -- the script
 * runs the binary and the loader reports it exactly as it did before this file existed.
 */

import fs from 'fs'
import path from 'path'

/** The runtime `collect-out` bundles into `libs/`; the launcher names it in what it prints. */
export const CXX_RUNTIME_LIBRARY = 'libstdc++.so.6'

/** The entry point every caller starts: tests, scripts, the wheel's console entry, a user. */
export const GUARDED_TOOL = 'camel'

/**
 * The interpreter (`PT_INTERP`) of an ELF executable, which is the loader that will decide whether
 * the binary starts. Read from the file instead of guessed from the architecture: the same bytes
 * that name the loader for the kernel are the ones this launcher hands `--list`.
 *
 * `null` for anything that is not a little-endian 64-bit ELF (the launcher is only written for
 * Linux, and a file this cannot read is one the launcher must not speak for).
 */
export function elfInterpreter(file) {
    let handle
    try {
        handle = fs.openSync(file, 'r')
        const header = Buffer.alloc(64)
        fs.readSync(handle, header, 0, header.length, 0)
        if (header.toString('latin1', 0, 4) !== '\x7fELF') return null
        const is64 = header[4] === 2
        const littleEndian = header[5] === 1
        if (!is64 || !littleEndian) return null
        const programHeadersOffset = Number(header.readBigUInt64LE(0x20))
        const programHeaderSize = header.readUInt16LE(0x36)
        const programHeaderCount = header.readUInt16LE(0x38)
        if (programHeaderSize === 0 || programHeaderCount === 0) return null
        const programHeaders = Buffer.alloc(programHeaderSize * programHeaderCount)
        fs.readSync(handle, programHeaders, 0, programHeaders.length, programHeadersOffset)
        for (let index = 0; index < programHeaderCount; index += 1) {
            const base = index * programHeaderSize
            if (programHeaders.readUInt32LE(base) !== 3) continue // PT_INTERP
            const offset = Number(programHeaders.readBigUInt64LE(base + 8))
            const size = Number(programHeaders.readBigUInt64LE(base + 32))
            if (size <= 0) return null
            const text = Buffer.alloc(size)
            fs.readSync(handle, text, 0, size, offset)
            const end = text.indexOf(0)
            return text.toString('utf8', 0, end === -1 ? size : end)
        }
        return null
    } catch {
        return null
    } finally {
        if (handle !== undefined) fs.closeSync(handle)
    }
}

/**
 * The launcher's text.
 *
 * Every line is a POSIX `sh` line: an install is read on the distribution the user has, not on the
 * one the build ran on, so nothing here may depend on bash. `${...}` never appears: the text is
 * assembled from literal lines, which keeps shell syntax and JavaScript apart.
 */
export function launcherText({ tool, realName, interpreter }) {
    return [
        '#!/bin/sh',
        `# ${tool}'s entry point in this install. It starts ${realName}, and before it does it answers one`,
        `# question the program itself cannot: is the C++ standard library this install finds new enough`,
        '# to load it?',
        '#',
        '# Nothing inside the program can answer it. The GLIBCXX versions a binary requires are ELF',
        '# version requirements and the dynamic loader resolves them before `main`, so a too-old',
        '# libstdc++.so.6 ends the process with one `version `GLIBCXX_3.4.x\' not found` line per',
        '# requirement, before a single line of Camel is printed.',
        '#',
        '# This script runs before that process exists. It asks the same loader the kernel would use',
        `# (\`--list\` maps what an exec maps, and names the file it picked), and when the answer is a`,
        '# version that is too old it says which version is needed, which one is there, and where to put',
        '# a newer one. Any other loader complaint is not its business: such a run execs the binary and',
        '# the loader reports it exactly as it did before this script existed.',
        '#',
        '# Written by scripts/cxx-runtime-guard.mjs (scripts/collect-out.js), explained with the readings',
        '# in docs/technical/35_install_cxx_runtime.md. Edit the writer, not this file.',
        'set -u',
        '',
        'self=$0',
        'case $self in',
        '*/*) ;;',
        '*)',
        '    found=$(command -v "$self" 2>/dev/null || :)',
        '    if [ -z "$found" ]; then',
        `        printf '${tool}: cannot find itself on PATH, so it cannot find the install it belongs to.\\n' >&2`,
        '        exit 1',
        '    fi',
        '    self=$found',
        '    ;;',
        'esac',
        '',
        '# A symbolic link on PATH names the install its target lives in, not the directory of the link.',
        'linked=$(readlink -f "$self" 2>/dev/null) && self=$linked',
        `bindir=$(CDPATH= cd "$(dirname "$self")" 2>/dev/null && pwd -P) || {`,
        `    printf '${tool}: cannot enter the directory this launcher sits in (%s).\\n' "$(dirname "$self")" >&2`,
        '    exit 1',
        '}',
        `binary=$bindir/${realName}`,
        `if [ ! -x "$binary" ]; then`,
        `    printf '${tool}: this launcher starts %s, and that file is missing or not executable; the install is incomplete -- install it again.\\n' "$binary" >&2`,
        '    exit 1',
        'fi',
        '',
        '# The highest GLIBCXX_3.4.N in a blob of text (a library\'s dynamic strings, or the loader\'s',
        '# complaint). awk and not `sort -V`: the install is read on whatever the distribution ships.',
        'highest_glibcxx() {',
        `    tr -cs 'A-Za-z0-9_.' '\\n' |`,
        `        grep '^GLIBCXX_3\\.4\\.[0-9][0-9]*$' |`,
        '        awk -F. \'BEGIN { top = -1 } { if ($3 + 0 > top) top = $3 + 0 } END { if (top >= 0) printf "GLIBCXX_3.4.%d\\n", top }\'',
        '}',
        '',
        `interpreter=${interpreter === null ? '' : interpreter}`,
        'if [ -z "$interpreter" ] || [ ! -x "$interpreter" ]; then',
        // Without the loader this file names, the launcher has no second opinion to offer; a run
        // that cannot be checked is still a run that may work, so it is not refused.
        '    interpreter=$(ls -1 /lib*/ld-linux*.so.2 2>/dev/null | head -n 1 || :)',
        'fi',
        'if [ -n "$interpreter" ] && [ -x "$interpreter" ]; then',
        '    report=$("$interpreter" --list "$binary" 2>&1 >/dev/null || :)',
        '    case $report in',
        '    *GLIBCXX_*"not found"*)',
        '        need=$(printf \'%s\\n\' "$report" | highest_glibcxx)',
        '        provider=$(printf \'%s\\n\' "$report" | awk \'/GLIBCXX_/ && /not found/ { sub(/^[^:]*: /, ""); sub(/: version .*$/, ""); print; exit }\')',
        '        if [ -z "$provider" ]; then',
        `            provider=$bindir/../libs/${CXX_RUNTIME_LIBRARY}`,
        '        else',
        '            resolved=$(readlink -f "$provider" 2>/dev/null) && provider=$resolved',
        '        fi',
        '        have=$(highest_glibcxx < "$provider" 2>/dev/null || :)',
        '        if [ -z "$have" ]; then',
        '            have="has no GLIBCXX_3.4 version at all"',
        '        else',
        '            have="has at most $have"',
        '        fi',
        '        libsdir=$bindir/../libs',
        '        real_libsdir=$(CDPATH= cd "$libsdir" 2>/dev/null && pwd -P) && libsdir=$real_libsdir',
        `        printf '${tool}: cannot start: this install needs the C++ runtime %s, and %s %s.\\n' "$need" "$provider" "$have" >&2`,
        `        printf '${tool}: put a ${CXX_RUNTIME_LIBRARY} that has %s (the one the toolchain that built this install ships) into %s, and run this again.\\n' "$need" "$libsdir" >&2`,
        '        exit 1',
        '        ;;',
        '    esac',
        'fi',
        '',
        'exec "$binary" "$@"',
        ''
    ].join('\n')
}

/**
 * Turn `bin/<tool>` into the launcher plus `<tool>.real`, the binary it starts.
 *
 * Returns `null` when there is nothing to wrap: not Linux (the guard is about `libstdc++` and the
 * ELF loader only), no such file, or a file this cannot identify as an ELF. A caller that gets
 * `null` keeps the install exactly as it was.
 */
export function installCxxRuntimeGuard(binDir, tool = GUARDED_TOOL) {
    if (process.platform !== 'linux') return null
    const binary = path.join(binDir, tool)
    if (!fs.existsSync(binary)) return null
    const interpreter = elfInterpreter(binary)
    if (interpreter === null) return null
    const realName = `${tool}.real`
    const real = path.join(binDir, realName)
    fs.renameSync(binary, real)
    fs.writeFileSync(binary, launcherText({ tool, realName, interpreter }), { mode: 0o755 })
    fs.chmodSync(binary, 0o755)
    return { launcher: binary, binary: real, interpreter }
}