import { execSync, spawnSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'
import { runCmakeBuild, copyBuildArtifacts, logDone, logWarn, BASEDIR, removeDir } from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

const isWin = process.platform === 'win32'

/**
 * Map folder tag `python3xx` minor segment to (major, minor), e.g. 312→3.12, 39→3.9.
 * @param {string} mm
 * @returns {[number, number] | null}
 */
function versionTupleFromRollupMm(mm) {
    if (!/^\d+$/.test(mm)) {
        return null
    }
    const minor = parseInt(mm.slice(1), 10)
    if (!Number.isFinite(minor)) {
        return null
    }
    return [3, minor]
}

/**
 * Read PY_MAJOR_VERSION / PY_MINOR_VERSION from SDK include/patchlevel.h (authoritative for headers/libs).
 * @param {string} sdkRoot
 * @returns {[number, number] | null}
 */
/**
 * @param {string} sdkRoot - `sdks/python312` or a venv / base_prefix root
 */
export function readSdkPythonVersionFromHeaders(sdkRoot) {
    /** @type {string[]} */
    const patchCandidates = [
        path.join(sdkRoot, 'include', 'patchlevel.h'),
        path.join(sdkRoot, 'Include', 'patchlevel.h')
    ]
    try {
        const inc = path.join(sdkRoot, 'include')
        if (fs.existsSync(inc)) {
            for (const name of fs.readdirSync(inc)) {
                patchCandidates.push(path.join(inc, name, 'patchlevel.h'))
            }
        }
    } catch {
        /* ignore */
    }
    let patch = ''
    for (const p of patchCandidates) {
        if (fs.existsSync(p)) {
            patch = p
            break
        }
    }
    if (!patch) {
        return null
    }
    let text
    try {
        text = fs.readFileSync(patch, 'utf-8')
    } catch {
        return null
    }
    const maj = text.match(/#\s*define\s+PY_MAJOR_VERSION\s+(\d+)/)
    const min = text.match(/#\s*define\s+PY_MINOR_VERSION\s+(\d+)/)
    if (!maj || !min) {
        return null
    }
    const major = parseInt(maj[1], 10)
    const minor = parseInt(min[1], 10)
    if (!Number.isFinite(major) || !Number.isFinite(minor)) {
        return null
    }
    return [major, minor]
}

/**
 * Windows: resolve `py -X.Y` to a concrete python.exe via sys.executable (CMake needs a path).
 * @param {number} major
 * @param {number} minor
 * @returns {string | null}
 */
function executableFromWindowsPyLauncher(major, minor) {
    if (!isWin) {
        return null
    }
    const r = spawnSync(
        'py',
        [`-${major}.${minor}`, '-c', 'import sys; print(sys.executable)'],
        { encoding: 'utf-8', stdio: ['ignore', 'pipe', 'pipe'] }
    )
    if (r.error || r.status !== 0 || !r.stdout?.trim()) {
        return null
    }
    const p = r.stdout
        .trim()
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter(Boolean)[0]
    if (!p || !fs.existsSync(p)) {
        return null
    }
    const v = probePythonVersion(p)
    if (v && v[0] === major && v[1] === minor) {
        return p
    }
    return null
}

/**
 * @param {string} exePath - absolute path, or `python` / `python3` for PATH lookup
 * @returns {[number, number] | null}
 */
function probePythonVersion(exePath) {
    const code = 'import sys;print(sys.version_info[0], sys.version_info[1])'
    const args = ['-c', code]
    try {
        const shell = exePath === 'python' || exePath === 'python3' ? isWin : false
        const r = spawnSync(exePath, args, { encoding: 'utf-8', shell, stdio: ['ignore', 'pipe', 'pipe'] })
        if (r.error || r.status !== 0 || !r.stdout) {
            return null
        }
        const parts = r.stdout.trim().split(/\s+/)
        if (parts.length < 2) {
            return null
        }
        const major = parseInt(parts[0], 10)
        const minor = parseInt(parts[1], 10)
        if (!Number.isFinite(major) || !Number.isFinite(minor)) {
            return null
        }
        return [major, minor]
    } catch {
        return null
    }
}

/**
 * Resolve the **compile-time** interpreter: `CAMEL_PYPI_PYTHON`, then venv/conda concrete paths,
 * then `python` / `python3`. Venv paths first because on Windows `spawnSync('python')` often fails
 * (PATH not visible to CreateProcess, or App Execution Aliases / Store stub).
 *
 * @returns {{ executable: string, major: number, minor: number }}
 */
export function probeCompilePythonInterpreter() {
    /** @type {string[]} */
    const tryOrder = []
    const o = process.env.CAMEL_PYPI_PYTHON?.trim()
    if (o) {
        tryOrder.push(o)
    }
    if (isWin) {
        tryOrder.push(path.join(BASEDIR, '.venv', 'Scripts', 'python.exe'))
    } else {
        tryOrder.push(path.join(BASEDIR, '.venv', 'bin', 'python3'))
        tryOrder.push(path.join(BASEDIR, '.venv', 'bin', 'python'))
    }
    if (process.env.VIRTUAL_ENV) {
        if (isWin) {
            tryOrder.push(path.join(process.env.VIRTUAL_ENV, 'Scripts', 'python.exe'))
            tryOrder.push(path.join(process.env.VIRTUAL_ENV, 'Scripts', 'python3.exe'))
        } else {
            tryOrder.push(path.join(process.env.VIRTUAL_ENV, 'bin', 'python3'))
            tryOrder.push(path.join(process.env.VIRTUAL_ENV, 'bin', 'python'))
        }
    }
    if (process.env.CONDA_PREFIX) {
        if (isWin) {
            tryOrder.push(path.join(process.env.CONDA_PREFIX, 'python.exe'))
        } else {
            tryOrder.push(path.join(process.env.CONDA_PREFIX, 'bin', 'python3'))
            tryOrder.push(path.join(process.env.CONDA_PREFIX, 'bin', 'python'))
        }
    }
    tryOrder.push('python', 'python3')

    const tried = []
    const code = 'import sys\nprint(sys.executable)\nprint(sys.version_info[0], sys.version_info[1])'
    for (const exe of tryOrder) {
        if (!exe) {
            continue
        }
        if ((path.isAbsolute(exe) || exe.includes(path.sep)) && !fs.existsSync(exe)) {
            continue
        }
        tried.push(exe)
        const shell = !path.isAbsolute(exe) && (exe === 'python' || exe === 'python3') && isWin
        const r = spawnSync(exe, ['-c', code], {
            encoding: 'utf-8',
            shell,
            stdio: ['ignore', 'pipe', 'pipe']
        })
        if (r.error || r.status !== 0 || !r.stdout?.trim()) {
            continue
        }
        const lines = r.stdout
            .trim()
            .split(/\r?\n/)
            .map((l) => l.trim())
            .filter(Boolean)
        if (lines.length < 2) {
            continue
        }
        const executable = lines[0]
        const verParts = lines[lines.length - 1].split(/\s+/)
        if (verParts.length < 2) {
            continue
        }
        const major = parseInt(verParts[0], 10)
        const minor = parseInt(verParts[1], 10)
        if (!Number.isFinite(major) || !Number.isFinite(minor)) {
            continue
        }
        if (!executable || !fs.existsSync(executable)) {
            continue
        }
        return { executable, major, minor }
    }
    throw new Error(
        `Could not run compile-time Python (tried: ${tried.join(', ') || tryOrder.filter(Boolean).join(', ')}). ` +
            'Set CAMEL_PYPI_PYTHON to your python.exe, or ensure .venv/Scripts/python.exe (Windows) / .venv/bin/python3 (Unix) exists.'
    )
}

/** `py_bridge` / `CAMEL_PYPI_SINGLE_SDK_MM` suffix: 3.12 → `312`, 3.9 → `39`. */
export function rollupMmFromVersion(major, minor) {
    return `${major}${minor}`
}

/**
 * Include dir + import lib dir for embedding, from the running interpreter (venv uses `sys.base_prefix`).
 * @param {string} pythonExe
 * @returns {{ root: string, includeDir: string, libDir: string, dotted: string } | null}
 */
export function findInterpreterDevelopmentLayout(pythonExe) {
    const code = [
        'import os, sys, sysconfig',
        'from pathlib import Path',
        'inc = Path(sysconfig.get_path("include"))',
        'if not (inc / "Python.h").is_file():',
        '    raise SystemExit("no Python.h in " + str(inc))',
        'root = Path(sys.base_prefix).resolve()',
        'dotted = f"{sys.version_info[0]}.{sys.version_info[1]}"',
        'if os.name == "nt":',
        '    lib = root / "libs"',
        '    if not lib.is_dir():',
        '        lib = root / "Libs"',
        '    if not lib.is_dir():',
        '        raise SystemExit("no libs/Libs under " + str(root))',
        'else:',
        '    lib = Path(sysconfig.get_config_var("LIBDIR") or (root / "lib"))',
        '    if not lib.is_dir():',
        '        raise SystemExit("no LIBDIR: " + str(lib))',
        'print(root)',
        'print(inc.resolve())',
        'print(lib.resolve())',
        'print(dotted)',
    ].join('\n')
    const shell = !path.isAbsolute(pythonExe) && (pythonExe === 'python' || pythonExe === 'python3') && isWin
    const r = spawnSync(pythonExe, ['-c', code], {
        encoding: 'utf-8',
        shell,
        stdio: ['ignore', 'pipe', 'pipe']
    })
    if (r.error || r.status !== 0 || !r.stdout?.trim()) {
        return null
    }
    const lines = r.stdout
        .trim()
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter(Boolean)
    if (lines.length < 4) {
        return null
    }
    const root = lines[0]
    const includeDir = lines[1]
    const libDir = lines[2]
    const dotted = lines[3]
    if (!fs.existsSync(includeDir) || !fs.existsSync(libDir)) {
        return null
    }
    return { root, includeDir, libDir, dotted }
}

/** True if `pythonRootDir` is a `modules/python/sdks/python3xx` tree (CMake glob layout). */
function isUnderCamelPythonSdks(pythonRootDir) {
    const sdksRoot = path.resolve(BASEDIR, 'modules', 'python', 'sdks')
    const resolved = path.resolve(pythonRootDir)
    const rel = path.relative(sdksRoot, resolved)
    if (rel.startsWith('..') || path.isAbsolute(rel)) {
        return false
    }
    return /^python3\d+$/i.test(path.basename(resolved))
}

/**
 * PyPI single-SDK: CMake must run the interpreter (FindPython3 probe). Copied SDK trees often have
 * python.exe but missing adjacent DLLs — then prefer VIRTUAL_ENV / PATH python matching the SDK minor,
 * while pythonRootDir still points at the SDK for headers/libs.
 *
 * @param {string} sdkRoot
 * @param {string} rollupMm - e.g. "312" from folder python312
 * @param {string} [preferExe] - tried first if set (e.g. SDK python.exe)
 * @returns {string}
 */
export function resolveRollupPythonExecutable(sdkRoot, rollupMm, preferExe) {
    const fromHeaders = readSdkPythonVersionFromHeaders(sdkRoot)
    const want = fromHeaders ?? versionTupleFromRollupMm(rollupMm)
    if (!want) {
        throw new Error(`Invalid rollupMm: ${rollupMm}`)
    }
    if (fromHeaders) {
        const fromName = versionTupleFromRollupMm(rollupMm)
        if (fromName && (fromName[0] !== fromHeaders[0] || fromName[1] !== fromHeaders[1])) {
            logWarn(
                `SDK folder python${rollupMm} headers report ${fromHeaders[0]}.${fromHeaders[1]} — ` +
                    `using that for interpreter matching (not folder name).`
            )
        }
    }
    /** @type {string[]} */
    const candidates = []
    if (preferExe) {
        candidates.push(preferExe)
    }
    if (process.env.VIRTUAL_ENV) {
        const v1 = isWin
            ? path.join(process.env.VIRTUAL_ENV, 'Scripts', 'python.exe')
            : path.join(process.env.VIRTUAL_ENV, 'bin', 'python3')
        if (fs.existsSync(v1)) {
            candidates.push(v1)
        }
        const v2 = path.join(process.env.VIRTUAL_ENV, 'bin', 'python')
        if (fs.existsSync(v2)) {
            candidates.push(v2)
        }
    }
    if (process.env.CONDA_PREFIX) {
        const c = isWin
            ? path.join(process.env.CONDA_PREFIX, 'python.exe')
            : path.join(process.env.CONDA_PREFIX, 'bin', 'python')
        if (fs.existsSync(c)) {
            candidates.push(c)
        }
    }
    candidates.push('python', 'python3')
    for (const p of [
        path.join(sdkRoot, 'python.exe'),
        path.join(sdkRoot, 'bin', 'python3'),
        path.join(sdkRoot, 'bin', 'python')
    ]) {
        if (fs.existsSync(p) && !candidates.includes(p)) {
            candidates.push(p)
        }
    }

    const seen = new Set()
    for (const exe of candidates) {
        const key = exe.toLowerCase()
        if (seen.has(key)) {
            continue
        }
        seen.add(key)
        const v = probePythonVersion(exe)
        if (v && v[0] === want[0] && v[1] === want[1]) {
            return exe
        }
    }

    const viaPy = executableFromWindowsPyLauncher(want[0], want[1])
    if (viaPy) {
        return viaPy
    }

    throw new Error(
        `No runnable Python ${want[0]}.${want[1]} for SDK python${rollupMm} (CMake must execute Python3_EXECUTABLE). ` +
            `Install that runtime (Windows: ensure \`py -${want[0]}.${want[1]}\` works), activate a matching venv, ` +
            `or fix DLLs beside ${path.join(sdkRoot, 'python.exe')}.`
    )
}

/**
 * @param {object} [opts]
 * @param {string} [opts.rollupMm] - **PyPI rollup only**: "312" → modules/python/sdks/python312, one wheel per SDK.
 *   Omit for normal `npm run build` — then CMake builds every py_bridge* for all SDK folders (no narrowing).
 * @param {string} [opts.pythonRootDir] - passed to CMake FindPython / Python3_ROOT_DIR.
 * @param {string} [opts.pythonExecutable] - CMake Python_EXECUTABLE / Python3_EXECUTABLE.
 * @param {boolean} [opts.cleanBuildDir] - remove build/ before configure (required when switching SDKs).
 */
export default function main(opts = {}) {
    const { rollupMm, pythonRootDir, pythonExecutable, cleanBuildDir } = opts

    let resolvedExecutable = pythonExecutable
    if (rollupMm && pythonRootDir) {
        resolvedExecutable = resolveRollupPythonExecutable(pythonRootDir, rollupMm, pythonExecutable)
    }

    const envStack = []
    const pushEnv = (key, value) => {
        if (value === undefined || value === null || value === '') return
        envStack.push([key, process.env[key]])
        process.env[key] = String(value)
    }

    // Never set for plain `npm run build`; only pypi-rollup passes rollupMm.
    pushEnv('CAMEL_PYPI_SINGLE_SDK_MM', rollupMm)
    if (rollupMm && pythonRootDir && resolvedExecutable) {
        pushEnv('CAMEL_PYPI_PYTHON_EXECUTABLE', resolvedExecutable)
    }
    if (rollupMm && pythonRootDir && !isUnderCamelPythonSdks(pythonRootDir)) {
        const layout = findInterpreterDevelopmentLayout(resolvedExecutable)
        if (!layout) {
            throw new Error(
                'PyPI rollup: could not resolve Python include/libs from the interpreter (sys.base_prefix / sysconfig). ' +
                    'Install a full CPython with headers, or use modules/python/sdks/python* from sync-python-sdks.'
            )
        }
        pushEnv('CAMEL_PYPI_SDK_ROOT', layout.root)
        pushEnv('CAMEL_PYPI_PY_INCLUDE_DIR', layout.includeDir)
        pushEnv('CAMEL_PYPI_PY_LIBS_DIR', layout.libDir)
        pushEnv('CAMEL_PYPI_PY_VERSION_DOT', layout.dotted)
    }
    if (pythonRootDir) {
        pushEnv('Python3_ROOT', pythonRootDir)
        pushEnv('Python_ROOT', pythonRootDir)
    }

    const cmakeExtra = []
    const qcmake = (p) => `"${p.replace(/\\/g, '/')}"`
    if (pythonRootDir) {
        cmakeExtra.push(`-DPython3_ROOT_DIR=${qcmake(pythonRootDir)}`, `-DPython_ROOT_DIR=${qcmake(pythonRootDir)}`)
    }
    if (resolvedExecutable) {
        cmakeExtra.push(
            `-DPython_EXECUTABLE=${qcmake(resolvedExecutable)}`,
            `-DPython3_EXECUTABLE=${qcmake(resolvedExecutable)}`
        )
    }

    try {
        if (cleanBuildDir) {
            removeDir(path.join(BASEDIR, 'build'))
        }
        const config = 'Release'
        const cmakeOpts = [getCmakeOptionFlags(), ...cmakeExtra].filter(Boolean).join(' ').trim()
        runCmakeBuild(config, cmakeOpts)
        copyBuildArtifacts(config)
        execSync('node scripts/collect-out.js build', { cwd: BASEDIR, stdio: 'inherit' })
        logDone('Release build completed')
    } finally {
        for (const [key, prev] of envStack.reverse()) {
            if (prev === undefined) delete process.env[key]
            else process.env[key] = prev
        }
    }
}

const thisFile = fileURLToPath(import.meta.url)
if (path.resolve(process.argv[1] || '') === thisFile) {
    main()
}
