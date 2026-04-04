/**
 * 将 build/debug/profile 构建产物收集到 out/latest/{bin,include,libs,stdlib}
 * 所有 tools 下的 exe 放到 bin/，libcamel.dll 放在 libs/（exe 会从 ./libs 查找）
 *
 * 输出策略：始终写入 out/latest/（保证为本次编译产物）。若当前 HEAD 带有 git tag，
 * 则在完成后将整棵 out/latest/ 再镜像一份到 out/<每个 tag>/（与 latest 内容相同）。
 *
 * 用法: node scripts/collect-out.js [build|debug|profile]
 *   build  -> Release, debug -> Debug, profile -> RelWithDebInfo
 *   默认 build (Release)
 */
import { execFileSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import {
    BASEDIR,
    libName,
    getGitTagsAtHead,
    logStep,
    logDone,
    logWarn,
    outArtifactRoot,
    outArtifactBin,
    outArtifactLibs,
    outArtifactInclude,
    outArtifactStdlib
} from './common.js'

const CONFIG_MAP = {
    build: 'Release',
    debug: 'Debug',
    profile: 'RelWithDebInfo'
}

function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true })
    }
}

const isWindows = process.platform === 'win32'

/** python3xx.dll / python.dll — required to consider the pack complete */
function isPythonRuntimeDllName(lowerName) {
    return (
        (lowerName.startsWith('python3') && lowerName.endsWith('.dll')) ||
        lowerName === 'python.dll'
    )
}

/** Same layout as sync-python-sdks: interpreters + common MSVC runtimes next to them */
function isWindowsPythonPackDll(lowerName) {
    if (isPythonRuntimeDllName(lowerName)) return true
    if (lowerName === 'vcruntime140.dll' || lowerName === 'vcruntime140_1.dll') return true
    return false
}

function hasPythonInterpreterDllInSet(files) {
    for (const p of files) {
        const n = path.basename(p).toLowerCase()
        if (n === 'python.dll') return true
        if (/^python3\d+\.dll$/.test(n)) return true
    }
    return false
}

/** Flat directory: files matching predicate (basename lowercased) */
function addMatchingDllsFromFlatDir(dir, intoSet, predicate) {
    if (!dir || !fs.existsSync(dir)) return
    try {
        for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
            if (!ent.isFile()) continue
            const name = ent.name.toLowerCase()
            if (predicate(name)) {
                intoSet.add(path.join(dir, ent.name))
            }
        }
    } catch {
        // ignore unreadable dirs
    }
}

/**
 * Windows: discover DLLs next to the interpreter (base_prefix, venv Scripts parent, DLLs/).
 * Merged with folder scan — venv root often has no .dll files.
 */
function addPythonDllsFromInterpreter(BASEDIR, intoSet) {
    const pyCode = `
import sys, pathlib
roots = [pathlib.Path(sys.base_prefix), pathlib.Path(sys.executable).resolve().parent]
ex = roots[-1]
if sys.platform == "win32" and ex.name.lower() == "scripts":
    roots.append(ex.parent)
    roots.append(ex.parent / "DLLs")
seen = set()
for r in roots:
    if not r.is_dir():
        continue
    try:
        for p in r.iterdir():
            if not p.is_file():
                continue
            n = p.name.lower()
            if (n.startswith("python3") and n.endswith(".dll")) or n == "python.dll":
                seen.add(str(p.resolve()))
    except OSError:
        pass
for line in sorted(seen):
    print(line)
`.trim()

    const tries = [
        ['python', ['-c', pyCode]],
        ['py', ['-3', '-c', pyCode]]
    ]
    for (const [cmd, args] of tries) {
        try {
            const out = execFileSync(cmd, args, {
                encoding: 'utf-8',
                cwd: BASEDIR,
                stdio: ['ignore', 'pipe', 'ignore']
            })
            for (const line of out.split(/\r?\n/)) {
                const t = line.trim()
                if (t && fs.existsSync(t)) {
                    intoSet.add(t)
                }
            }
            return
        } catch {
            // try next launcher
        }
    }
}

