// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * 将 build/debug/profile 构建产物收集到 out/<目录>/{bin,include,libs,stdlib}
 * 所有 tools 下的可执行文件放到 bin/，libcamel 共享库放到 libs/（exe 会从 ./libs 查找）
 *
 *  输出策略：Release 写入 out/latest/，Debug 写入 out/debug/（ ：两种配置可以同时存在，
 * 测试运行器的 --build debug 层读取 out/debug，绝不会读到 Release 的二进制）。profile 仍写
 * out/latest/。Release 构建后若当前 HEAD 带有 git tag，则把整棵 out/latest/ 再镜像一份到
 * out/<每个 tag>/（与 latest 内容相同）。
 *
 * 用法: node scripts/collect-out.js [build|debug|profile]
 *   build  -> Release / out/latest, debug -> Debug / out/debug, profile -> RelWithDebInfo / out/latest
 *   默认 build (Release)
 */
import { execFileSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import {
    BASEDIR,
    libName,
    findBuiltLibcamel,
    getGitTagsAtHead,
    logStep,
    logDone,
    logWarn,
    moduleCmoFiles,
    outArtifactRoot,
    outArtifactBin,
    outArtifactLibs,
    outArtifactInclude,
    outArtifactPython,
    outArtifactStdlib,
    writeBuildInfo
} from './common.js'
import { CXX_RUNTIME_LIBRARY, installCxxRuntimeGuard } from './cxx-runtime-guard.mjs'

// The Debug build has its own artifact directory: the Release tree stays at out/latest, so the
// test runner can address a Debug binary by path (--build debug) instead of guessing which
// configuration last overwrote out/latest.
const CONFIG_MAP = {
    build: { config: 'Release', tag: 'latest' },
    debug: { config: 'Debug', tag: 'debug' },
    profile: { config: 'RelWithDebInfo', tag: 'latest' }
}

function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true })
    }
}

const isWindows = process.platform === 'win32'

/** The Python binding's extension module: `_camel.cpython-310-x86_64-linux-gnu.so`, `_camel.pyd` */
function isPythonExtensionName(name) {
    return /^_camel[^/\\]*\.(so|pyd|dylib)$/i.test(name)
}

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

// The C++ standard library the link used travels with the install.
//
// libcamel.so and every tool ask for GLIBCXX_3.4.31..36, which is newer than the libstdc++ an
// older distribution ships: there the binary refuses to start ("version `GLIBCXX_3.4.36' not
// found") and every caller had to export LD_LIBRARY_PATH first. A command typed by hand over ssh,
// or any caller that did not inherit that export, got a binary that could not start at all -- and
// the failure looks like a broken test rather than a missing library (docs/handover/pitfalls.cn.md,
// the GLIBCXX row).
//
// Writing the toolchain's directory into the executables' rpath would answer that with an absolute
// path inside one account's toolchain: a wheel carrying it names a directory the machine it is
// installed on has never had, and the path stops resolving the day that toolchain moves. libs/ is
// where the rpath the build already writes points -- `$ORIGIN/../libs` on every tool
// (tools/libcamel_tool.cmake), `$ORIGIN` on libcamel itself (CMakeLists.txt) -- so the library is
// copied there
// instead: the binary loads the copy beside it wherever the install is unpacked, and `ldd` names a
// file inside the install rather than the system's. What that costs is size (~24 MB in libs/,
// already 20 MB of libcamel) and the rule that libs/ travels with bin/, which it already did.
//
// The copy cannot answer the install that finds a *too old* one -- `libs/` missing from an older
// install, or its `libstdc++.so.6` replaced -- because the loader refuses the binary before `main`.
// `bin/camel` therefore becomes a launcher (scripts/cxx-runtime-guard.mjs) whose one check runs
// before that process exists; the copy above and the launcher are the two halves of the ruling
// "an install carries its own C++ runtime, and when the one it finds is too old it says so".
// Directories that belong to the distribution. A link that resolved the library there produced a
// binary the machine it runs on can load, so there is nothing to carry; only a toolchain kept
// elsewhere (a conda environment, a custom GCC) needs its own copy bundled.
const SYSTEM_LIBRARY_DIRS = ['/lib', '/lib32', '/lib64', '/usr/lib', '/usr/lib32', '/usr/lib64']

function isSystemLibraryDir(dir) {
    return SYSTEM_LIBRARY_DIRS.some((base) => dir === base || dir.startsWith(base + '/'))
}

