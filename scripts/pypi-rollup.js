// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import fs from 'fs'
import {
    runCommand,
    copyDir,
    removeDir,
    BASEDIR,
    executableName,
    libName,
    logStep,
    logDone,
    logFail,
    logWarn,
    outArtifactRoot,
    outArtifactBin
} from './common.js'
import build, {
    findInterpreterDevelopmentLayout,
    probeCompilePythonInterpreter,
    readSdkPythonVersionFromHeaders,
    resolveRollupPythonExecutable,
    rollupMmFromVersion
} from './build.js'
import { spawnSync } from 'child_process'

/** `fs-extra`'s ensureDirSync is one call to `fs`; keeping it out lets this script run on a
 *  checkout that has the build but no `node_modules`, which is where a local wheel is checked. */
function ensureDirSync(dir) {
    fs.mkdirSync(dir, { recursive: true })
}

/**
 * PyPI packaging: for each SDK under modules/python/sdks/, calls build({ rollupMm, ... }) so CMake sees
 * CAMEL_PYPI_SINGLE_SDK_MM and emits only that py_bridge + matching python.cmo for one wheel.
 *
 * Default: one wheel for the **current compile-time** `python` (or `CAMEL_PYPI_PYTHON`), matching an SDK by
 * `include/patchlevel.h`. Set `CAMEL_PYPI_ALL_SDKS=1` to build one wheel per SDK folder (needs each runtime).
 *
 * Normal development: use `npm run build` only — never sets CAMEL_PYPI_SINGLE_SDK_MM, so every SDK folder
 * gets its own py_bridge* and the usual multi-bridge layout.
 */

const isWin = process.platform === 'win32'

/** Tools that are built for the test suite only and are not part of a release. */
const TEST_ONLY_TOOLS = ['camel-parsecheck']

function isTestOnlyTool(fileName) {
    return TEST_ONLY_TOOLS.includes(fileName.replace(/[.]exe$/i, ''))
}

/**
 * Basenames of tool binaries under collect-out bin/ (same rules as collect-out: skip .o/.pdb etc. on Unix).
 * @param {string} binDir
 * @returns {string[]}
 */
function listToolBinaries(binDir) {
    if (!fs.existsSync(binDir)) {
        return []
    }
    const names = []
    for (const ent of fs.readdirSync(binDir, { withFileTypes: true })) {
        if (!ent.isFile()) {
            continue
        }
        const f = ent.name
        if (isTestOnlyTool(f)) {
            continue
        }
        if (isWin) {
            if (f.endsWith('.exe')) {
                names.push(f)
            }
        } else if (!/\.(o|a|obj|d|pdb)$/i.test(f)) {
            names.push(f)
        }
    }
    return names.sort()
}

/** The commands the wheel promises, whatever `pyproject.toml` says (5.37 item 2). */
const TOOLS_THE_WHEEL_PROMISES = ['camel', 'camel-ls', 'camel-db', 'camel-format']

/**
 * The `[project.scripts]` command names in `pyproject.toml`, in declaration order.
 * @param {string} pyprojectPath
 * @returns {string[]}
 */
