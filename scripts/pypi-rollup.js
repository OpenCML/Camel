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
    outArtifactRoot,
    outArtifactBin
} from './common.js'
import { ensureDirSync } from 'fs-extra/esm'
import build from './build.js'
import { execSync } from 'child_process'
import os from 'os'

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

function getPythonPlatformTag() {
    const platform = os.platform()
    const arch = os.arch()

    const archMap = {
        x64: platform === 'win32' ? 'amd64' : 'x86_64',
        arm64: 'arm64'
    }

    const normalizedArch = archMap[arch] || arch

    switch (platform) {
        case 'win32':
            return `win_${normalizedArch}`

        case 'darwin': {
            const ver = execSync('sw_vers -productVersion').toString().trim()
            const [major, minor] = ver.split('.').slice(0, 2)
            const macosVersion = `${major}_${minor}`

            let realArch = normalizedArch
            try {
                const proc = execSync('sysctl -n machdep.cpu.brand_string').toString()
                realArch = proc.includes('Apple') ? 'arm64' : normalizedArch
            } catch {}

            return `macosx_${macosVersion}_${realArch}`
        }

        case 'linux':
            return `linux_${normalizedArch}`

        default:
            throw new Error(`Unsupported platform: ${platform}`)
    }
}

function main() {
    logStep('Building project')
    process.chdir(BASEDIR)
    build()
    // runCmakeBuild leaves cwd at build/; restore so relative paths (e.g. future runCommand) resolve from repo root
    process.chdir(BASEDIR)
    logDone('Project built')

    // Default out/latest: always the tree produced by the last collect-out (see collect-out.js)
    const outTag = process.env.CAMEL_PYPI_TAG || 'latest'
    logDone(`Using out/${outTag}/`)

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

    logStep('Copying files to pypi directory')
    const PYPI_BASE = path.join(BASEDIR, 'pypi', 'camel-lang')
    const CAMEL_BASE = path.join(PYPI_BASE, 'src', 'camel')
    const CMLENV = path.join(CAMEL_BASE, '.cmlenv')

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

    logDone('Files copied')

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

    logStep('Building wheel')
    process.chdir(PYPI_BASE)
    // PEP 517 frontend; not always present in minimal venvs
    runCommand('python -m pip install -q build wheel')
    runCommand('python -m build --wheel')
    logDone('Wheel built (initial py3-none-any from setuptools)')

    // Native binaries live under package-data; setuptools still emits a "universal" wheel unless we retag.
    // `wheel tags` fixes both the filename and .dist-info/WHEEL (Tag: py3-none-<platform>), which pip/PyPI use.
    const platformTag = getPythonPlatformTag()
    const distDir = path.join(PYPI_BASE, 'dist')
    const anyWheels = fs.readdirSync(distDir).filter((f) => f.endsWith('-any.whl'))
    if (anyWheels.length === 0) {
        logFail(`No *-any.whl found in ${distDir} after build`)
        process.exit(1)
    }
    logStep(`Retagging wheel(s) for platform: ${platformTag}`)
    for (const w of anyWheels) {
        const wheelPath = path.join('dist', w)
        runCommand(`python -m wheel tags --platform-tag ${platformTag} --remove "${wheelPath}"`)
    }
    logDone(`Platform wheel tag: py3-none-${platformTag}`)

    logStep('Move wheel to project dist')
    const PYPI_DIST = path.join(BASEDIR, 'dist', 'pypi')
    ensureDirSync(PYPI_DIST)
    copyDir(path.join(PYPI_BASE, 'dist'), PYPI_DIST)
    logDone('Wheel moved')

    logStep('Cleaning up')
    fs.writeFileSync(pyprojectPath, pyprojectOriginal)
    removeDir(path.join(PYPI_BASE, 'dist'))
    removeDir(path.join(PYPI_BASE, 'build'))
    removeDir(CMLENV)
    removeDir(WRAPPER_DIR)
    removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
    logDone('Cleaned up')
    process.chdir(BASEDIR)
}

main()