function addPythonDllsFromSdks(BASEDIR, intoSet) {
    const sdksRoot = path.join(BASEDIR, 'modules', 'python', 'sdks')
    if (!fs.existsSync(sdksRoot)) return
    try {
        for (const ent of fs.readdirSync(sdksRoot, { withFileTypes: true })) {
            if (!ent.isDirectory()) continue
            const low = ent.name.toLowerCase()
            if (!/^python3\d+$/.test(low)) continue
            const sdkRoot = path.join(sdksRoot, ent.name)
            addMatchingDllsFromFlatDir(sdkRoot, intoSet, isWindowsPythonPackDll)
            addMatchingDllsFromFlatDir(path.join(sdkRoot, 'DLLs'), intoSet, isWindowsPythonPackDll)
        }
    } catch {
        // ignore
    }
}

/** When sdks did not supply an interpreter DLL, copy from venv / conda / active python. */
function addPythonDllsFromVenvFallback(BASEDIR, intoSet) {
    if (process.env.VIRTUAL_ENV) {
        const v = process.env.VIRTUAL_ENV
        addMatchingDllsFromFlatDir(v, intoSet, isWindowsPythonPackDll)
        addMatchingDllsFromFlatDir(path.join(v, 'Scripts'), intoSet, isWindowsPythonPackDll)
        addMatchingDllsFromFlatDir(path.join(v, 'DLLs'), intoSet, isWindowsPythonPackDll)
    }
    if (process.env.CONDA_PREFIX) {
        const c = process.env.CONDA_PREFIX
        addMatchingDllsFromFlatDir(c, intoSet, isWindowsPythonPackDll)
        addMatchingDllsFromFlatDir(path.join(c, 'Library', 'bin'), intoSet, isWindowsPythonPackDll)
    }
    if (isWindows) {
        addPythonDllsFromInterpreter(BASEDIR, intoSet)
    }
}

function collectPythonRuntimeDlls(BASEDIR) {
    const files = new Set()

    addPythonDllsFromSdks(BASEDIR, files)

    if (!hasPythonInterpreterDllInSet(files)) {
        addPythonDllsFromVenvFallback(BASEDIR, files)
    }

    return [...files]
}

const COLLECT_PRIMARY_TAG = 'latest'