function declaredEntryPoints(pyprojectPath) {
    const text = fs.readFileSync(pyprojectPath, 'utf-8')
    const after = text.split(/^\[project\.scripts\]\s*$/m)[1]
    if (after === undefined) {
        return []
    }
    const body = after.split(/^\[/m)[0]
    const names = []
    for (const line of body.split(/\r?\n/)) {
        const m = line.match(/^([A-Za-z0-9_.-]+)\s*=\s*"[^"]+"/)
        if (m) {
            names.push(m[1])
        }
    }
    return names
}

/**
 * Check that every command the wheel declares has its binary in the bundled install.
 *
 * Each `[project.scripts]` entry point is a Python function that execs
 * `camel/.cmlenv/bin/<command>` (pypi/camel-lang/src/camel_cli.py), so a declared command whose
 * binary the build did not collect installs a command that fails on first use. Failing here
 * names the tool and the directory, rather than leaving it to the smoke test on one platform.
 *
 * Which mode those binaries carry in the wheel is set later, on the wheel itself
 * (scripts/wheel_modes.py): setuptools records 0o644 for package data whatever the source
 * mode is, so `chmod` before the build would not travel.
 *
 * @param {string} pyprojectBase - pypi/camel-lang, which holds pyproject.toml
 * @param {string} bundledBin - camel/.cmlenv/bin inside the package being built
 * @param {string[]} toolBinaries - the tool names copied there
 */
function prepareBundledBinaries(pyprojectBase, bundledBin, toolBinaries) {
    const declared = declaredEntryPoints(path.join(pyprojectBase, 'pyproject.toml'))
    if (declared.length === 0) {
        logFail(
            `pypi/camel-lang/pyproject.toml declares no [project.scripts]: the wheel would install no command`
        )
        process.exit(1)
    }

    const missing = []
    for (const tool of new Set([...declared, ...TOOLS_THE_WHEEL_PROMISES])) {
        if (!fs.existsSync(path.join(bundledBin, tool + (isWin ? '.exe' : '')))) {
            missing.push(tool)
        }
    }
    if (missing.length > 0) {
        const present = fs.existsSync(bundledBin)
            ? fs.readdirSync(bundledBin).sort().join(', ') || '(empty)'
            : '(no bin directory)'
        logFail(
            `No bundled binary for: ${missing.join(', ')}. Every [project.scripts] command runs ` +
                `camel/.cmlenv/bin/<command>, so the build has to have collected it. ` +
                `Found under ${bundledBin}: ${present}`
        )
        process.exit(1)
    }
    const entryPoints = [...declared].sort()
    logStep(`Entry points ready: ${entryPoints.join(', ')} (collected: ${toolBinaries.join(', ')})`)
}

/**
 * Run a tiny Python probe with the given interpreter and return its trimmed stdout.
 * @param {string} pyExe - absolute path or `python` / `python3`
 * @param {string} snippet
 * @param {string} what - named in the failure, so the message says what was being asked
 */
function runPythonProbe(pyExe, snippet, what) {
    const shell = !path.isAbsolute(pyExe) && (pyExe === 'python' || pyExe === 'python3') && isWin
    const r = spawnSync(pyExe, ['-c', snippet], {
        encoding: 'utf-8',
        shell,
        stdio: ['ignore', 'pipe', 'pipe']
    })
    if (r.error || r.status !== 0) {
        logFail(
            `Could not resolve ${what} (${pyExe}): ${r.stderr || r.error?.message || 'non-zero exit'}`
        )
        process.exit(1)
    }
    return r.stdout.trim()
}

/**
 * The platform tag a wheel built by this interpreter on this machine may claim *before*
 * `auditwheel repair` / `delocate-wheel` have looked at it.
 *
 * `sysconfig.get_platform()` is exactly that tag: `linux_x86_64`, `macosx_13_0_arm64`,
 * `win_amd64`. It replaces the wheel tag the rollup used to read out of
 * `packaging.tags.sys_tags()`: that returns a *list*, and which manylinux tag (if any) comes
 * first depends on the `packaging` version -- 21.3 yields `manylinux_2_35_x86_64`, 26.3 yields
 * `linux_x86_64` -- so taking its first entry made the tag a property of the machine's
 * `packaging` rather than of the binaries. PyPI rejected the 0.2.31 upload for exactly that
 * (`400 ... has an unsupported platform tag 'linux_x86_64'`, 5.37). Linux therefore claims
 * `linux_<arch>` here and gets its publishable tag from `auditwheel repair`, which is the only
 * thing that knows which glibc the bundled libraries actually need; `scripts/check_wheel_tags.py`
 * fails the build if that repair did not happen.
 *
 * @param {string} pyExe
 */
function nativePlatformTag(pyExe) {
    const forced = (process.env.CAMEL_PYPI_PLATFORM_TAG || '').trim()
    if (forced) {
        return forced
    }
    const platform = runPythonProbe(
        pyExe,
        'import sysconfig;print(sysconfig.get_platform())',
        'sysconfig.get_platform()'
    )
    return platform.replace(/[-.]/g, '_')
}

/**
 * PEP 425 tags for native code linked against the given interpreter.
 * @param {string} pyExe - absolute path or `python` / `python3`
 */
function getWheelTagsFromPythonExe(pyExe) {
    // `cp310`, from the interpreter that the extension is compiled and linked against. It is
    // read from `sys.version_info` rather than from `packaging.tags` for the reason in
    // `nativePlatformTag`.
    const pythonTag = runPythonProbe(
        pyExe,
        'import sys;print("cp%d%d" % sys.version_info[:2])',
        'the interpreter tag'
    )
    return { pythonTag, abiTag: pythonTag, platformTag: nativePlatformTag(pyExe) }
}

/**
 * Pick one `sdks/python*` tree whose patchlevel.h matches the compile-time interpreter.
 * @param {{ name: string, root: string, mm: string }[]} sdks
 * @param {number} major
 * @param {number} minor
 */
function findSdkMatchingCompilePython(sdks, major, minor) {
    const matches = []
    for (const s of sdks) {
        const h = readSdkPythonVersionFromHeaders(s.root)
        if (h && h[0] === major && h[1] === minor) {
            matches.push(s)
        }
    }
    if (matches.length === 0) {
        return null
    }
    matches.sort((a, b) => a.mm.localeCompare(b.mm, undefined, { numeric: true }))
    if (matches.length > 1) {
        logWarn(
            `Multiple SDK trees match Python ${major}.${minor}; using ${matches[0].name} (folder suffix "${matches[0].mm}")`
        )
    }
    return matches[0]
}

const SDKS_ROOT = path.join(BASEDIR, 'modules', 'python', 'sdks')

/** @returns {{ name: string, root: string, mm: string }[]} */
function listValidPythonSdks() {
    if (!fs.existsSync(SDKS_ROOT)) {
        return []
    }
    const out = []
    for (const name of fs.readdirSync(SDKS_ROOT)) {
        if (!/^python3\d+$/.test(name)) {
            continue
        }
        const root = path.join(SDKS_ROOT, name)
        const inc = path.join(root, 'include', 'Python.h')
        const libs = path.join(root, 'libs')
        if (!fs.existsSync(inc) || !fs.existsSync(libs)) {
            continue
        }
        const mm = name.replace(/^python/, '')
        out.push({ name, root, mm })
    }
    return out.sort((a, b) => a.mm.localeCompare(b.mm, undefined, { numeric: true }))
}

function sdkPythonExecutable(sdkRoot) {
    const cands = [
        path.join(sdkRoot, 'python.exe'),
        path.join(sdkRoot, 'bin', 'python3'),
        path.join(sdkRoot, 'bin', 'python')
    ]
    for (const p of cands) {
        if (fs.existsSync(p)) {
            return p
        }
    }
    return ''
}

/** Quote for shell if path is not a bare `python` / `python3`. */
function pyCmd(exe) {
    if (exe === 'python' || exe === 'python3') {
        return exe
    }
    return `"${exe.replace(/"/g, '\\"')}"`
}

function envFlag(name) {
    const value = (process.env[name] || '').toLowerCase()
    return value === '1' || value === 'true' || value === 'yes'
}

const SKIP_BUILD = envFlag('CAMEL_PYPI_SKIP_BUILD')

/**
 * The rollup's build step, with an escape for a machine that has already built the tree.
 *
 * With CAMEL_PYPI_SKIP_BUILD=1 the install collected in out/<tag>/ is rolled up exactly as it
 * stands. The content of the wheel does not come from the rebuild -- it is copied out of
 * out/<tag>/ -- so a local check of a wheel on a machine that cannot reach the package index,
 * and therefore cannot install the `build` module either, still produces the same artifact from
 * a tree that `npm run build` already built. CI never sets it, and every other run takes the
 * path it took before.
 */
function runBuild(options) {
    if (SKIP_BUILD) {
        logStep('CAMEL_PYPI_SKIP_BUILD=1: rolling up the out/<tag>/ that is already collected')
        return
    }
    build(options)
}

/**
 * Copy out/<tag>/ into the package, run setuptools + wheel tags, append wheels to dist/pypi/.
 */
function packageWheelBundle({
    outTag,
    pythonTag,
    abiTag,
    platformTag,
    buildPythonExe,
    pypiDist
}) {
    const OUT_ROOT = outArtifactRoot(outTag)
    if (!fs.existsSync(OUT_ROOT)) {
        logFail(`Collect-out directory not found: ${OUT_ROOT}`)
        process.exit(1)
    }

    const binSrc = outArtifactBin(outTag)
    const toolBinaries = listToolBinaries(binSrc)
    const exePath = path.join(binSrc, executableName)
    if (!fs.existsSync(exePath)) {
        logFail(`Camel executable not found (run collect-out after build): ${exePath}`)
        process.exit(1)
    }
    if (toolBinaries.length === 0) {
        logFail(`No tool binaries found under: ${binSrc}`)
        process.exit(1)
    }

    const PYPI_BASE = path.join(BASEDIR, 'pypi', 'camel-lang')
    const CAMEL_BASE = path.join(PYPI_BASE, 'src', 'camel')
    const CMLENV = path.join(CAMEL_BASE, '.cmlenv')

    logStep('Copying files to pypi directory')
    removeDir(CMLENV)
    copyDir(OUT_ROOT, CMLENV)
    // The build collects every tool into out/; the ones the tests alone use stay out of the wheel,
    // and so does the link product `listToolBinaries` already refuses to call a tool -- shipping
    // an object file beside the binaries would only put a non-executable file where the entry
    // points look, and `scripts/wheel_modes.py` would set its executable bit.
    const bundledBin = path.join(CMLENV, 'bin')
    if (fs.existsSync(bundledBin)) {
        for (const f of fs.readdirSync(bundledBin)) {
            if (isTestOnlyTool(f) || /\.(o|a|obj|d|pdb)$/i.test(f)) {
                fs.rmSync(path.join(bundledBin, f))
            }
        }
    }

    const libInCmlenv = path.join(CMLENV, 'libs', libName)
    if (!fs.existsSync(libInCmlenv)) {
        logFail(`After copying out/${outTag}, expected library missing: ${libInCmlenv}`)
        process.exit(1)
    }

    prepareBundledBinaries(PYPI_BASE, bundledBin, toolBinaries)

    // A run that failed left its `build/` and `dist/` behind, and both are read below: the wheel
    // is copied out of `dist/`, so a stale one from an earlier run would be published beside the
    // new one. Start from neither.
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(path.join(PYPI_BASE, 'dist'))

    const py = pyCmd(buildPythonExe)

    // Every target that links libcamel carries the build machine in its RUNPATH: CMake adds the
    // build tree's library directory (`...:$ORIGIN/../libs:/src/build/Release`), and the Conan
    // packages the build linked against add their cache (`/root/.conan2/p/b/fmt.../p/lib`). Both
    // are dead on the user's machine -- the install is `$ORIGIN`-relative, and `auditwheel
    // lddtree` shows the only library anything in the wheel needs from outside itself is the
    // system's -- and the build tree's entry is what `auditwheel repair` follows to find
    // libcamel.so, which is how the wheel ended up carrying a second copy of it (H604). Dropped
    // from the copy the wheel is packed from, so the build tree itself is untouched and stays
    // runnable: on Linux only, because this edits the ELF dynamic string table, and the other
    // platforms have no such entry (Windows has no RUNPATH; a macOS Mach-O is rewritten by
    // delocate, which 5.37 item 6 has not written yet).
    if (process.platform === 'linux') {
        runCommand(`${py} "${path.join(BASEDIR, 'scripts', 'strip_build_rpath.py')}" "${CMLENV}"`)
        runCommand(
            `${py} "${path.join(BASEDIR, 'scripts', 'strip_build_rpath.py')}" --check "${CMLENV}"`
        )
    }
    // The wheel is built with `build` where it is installable, and with the setuptools that pip can
    // already see where it is not; `wheel` is not needed at all, since scripts/wheel_retag.py does
    // the retag with the standard library. A host with no egress gets the same artifact.
    spawnSync(`${py} -m pip install -q --disable-pip-version-check build packaging`, {
        stdio: 'ignore',
        shell: true
    })
    // `-I` keeps this directory off `sys.path`: a `build/` left by an interrupted run would
    // otherwise be imported as the `build` module, and `python -m build` would fail on it.
    const hasBuildModule =
        spawnSync(`${py} -I -c "import build.__main__"`, {
            stdio: 'ignore',
            shell: true
        }).status === 0

    process.chdir(PYPI_BASE)
    // One native build per platform, two wheels off it (5.37 item 7): the interpreter cannot
    // reach the binaries, so the wheel does not have to be rebuilt per Python version.
    //   camel_lang-<ver>-<cp3XX>-<cp3XX>-<platform>.whl  the body *and* the interop extension;
    //   camel_lang-<ver>-py3-none-<platform>.whl         the body only, any Python 3.
    // `.cmlenv/python/` is the only part that is interpreter-specific, so the second wheel is
    // the first one without it.
    const BUNDLES = [
        { pythonTag, abiTag, platformTag, withExtension: true },
        { pythonTag: 'py3', abiTag: 'none', platformTag, withExtension: false }
    ]
    ensureDirSync(pypiDist)
    for (const bundle of BUNDLES) {
        const extensionDir = path.join(CMLENV, 'python')
        const stashedExtension = path.join(PYPI_BASE, 'python.stash')
        removeDir(stashedExtension)
        if (bundle.withExtension) {
            if (!fs.existsSync(extensionDir)) {
                logFail(
                    `After copying out/${outTag}, the interop extension directory is missing: ` +
                        `${extensionDir}. The interop wheel has to carry it; if the build stopped ` +
                        `collecting out/${outTag}/python/, the two-wheel split is wrong.`
                )
                process.exit(1)
            }
        } else {
            // Moved aside rather than deleted: the next bundle needs it back.
            fs.renameSync(extensionDir, stashedExtension)
        }

        // Both `dist/` and `build/` are cleared before each bundle: setuptools stages the package
        // data it will pack under `build/lib/...`, and `pip wheel --no-build-isolation` reuses
        // that staging directory. Leaving it meant the body-only wheel was packed from the tree
        // the *interop* bundle had staged, and so carried `.cmlenv/python/` -- with a `py3-none`
        // name that promises it does not.
        removeDir(path.join(PYPI_BASE, 'dist'))
        removeDir(path.join(PYPI_BASE, 'build'))
        logStep(`Building wheel (stub py3-none-any, then retag to ${bundle.pythonTag}-${bundle.abiTag}-${bundle.platformTag})`)
        if (hasBuildModule) {
            runCommand(`${py} -I -m build --wheel`)
        } else {
            logWarn(
                'the `build` module is not installed and could not be fetched; ' +
                    'building the wheel with `pip wheel --no-build-isolation` instead'
            )
            runCommand(`${py} -m pip wheel --no-build-isolation --no-deps -w dist .`)
        }

        const distDir = path.join(PYPI_BASE, 'dist')
        const anyWheels = fs.readdirSync(distDir).filter((f) => f.endsWith('-any.whl'))
        if (anyWheels.length === 0) {
            logFail(`No *-any.whl found in ${distDir} after build`)
            process.exit(1)
        }
        // The retag is a rename plus the `Tag:` lines in `*.dist-info/WHEEL`, and wheel 0.45
        // removed the command line that used to do it, so the repository does it itself with
        // `zipfile` (scripts/wheel_retag.py). A wheel whose name says `py3-none-any` would be
        // installable on an interpreter and a machine it cannot run on, which is exactly what
        // this prevents.
        const retagger = path.join(BASEDIR, 'scripts', 'wheel_retag.py')
        for (const w of anyWheels) {
            const wheelPath = path.join('dist', w).split(path.sep).join('/')
            runCommand(
                `${py} "${retagger}" "${wheelPath}" ${bundle.pythonTag} ${bundle.abiTag} ${bundle.platformTag}`
            )
        }
        // The retag rewrites the archive, so the modes are set after it: the console entry
        // points exec these binaries and setuptools recorded 0o644 for all of them.
        const modeFixer = path.join(BASEDIR, 'scripts', 'wheel_modes.py')
        for (const f of fs.readdirSync(distDir).filter((f) => f.endsWith('.whl'))) {
            const wheelPath = path.join('dist', f).split(path.sep).join('/')
            runCommand(`${py} "${modeFixer}" "${wheelPath}"`)
            runCommand(`${py} "${modeFixer}" --check "${wheelPath}"`)
        }
        for (const f of fs.readdirSync(distDir)) {
            if (f.endsWith('.whl')) {
                const target = path.join(pypiDist, f)
                if (fs.existsSync(target)) {
                    logFail(`Two bundles produced the same wheel name: ${f}`)
                    process.exit(1)
                }
                fs.copyFileSync(path.join(distDir, f), target)
            }
        }

        if (!bundle.withExtension) {
            fs.renameSync(stashedExtension, extensionDir)
        }
    }
    process.chdir(BASEDIR)

    removeDir(path.join(PYPI_BASE, 'dist'))
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(CMLENV)
    removeDir(path.join(PYPI_BASE, 'python.stash'))
    removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
}

function main() {
    process.chdir(BASEDIR)
    const PYPI_DIST = path.join(BASEDIR, 'dist', 'pypi')
    ensureDirSync(PYPI_DIST)
    for (const f of fs.readdirSync(PYPI_DIST)) {
        if (f.endsWith('.whl')) {
            fs.unlinkSync(path.join(PYPI_DIST, f))
        }
    }

    const outTag = process.env.CAMEL_PYPI_TAG || 'latest'
    const sdks = listValidPythonSdks()
    const allSdksRollup =
        process.env.CAMEL_PYPI_ALL_SDKS === '1' ||
        process.env.CAMEL_PYPI_ALL_SDKS === 'true' ||
        process.env.CAMEL_PYPI_ALL_SDKS === 'yes'

    if (sdks.length > 0 && allSdksRollup) {
        logStep(`PyPI rollup: CAMEL_PYPI_ALL_SDKS — ${sdks.length} SDK folder(s), one wheel each`)
        for (const sdk of sdks) {
            logStep(`--- Build + wheel for SDK python${sdk.mm} (${sdk.root}) ---`)
            const sdkExe = sdkPythonExecutable(sdk.root)
            let pyExe
            try {
                pyExe = resolveRollupPythonExecutable(sdk.root, sdk.mm, sdkExe || undefined)
            } catch (e) {
                logFail(e instanceof Error ? e.message : String(e))
                process.exit(1)
            }
            const tags = getWheelTagsFromPythonExe(pyExe)
            runBuild({
                rollupMm: sdk.mm,
                pythonRootDir: sdk.root,
                pythonExecutable: pyExe,
                cleanBuildDir: true
            })
            process.chdir(BASEDIR)
            packageWheelBundle({
                outTag,
                pythonTag: tags.pythonTag,
                abiTag: tags.abiTag,
                platformTag: tags.platformTag,
                buildPythonExe: pyExe,
                pypiDist: PYPI_DIST
            })
        }
        const finals = fs.readdirSync(PYPI_DIST).filter((f) => f.endsWith('.whl'))
        logDone(`Final wheel(s) in dist/pypi/: ${finals.join(', ')}`)
        return
    }

    if (sdks.length > 0) {
        let compile
        try {
            compile = probeCompilePythonInterpreter()
        } catch (e) {
            logFail(e instanceof Error ? e.message : String(e))
            process.exit(1)
        }
        const sdk = findSdkMatchingCompilePython(sdks, compile.major, compile.minor)
        const rollupMm = sdk ? sdk.mm : rollupMmFromVersion(compile.major, compile.minor)
        let pyExe
        let tags
        if (sdk) {
            logStep(
                `PyPI rollup: Python ${compile.major}.${compile.minor} (${compile.executable}) → ` +
                    `py_bridge from ${sdk.name}`
            )
            try {
                pyExe = resolveRollupPythonExecutable(sdk.root, sdk.mm, compile.executable)
            } catch (e) {
                logFail(e instanceof Error ? e.message : String(e))
                process.exit(1)
            }
            tags = getWheelTagsFromPythonExe(pyExe)
            runBuild({
                rollupMm: sdk.mm,
                pythonRootDir: sdk.root,
                pythonExecutable: pyExe,
                cleanBuildDir: true
            })
        } else {
            const layout = findInterpreterDevelopmentLayout(compile.executable)
            if (!layout) {
                logFail(
                    `Compile-time Python is ${compile.major}.${compile.minor} (${compile.executable}), but no ` +
                        `matching modules/python/sdks/python* and no full interpreter dev tree (headers/libs under ` +
                        `sys.base_prefix / sysconfig). Install CPython with dev files, run sync-python-sdks, or ` +
                        `set CAMEL_PYPI_ALL_SDKS=1.`
                )
                process.exit(1)
            }
            logStep(
                `PyPI rollup: Python ${compile.major}.${compile.minor} — using interpreter install at ${layout.root} ` +
                    `(no matching sdks/python*; venv/base_prefix headers+libs)`
            )
            try {
                pyExe = resolveRollupPythonExecutable(layout.root, rollupMm, compile.executable)
            } catch (e) {
                logFail(e instanceof Error ? e.message : String(e))
                process.exit(1)
            }
            tags = getWheelTagsFromPythonExe(pyExe)
            runBuild({
                rollupMm,
                pythonRootDir: layout.root,
                pythonExecutable: pyExe,
                cleanBuildDir: true
            })
        }
        process.chdir(BASEDIR)
        packageWheelBundle({
            outTag,
            pythonTag: tags.pythonTag,
            abiTag: tags.abiTag,
            platformTag: tags.platformTag,
            buildPythonExe: pyExe,
            pypiDist: PYPI_DIST
        })
        const finals = fs.readdirSync(PYPI_DIST).filter((f) => f.endsWith('.whl'))
        logDone(`Final wheel(s) in dist/pypi/: ${finals.join(', ')}`)
        return
    }

    logWarn('No modules/python/sdks/python* trees — linking py_bridge from current interpreter (sys.base_prefix)')
    let compile
    try {
        compile = probeCompilePythonInterpreter()
    } catch (e) {
        logFail(e instanceof Error ? e.message : String(e))
        process.exit(1)
    }
    const layout = findInterpreterDevelopmentLayout(compile.executable)
    let pyExe = compile.executable
    let tags = getWheelTagsFromPythonExe(pyExe)
    if (layout) {
        const rollupMm = rollupMmFromVersion(compile.major, compile.minor)
        try {
            pyExe = resolveRollupPythonExecutable(layout.root, rollupMm, compile.executable)
        } catch (e) {
            logFail(e instanceof Error ? e.message : String(e))
            process.exit(1)
        }
        tags = getWheelTagsFromPythonExe(pyExe)
            runBuild({
            rollupMm,
            pythonRootDir: layout.root,
            pythonExecutable: pyExe,
            cleanBuildDir: true
        })
    } else {
        runBuild({ cleanBuildDir: false })
    }
    process.chdir(BASEDIR)
    packageWheelBundle({
        outTag,
        pythonTag: tags.pythonTag,
        abiTag: tags.abiTag,
        platformTag: tags.platformTag,
        buildPythonExe: pyExe,
        pypiDist: PYPI_DIST
    })
    const finals = fs.readdirSync(PYPI_DIST).filter((f) => f.endsWith('.whl'))
    logDone(`Final wheel(s) in dist/pypi/: ${finals.join(', ')}`)
}

main()
