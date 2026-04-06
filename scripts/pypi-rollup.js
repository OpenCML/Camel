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
import { ensureDirSync } from 'fs-extra/esm'
import build, {
    findInterpreterDevelopmentLayout,
    probeCompilePythonInterpreter,
    readSdkPythonVersionFromHeaders,
    resolveRollupPythonExecutable,
    rollupMmFromVersion
} from './build.js'
import { spawnSync } from 'child_process'

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

/** Windows: one .cmd per exe in Scripts/ → ..\\site-packages\\camel\\.cmlenv\\bin\\<same>.exe */
function writeWindowsWrappers(wrapperDir, exeFileNames) {
    for (const exeName of exeFileNames) {
        const cmdName = `${path.parse(exeName).name}.cmd`
        const lines = [
            '@echo off',
            'REM Delegates to camel-lang wheel bundled .cmlenv\\bin (standard venv / prefix layout).',
            `"%~dp0..\\Lib\\site-packages\\camel\\.cmlenv\\bin\\${exeName}" %*`,
            ''
        ]
        fs.writeFileSync(path.join(wrapperDir, cmdName), lines.join('\r\n'), 'utf-8')
    }
}

/** Shell-safe single-quoted literal for POSIX sh */
function shSingleQuoted(s) {
    return `'${String(s).replace(/'/g, `'\\''`)}'`
}

/** Unix: one sh launcher per tool in env bin/ → site-packages/camel/.cmlenv/bin/<name> */
function writeUnixWrappers(wrapperDir, binaryNames) {
    for (const binName of binaryNames) {
        const q = shSingleQuoted(binName)
        const script = `#!/usr/bin/env sh
# Delegates to camel-lang wheel bundled .cmlenv/bin/${binName}
_TARGET=${q}
_BINDIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
for _bin in "$_BINDIR"/../lib/python*/site-packages/camel/.cmlenv/bin/"$_TARGET"; do
  if [ -f "$_bin" ] && [ -x "$_bin" ]; then
    exec "$_bin" "$@"
  fi
done
echo "camel-lang (${binName}): bundled binary not found. Set CAMEL_HOME to site-packages/camel/.cmlenv" >&2
exit 1
`
        const shPath = path.join(wrapperDir, binName)
        fs.writeFileSync(shPath, script, 'utf-8')
        try {
            fs.chmodSync(shPath, 0o755)
        } catch {
            /* non-Unix or permission noop */
        }
    }
}

/**
 * PEP 425 tags for native code linked against the given interpreter (same rules as pip).
 * @param {string} pyExe - absolute path or `python` / `python3`
 */
function getWheelTagsFromPythonExe(pyExe) {
    const snippet = `from packaging.tags import sys_tags
_t = next(iter(sys_tags()))
print(_t.interpreter)
print(_t.abi)
print(_t.platform)
`
    const shell = !path.isAbsolute(pyExe) && (pyExe === 'python' || pyExe === 'python3') && isWin
    const r = spawnSync(pyExe, ['-c', snippet], {
        encoding: 'utf-8',
        shell,
        stdio: ['ignore', 'pipe', 'pipe']
    })
    if (r.error || r.status !== 0) {
        logFail(
            `Could not resolve wheel tags via packaging.tags (${pyExe}): ${r.stderr || r.error?.message || 'non-zero exit'}`
        )
        process.exit(1)
    }
    const lines = r.stdout
        .trim()
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter(Boolean)
    if (lines.length < 3) {
        logFail(`Unexpected packaging.tags output:\n${r.stdout}`)
        process.exit(1)
    }
    return { pythonTag: lines[0], abiTag: lines[1], platformTag: lines[2] }
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

    const libInCmlenv = path.join(CMLENV, 'libs', libName)
    if (!fs.existsSync(libInCmlenv)) {
        logFail(`After copying out/${outTag}, expected library missing: ${libInCmlenv}`)
        process.exit(1)
    }

    const WRAPPER_DIR = path.join(PYPI_BASE, 'wrapper-dist')
    fs.mkdirSync(WRAPPER_DIR, { recursive: true })
    if (isWin) {
        writeWindowsWrappers(WRAPPER_DIR, toolBinaries)
    } else {
        writeUnixWrappers(WRAPPER_DIR, toolBinaries)
    }

    const scriptFiles = fs
        .readdirSync(WRAPPER_DIR)
        .sort()
        .map((f) => `wrapper-dist/${f}`)
        .filter((p) => fs.existsSync(path.join(PYPI_BASE, p)))
    const pyprojectPath = path.join(PYPI_BASE, 'pyproject.toml')
    const pyprojectOriginal = fs.readFileSync(pyprojectPath, 'utf-8')
    const scriptFilesStr = scriptFiles.map((s) => `"${s.replace(/\\/g, '/')}"`).join(', ')
    fs.writeFileSync(
        pyprojectPath,
        pyprojectOriginal.replace(/script-files = \[[^\]]*\]/, `script-files = [${scriptFilesStr}]`)
    )
    logStep(`Configured script-files: [${scriptFiles.join(', ')}]`)

    const py = pyCmd(buildPythonExe)
    logStep('Building wheel (stub py3-none-any, then retag)')
    process.chdir(PYPI_BASE)
    runCommand(`${py} -m pip install -q build wheel packaging`)
    runCommand(`${py} -m build --wheel`)

    const distDir = path.join(PYPI_BASE, 'dist')
    const anyWheels = fs.readdirSync(distDir).filter((f) => f.endsWith('-any.whl'))
    if (anyWheels.length === 0) {
        logFail(`No *-any.whl found in ${distDir} after build`)
        process.exit(1)
    }
    logStep(`Retagging wheel(s): ${pythonTag}-${abiTag}-${platformTag}`)
    for (const w of anyWheels) {
        const wheelPath = path.join('dist', w).split(path.sep).join('/')
        runCommand(
            `${py} -m wheel tags --python-tag ${pythonTag} --abi-tag ${abiTag} --platform-tag ${platformTag} --remove "${wheelPath}"`
        )
    }

    ensureDirSync(pypiDist)
    for (const f of fs.readdirSync(distDir)) {
        if (f.endsWith('.whl')) {
            fs.copyFileSync(path.join(distDir, f), path.join(pypiDist, f))
        }
    }

    fs.writeFileSync(pyprojectPath, pyprojectOriginal)
    removeDir(path.join(PYPI_BASE, 'dist'))
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(CMLENV)
    removeDir(WRAPPER_DIR)
    removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
    process.chdir(BASEDIR)
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
            build({
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
            build({
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
            build({
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
        build({
            rollupMm,
            pythonRootDir: layout.root,
            pythonExecutable: pyExe,
            cleanBuildDir: true
        })
    } else {
        build({ cleanBuildDir: false })
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