/**
 * The runtime library the C++ compiler driver links, as that same driver reports it: the driver
 * that chose the library at link time is the one whose answer decides which version the binary
 * needs, so asking it is not a guess. `null` when no driver on PATH answers.
 */
function findCxxRuntimeLibrary(name) {
    const compilers = [process.env.CXX, process.env.CC, 'c++', 'clang++'].filter(Boolean)
    for (const compiler of compilers) {
        try {
            const reported = execFileSync(compiler, [`--print-file-name=${name}`], {
                encoding: 'utf8',
                stdio: ['ignore', 'pipe', 'ignore']
            }).trim()
            // A driver that cannot find the library prints the bare name; an absolute path that
            // exists is the answer, and its real file is what is copied (`libstdc++.so.6` is a
            // symlink to `libstdc++.so.6.0.36`, and the linker records the symlink's name).
            if (path.isAbsolute(reported) && fs.existsSync(reported)) return fs.realpathSync(reported)
        } catch {
            // Not every CXX/CC on PATH is a compiler driver, and not every driver knows this
            // option: the next candidate answers instead.
        }
    }
    return null
}

function collectCxxRuntimeLibrary(libsDir) {
    if (isWindows || process.platform === 'darwin') return
    const source = findCxxRuntimeLibrary(CXX_RUNTIME_LIBRARY)
    if (source === null) {
        logWarn(
            `No compiler reported ${CXX_RUNTIME_LIBRARY}; out/<tag>/libs will not carry it and the ` +
                `binaries load the host's copy (bin/camel then refuses with what that copy is missing)`
        )
        return
    }
    // A link that resolved the library inside the distribution is not carried: the machine that
    // runs this install is the one whose copy answered the link, so there is nothing a copy would
    // add. Said out loud, because "libs/ has no libstdc++ and that is correct" has to be readable
    // from the build log rather than inferred from a missing file.
    if (isSystemLibraryDir(path.dirname(source))) {
        logStep(
            `Not bundling ${CXX_RUNTIME_LIBRARY}: the link resolved it in ${path.dirname(source)}, ` +
                `a directory of the distribution (SYSTEM_LIBRARY_DIRS)`
        )
        return
    }
    const destination = path.join(libsDir, CXX_RUNTIME_LIBRARY)
    fs.copyFileSync(source, destination)
    fs.chmodSync(destination, 0o755)
    logStep(`Bundled ${CXX_RUNTIME_LIBRARY} from ${source}: the binaries load it from libs/`)
}

const COLLECT_PRIMARY_TAG = 'latest'