function collect(config) {
    const tag = COLLECT_PRIMARY_TAG
    const outRoot = outArtifactRoot(tag)
    const binDir = outArtifactBin(tag)
    const includeDir = outArtifactInclude(tag)
    const libsDir = outArtifactLibs(tag)
    const stdlibDir = outArtifactStdlib(tag)

    logStep(`Collecting ${config} build artifacts to out/${tag}/`)

    if (fs.existsSync(outRoot)) {
        fs.rmSync(outRoot, { recursive: true })
    }
    ensureDir(outRoot)
    ensureDir(binDir)
    ensureDir(includeDir)
    ensureDir(libsDir)
    ensureDir(stdlibDir)

    const toolsBuildDir = path.join(BASEDIR, 'build', 'tools')
    const libSrcDir = path.join(BASEDIR, 'build', config)
    const libsBuildDir = path.join(BASEDIR, 'build', 'libs')
    const includeSrc = path.join(BASEDIR, 'include')
    const stdlibSrc = path.join(BASEDIR, 'stdlib')

    if (!fs.existsSync(toolsBuildDir)) {
        logWarn(`Build dir not found: ${toolsBuildDir}, run npm run build|debug|profile first`)
        process.exit(1)
    }

    // 复制 tools 下所有子目录的 exe 到 out/tag/bin/
    const toolSubdirs = fs.readdirSync(toolsBuildDir, { withFileTypes: true })
    for (const ent of toolSubdirs) {
        if (!ent.isDirectory()) continue
        const configDir = path.join(toolsBuildDir, ent.name, config)
        if (!fs.existsSync(configDir)) continue
        const files = fs.readdirSync(configDir)
        for (const f of files) {
            const srcPath = path.join(configDir, f)
            if (!fs.statSync(srcPath).isFile()) continue
            const copyExe = isWindows ? f.endsWith('.exe') : !/\.(o|a|obj|d|pdb)$/i.test(f)
            const copyPdb = isWindows && config === 'Debug' && f.endsWith('.pdb')
            if (copyExe || copyPdb) {
                fs.copyFileSync(srcPath, path.join(binDir, f))
            }
        }
    }

    const libPath = path.join(libSrcDir, libName)
    if (fs.existsSync(libPath)) {
        fs.copyFileSync(libPath, path.join(libsDir, libName))
        if (isWindows && config === 'Debug') {
            fs.copyFileSync(libPath.replace('.dll', '.pdb'), path.join(libsDir, libName.replace('.dll', '.pdb')))
        }
    } else {
        logWarn(`Library not found: ${libPath}`)
    }

    // Python runtime DLLs: modules/python/sdks/python3xx/ (sync-python-sdks); else venv / conda / python on PATH.
    if (process.platform === 'win32') {
        const dlls = collectPythonRuntimeDlls(BASEDIR)
        if (dlls.length === 0) {
            logWarn(
                'No Python runtime DLL found. Run sync-python-sdks into modules/python/sdks/, or activate a venv with python3xx.dll discoverable.'
            )
        }
        for (const dllPath of dlls) {
            const dllName = path.basename(dllPath)
            fs.copyFileSync(dllPath, path.join(libsDir, dllName))
        }
    }

    if (fs.existsSync(includeSrc)) {
        fs.cpSync(includeSrc, includeDir, { recursive: true })
    }

    if (fs.existsSync(stdlibSrc)) {
        fs.cpSync(stdlibSrc, stdlibDir, { recursive: true })
    }
    const cmoSourceDirs = [
        path.join(BASEDIR, 'build', 'libs'),
        path.join(BASEDIR, 'build', 'modules')
    ]
    for (const baseDir of cmoSourceDirs) {
        if (!fs.existsSync(baseDir)) continue
        const subdirs = fs.readdirSync(baseDir, { withFileTypes: true })
        for (const ent of subdirs) {
            if (!ent.isDirectory()) continue
            const configDir = path.join(baseDir, ent.name, config)
            if (!fs.existsSync(configDir)) continue
            const files = fs.readdirSync(configDir)
            for (const f of files) {
                if (f.endsWith('.cmo') || f.endsWith('.pdb')) {
                    fs.copyFileSync(path.join(configDir, f), path.join(stdlibDir, f))
                }
            }
        }
    }

    // Copy Python bridge DLLs to out/<tag>/libs for runtime routing.
    const bridgeDir = path.join(BASEDIR, 'build', 'modules', 'python', config)
    if (fs.existsSync(bridgeDir)) {
        const files = fs.readdirSync(bridgeDir)
        for (const f of files) {
            const lower = f.toLowerCase()
            if (lower.startsWith('py_bridge') && (lower.endsWith('.dll') || lower.endsWith('.pdb'))) {
                fs.copyFileSync(path.join(bridgeDir, f), path.join(libsDir, f))
            }
        }
    }

    logDone(`Artifacts collected to out/${tag}/`)

    const tagsAtHead = getGitTagsAtHead()
    for (const mirrorTag of tagsAtHead) {
        if (mirrorTag === COLLECT_PRIMARY_TAG) {
            continue
        }
        const mirrorRoot = outArtifactRoot(mirrorTag)
        logStep(`Mirroring out/${COLLECT_PRIMARY_TAG}/ to out/${mirrorTag}/`)
        if (fs.existsSync(mirrorRoot)) {
            fs.rmSync(mirrorRoot, { recursive: true })
        }
        fs.cpSync(outRoot, mirrorRoot, { recursive: true })
        logDone(`Mirrored to out/${mirrorTag}/`)
    }
}

const arg = process.argv[2] || 'build'
const config = CONFIG_MAP[arg] ?? CONFIG_MAP.build
if (!CONFIG_MAP[arg]) {
    logWarn(`Unknown config "${arg}", using Release. Valid: build, debug, profile`)
}
collect(config)