function collect(config, tag) {
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

    const libPath = findBuiltLibcamel(config)
    if (fs.existsSync(libPath)) {
        fs.copyFileSync(libPath, path.join(libsDir, libName))
        // Windows consumers of the runtime bridge link its import library, not the DLL.
        if (isWindows) {
            const importLib = libPath.replace(/\.dll$/i, '.lib')
            if (fs.existsSync(importLib)) {
                fs.copyFileSync(importLib, path.join(libsDir, path.basename(importLib)))
            } else {
                logWarn(`Import library not found: ${importLib}`)
            }
        }
        if (isWindows && config === 'Debug') {
            fs.copyFileSync(libPath.replace('.dll', '.pdb'), path.join(libsDir, libName.replace('.dll', '.pdb')))
        }
    } else {
        logWarn(`Library not found for ${libName} under build/ (expected a built shared library for ${config})`)
    }

    collectCxxRuntimeLibrary(libsDir)

    // `bin/camel` becomes a launcher and `bin/camel.real` the binary it starts (Linux only; see
    // scripts/cxx-runtime-guard.mjs). The install is complete either way: a launcher that cannot be
    // written leaves the binary where it is, and every caller keeps starting it directly.
    const guard = installCxxRuntimeGuard(binDir)
    if (guard !== null) {
        logStep(
            `bin/${path.basename(guard.launcher)} is a launcher for bin/${path.basename(guard.binary)}: ` +
                `it answers with what the loader is missing (${guard.interpreter}) before the loader refuses`
        )
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

    // The Python binding: the CPython extension module, next to libs/ so the runpath
    // `$ORIGIN/../libs` the build wrote finds libcamel in both layouts. Absent whenever the
    // configure found no Python development files, which is not a warning: the rest of the
    // install is complete without it.
    const pythonBuildDir = [
        path.join(BASEDIR, 'build', 'python', config),
        path.join(BASEDIR, 'build', 'python')
    ].find((dir) => fs.existsSync(dir))
    if (pythonBuildDir !== undefined) {
        const pythonDir = outArtifactPython(tag)
        ensureDir(pythonDir)
        for (const f of fs.readdirSync(pythonBuildDir)) {
            const srcPath = path.join(pythonBuildDir, f)
            if (!fs.statSync(srcPath).isFile()) continue
            if (!isPythonExtensionName(f)) continue
            fs.copyFileSync(srcPath, path.join(pythonDir, f))
        }
    }

    if (fs.existsSync(includeSrc)) {
        fs.cpSync(includeSrc, includeDir, { recursive: true })
    }

    if (fs.existsSync(stdlibSrc)) {
        fs.cpSync(stdlibSrc, stdlibDir, { recursive: true })
    }
    // The `.cmo` files this configure builds, from CMake's manifest. Never scan
    // build/{libs,modules} for them: a module that was renamed or removed leaves its artifact
    // behind, and copying that forward is how a retired name kept resolving.
    const moduleFiles = moduleCmoFiles(config)
    const installedCmo = new Set()
    for (const src of moduleFiles) {
        const name = path.basename(src)
        fs.copyFileSync(src, path.join(stdlibDir, name))
        installedCmo.add(name)
        const pdb = src.replace(/\.cmo$/, '.pdb')
        if (fs.existsSync(pdb)) {
            fs.copyFileSync(pdb, path.join(stdlibDir, path.basename(pdb)))
        }
    }
    // stdlib/ is copied verbatim above, so a stale `.cmo` still in the source tree would land in
    // the install, and out/<tag>/stdlib is on the module search path. The install is exactly the
    // set the build produced: anything else would be a module that no longer exists.
    for (const f of fs.readdirSync(stdlibDir)) {
        if (!f.endsWith('.cmo') || installedCmo.has(f)) continue
        fs.rmSync(path.join(stdlibDir, f))
        logWarn(`Removed stale out/${tag}/stdlib/${f}: no module of that name is built`)
    }

    // Copy Python bridge libraries to out/<tag>/libs for runtime routing. The name carries the
    // platform: `py_bridge310.dll` on Windows, `libpy_bridge310.so` / `.dylib` elsewhere -- and the
    // loader asks for exactly that name inside `libs/` (`bridgeFileNameForTag` in
    // src/core/module/dynamic.cpp), so a Linux host that built its bridge still failed to find it
    // while only the Windows spelling was collected here.
    const bridgeDir = path.join(BASEDIR, 'build', 'modules', 'python', config)
    if (fs.existsSync(bridgeDir)) {
        const files = fs.readdirSync(bridgeDir)
        for (const f of files) {
            const lower = f.toLowerCase()
            const isBridge = lower.startsWith('py_bridge') || lower.startsWith('libpy_bridge')
            const isLibrary =
                lower.endsWith('.dll') ||
                lower.endsWith('.pdb') ||
                lower.endsWith('.so') ||
                lower.endsWith('.dylib')
            if (isBridge && isLibrary) {
                fs.copyFileSync(path.join(bridgeDir, f), path.join(libsDir, f))
            }
        }
    }

    logDone(`Artifacts collected to out/${tag}/`)

    // The stamp the `-NoBuild` guard compares against. Written by the build, not by the runner: an
    // install made by `npm run build` on this machine, in a release tarball or on the Linux host all
    // carry it, and a collect that died leaves no stamp for an install it did not finish.
    writeBuildInfo(config, tag)

    // Only the Release tree is the installation a tag names; the Debug tree is a local test
    // artifact and must not become a release mirror.
    if (tag !== COLLECT_PRIMARY_TAG) {
        return
    }
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
        // The mirror is the same install under another name; its stamp must name its own tag so a
        // guard run against out/<tag> reports where it looked.
        writeBuildInfo(config, mirrorTag)
        logDone(`Mirrored to out/${mirrorTag}/`)
    }
}

const arg = process.argv[2] || 'build'
const entry = CONFIG_MAP[arg]
if (!entry) {
    logWarn(`Unknown config "${arg}", using Release. Valid: build, debug, profile`)
}
collect(entry ? entry.config : CONFIG_MAP.build.config, entry ? entry.tag : CONFIG_MAP.build.tag)
