// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import { execSync, spawnSync } from 'child_process'
import { createHash } from 'crypto'
import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
export const BASEDIR = path.dirname(path.dirname(__filename))

const isWindows = process.platform === 'win32'
export const executableName = `camel${isWindows ? '.exe' : ''}`
export const libName = isWindows
    ? 'libcamel.dll'
    : process.platform === 'darwin'
        ? 'libcamel.dylib'
        : 'libcamel.so'

const colors = {
    reset: '\x1b[0m',
    cyan: '\x1b[36m',
    yellow: '\x1b[33m',
    green: '\x1b[32m',
    red: '\x1b[31m'
}

export function logStep(message) {
    console.log(`${colors.cyan}[STEP]${colors.reset} ${message}`)
}

export function logDone(message) {
    console.log(`${colors.green}[DONE]${colors.reset} ${message}`)
}

export function logWarn(message) {
    console.log(`${colors.yellow}[WARN]${colors.reset} ${message}`)
}

export function logFail(message) {
    console.error(`${colors.red}[FAIL]${colors.reset} ${message}`)
}

export function runCommand(command, options = {}) {
    try {
        logStep(`Running: ${command}`)
        execSync(command, { stdio: 'inherit', ...options })
    } catch (error) {
        logFail(`Error executing command: ${command}\n${error}`)
        process.exit(1)
    }
}

/** Windows: 避免 node_modules/.bin/rc 被 CMake 当作 Resource Compiler，将含 node_modules 的 PATH 项移到末尾 */
export function getCmakeEnv() {
    if (process.platform !== 'win32') return undefined
    const pathParts = (process.env.PATH || '').split(path.delimiter)
    const normal = pathParts.filter((p) => !p.includes('node_modules'))
    const nodeParts = pathParts.filter((p) => p.includes('node_modules'))
    if (nodeParts.length === 0) return undefined
    return { ...process.env, PATH: [...normal, ...nodeParts].join(path.delimiter) }
}

/** 显式指定 Clang，避免 CMake/Conan 在 CI 或本地误选 GCC/MSVC。 */
export function getCmakeClangFlags() {
    if (process.platform === 'win32') {
        return '-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-rc'
    }
    return '-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++'
}

/**
 * ccache 作为编译器启动器，只在 `CAMEL_USE_CCACHE=1` 时启用（CI 的缓存步骤会设置它，并保证
 * `ccache` 在 PATH 上）。默认关闭：本机的构建产物不该悄悄依赖一个额外的缓存目录，而当 ccache
 * 不可用时（版本不认识、缓存目录不可写）构建仍必须以正确的结果完成，故探测失败只告警。
 * @returns {string} 传给 cmake 的 `-D` 参数，空串表示不用
 */
export function getCmakeCcacheFlags() {
    if (process.env.CAMEL_USE_CCACHE !== '1') {
        return ''
    }
    const ccache = process.platform === 'win32' ? 'ccache.exe' : 'ccache'
    const probe = spawnSync(ccache, ['--version'], { encoding: 'utf8' })
    if (probe.status !== 0) {
        logWarn(`CAMEL_USE_CCACHE=1 but ${ccache} did not run; building without it`)
        return ''
    }
    return `-DCMAKE_C_COMPILER_LAUNCHER=${ccache} -DCMAKE_CXX_COMPILER_LAUNCHER=${ccache}`
}

/**
 * `conan install` 要覆盖哪些 build_type，来自 `CAMEL_CONAN_BUILD_TYPES`（逗号分隔，默认三种）。
 * 一个只构建 Release 的 CI job 只要 Release：Debug 与 RelWithDebInfo 的头文件与库没人读，
 * 在 Windows 上却要为每种配置各构建一遍依赖。
 * @returns {string[]}
 */
export function conanBuildTypes() {
    const known = ['Release', 'Debug', 'RelWithDebInfo']
    const raw = process.env.CAMEL_CONAN_BUILD_TYPES
    if (!raw) {
        return known
    }
    const wanted = raw
        .split(',')
        .map((part) => part.trim())
        .filter(Boolean)
    const unknown = wanted.filter((type) => !known.includes(type))
    if (unknown.length > 0) {
        throw new Error(
            `CAMEL_CONAN_BUILD_TYPES has ${unknown.join(', ')}; the known build types are ${known.join(', ')}`
        )
    }
    return wanted
}

/**
 * 为 `conanBuildTypes()` 里的每种配置执行 `conan install`。Ninja Multi-Config 的工程需要同一棵
 * `build/` 里有各配置的依赖，因此这里的集合必须覆盖工程会构建的配置。
 * @param {string[]} [buildTypes] - 覆盖 `CAMEL_CONAN_BUILD_TYPES`
 */
export function conanInstall(buildTypes = conanBuildTypes()) {
    for (const buildType of buildTypes) {
        const settings = buildType === 'Release' ? '' : ` --settings=build_type=${buildType}`
        runCommand(`conan install . --output-folder=build --build=missing${settings}`, { cwd: BASEDIR })
    }
}

export function removeDir(dir) {
    if (fs.existsSync(dir)) {
        fs.rmSync(dir, { recursive: true, force: true })
        console.log(`Removed directory: ${dir}`)
    }
}

export function copyFile(src, dest) {
    fs.copyFileSync(src, dest)
    // console.log(`Copied: ${src} to ${dest}`)
}

export function copyDir(src, dest) {
    if (!fs.existsSync(dest)) {
        fs.mkdirSync(dest, { recursive: true })
    }
    fs.readdirSync(src).forEach((file) => {
        const srcFile = path.join(src, file)
        const destFile = path.join(dest, file)
        if (fs.lstatSync(srcFile).isDirectory()) {
            copyDir(srcFile, destFile)
        } else {
            copyFile(srcFile, destFile)
        }
    })
}

function walkForFile(rootDir, fileName, maxDepth, matches, depth = 0) {
    if (depth > maxDepth || !fs.existsSync(rootDir)) {
        return
    }
    for (const ent of fs.readdirSync(rootDir, { withFileTypes: true })) {
        const fullPath = path.join(rootDir, ent.name)
        if (ent.isFile()) {
            if (ent.name === fileName) {
                matches.push(fullPath)
            }
            continue
        }
        if (ent.isDirectory()) {
            // Object and dependency directories cannot contain the final shared library artifact.
            if (ent.name === 'CMakeFiles' || ent.name.endsWith('.dir')) {
                continue
            }
            walkForFile(fullPath, fileName, maxDepth, matches, depth + 1)
        }
    }
}

/**
 * Resolve the built libcamel shared library across generator/platform-specific output layouts.
 * Multi-config generators do not guarantee the top-level target lands under build/<Config>/.
 *
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @returns {string}
 */
export function findBuiltLibcamel(config) {
    const buildRoot = path.join(BASEDIR, 'build')
    const candidates = [
        path.join(buildRoot, config, libName),
        path.join(buildRoot, libName),
        path.join(buildRoot, 'src', config, libName),
        path.join(buildRoot, 'src', libName)
    ]
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate
        }
    }

    const matches = []
    walkForFile(buildRoot, libName, 4, matches)
    if (matches.length === 0) {
        return ''
    }

    const scored = matches
        .map((fullPath) => {
            const rel = path.relative(buildRoot, fullPath)
            const parts = rel.split(path.sep)
            const hasConfigDir = parts.includes(config)
            return {
                fullPath,
                score: (hasConfigDir ? 0 : 100) + parts.length
            }
        })
        .sort((a, b) => a.score - b.score || a.fullPath.localeCompare(b.fullPath))

    return scored[0].fullPath
}

/**
 *  CMake 在 configure 时写出的模块清单：每个配置一份，每行一个本次会构建的 `.cmo`
 * 绝对路径。见顶层 `CMakeLists.txt` 末尾的 `file(GENERATE)`。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 */
export function moduleManifestPath(config) {
    return path.join(BASEDIR, 'build', `camel-module-outputs-${config}.txt`)
}

/**
 * 本次 configure 会构建的 `.cmo` 文件（绝对路径），按清单顺序返回其中已存在的那些。
 *
 * 清单是唯一权威：目录扫描会把“模块改名/删除后留在 build/ 里的旧 `.cmo`”一起收进安装，
 *  于是旧名仍然可导入。旧构建树没有清单时这里报错而不是退回到扫描——退回等于把缺陷
 * 原样保留。
 *
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @returns {string[]}
 */
export function moduleCmoFiles(config) {
    const manifest = moduleManifestPath(config)
    if (!fs.existsSync(manifest)) {
        throw new Error(
            `module manifest not found: ${path.relative(BASEDIR, manifest)}; ` +
                'configure and build this tree first (the manifest is written at configure time)'
        )
    }
    const files = []
    for (const line of fs.readFileSync(manifest, 'utf-8').split(/\r?\n/)) {
        const file = line.trim()
        // A configuration that was never built has its manifest but no files yet; only the
        // modules that were actually linked are installed.
        if (file && fs.existsSync(file)) {
            files.push(file)
        }
    }
    return files
}

/**
 * collect-out 主写入 `out/latest/`；若有 tag 则另镜像 `out/<tag>/`。与 pypi-rollup 读取路径须一致。
 * @param {string} tag - 通常为 "latest"（collect-out 主输出）或 CAMEL_PYPI_TAG 指定的镜像目录名
 */
export function outArtifactRoot(tag) {
    return path.join(BASEDIR, 'out', tag)
}

export function outArtifactBin(tag) {
    return path.join(outArtifactRoot(tag), 'bin')
}

export function outArtifactLibs(tag) {
    return path.join(outArtifactRoot(tag), 'libs')
}

export function outArtifactStdlib(tag) {
    return path.join(outArtifactRoot(tag), 'stdlib')
}

export function outArtifactInclude(tag) {
    return path.join(outArtifactRoot(tag), 'include')
}

/**
 *  The CPython extension module of the Python binding: `out/<tag>/python/`. It is beside
 * `libs/` so the runpath `$ORIGIN/../libs` the build wrote finds libcamel here and in the wheel,
 * where the whole install lives under `camel/.cmlenv/`.
 * @param {string} tag
 */
export function outArtifactPython(tag) {
    return path.join(outArtifactRoot(tag), 'python')
}

/**
 * 当前 commit 若有 git tag 则返回其中一个，否则 "latest"。
 * 注意：collect-out 主写入固定为 out/latest/；本函数仍可用于其它需「取一个代表 tag」的场景。
 */
export function getTag() {
    const tags = getGitTagsAtHead()
    if (tags.length > 0) return tags[0]
    return 'latest'
}

/** 指向当前 HEAD 的所有 git tag（可能为空、可能多个），顺序与 `git tag` 一致 */
export function getGitTagsAtHead() {
    try {
        const out = execSync('git tag -l --points-at HEAD', {
            cwd: BASEDIR,
            encoding: 'utf-8'
        })
            .trim()
            .split(/\s+/)
            .filter(Boolean)
        return out
    } catch {
        return []
    }
}

/** 供 build/debug/profile 使用：git describe 生成版本后缀 */
export function getGitVersion() {
    try {
        const output = execSync('git describe --tags --always --dirty', {
            cwd: BASEDIR,
            stdio: ['ignore', 'pipe', 'ignore'],
            encoding: 'utf-8'
        })
            .trim()
            .replace(/-/g, '_')
        return `${output}`
    } catch {
        return ''
    }
}

/**
 * `BUILD_FOOTPRINT` names the build in `camel --version`. It used to be the wall clock of every
 * configure, and because tools/camel-cli and tools/debugger compile it into a generated header,
 * every reconfigure recompiled config.cpp (17 s) and repl.cpp (15 s) even when not one byte of the
 * tree had moved: a gate re-run on an unchanged tree paid 43 s of rebuild for a new timestamp.
 *
 * Pin it to what actually produces the binary -- the source content, the configuration and the
 * option flags -- in `build/footprint.json`. A rebuild of the same tree keeps the same stamp (no
 * recompile); a changed tree, configuration or option set gets a new one, so two different builds
 * still print different stamps. Deleting `build/` drops the pin and stamps afresh.
 */
function pinnedFootprint(config, cmakeOptionFlags, gitVersion) {
    const pinPath = path.join(BASEDIR, 'build', 'footprint.json')
    const fingerprint = sourceFingerprint(BASEDIR)
    const key = createHash('sha256')
        .update(config)
        .update('\0')
        .update(cmakeOptionFlags)
        .update('\0')
        .update(fingerprint.hash)
        .digest('hex')
    try {
        const pin = JSON.parse(fs.readFileSync(pinPath, 'utf-8'))
        if (pin.key === key && typeof pin.footprint === 'string' && pin.footprint) {
            logStep(`Footprint ${pin.footprint} reused: sources, configuration and options unchanged`)
            return pin.footprint
        }
    } catch {
        // No pin yet (first configure, or build/ was removed): stamp afresh below.
    }
    const footprint =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        (gitVersion ? `_${gitVersion}` : '')
    fs.mkdirSync(path.dirname(pinPath), { recursive: true })
    fs.writeFileSync(
        pinPath,
        JSON.stringify(
            { key, footprint, fingerprint: fingerprint.hash, writtenAt: new Date().toISOString() },
            null,
            2
        ) + '\n'
    )
    logStep(`Footprint ${footprint} stamped: sources, configuration or options changed`)
    return footprint
}

/**
 * 执行 Conan 检查、CMake 配置与构建。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @param {string} cmakeOptionFlags - 来自 getCmakeOptionFlags() 的 -D 参数
 */
export function runCmakeBuild(config, cmakeOptionFlags) {
    process.chdir(BASEDIR)
    // The diagnostic numbers are assigned in diagnostics/registry.tsv and the headers and message
    //  tables are generated from it. Generate before CMake configures, so a new declaration
    // is numbered and the tree CMake reads already carries the emitted files; CMake then runs
    // --check again as a dependency of libcamel_objects, so a direct cmake build is guarded too.
    execSync('node scripts/diagnostics-catalog.mjs --generate', { cwd: BASEDIR, stdio: 'inherit' })
    const buildDir = path.join(BASEDIR, 'build')
    const conanToolchain = path.join(buildDir, 'conan_toolchain.cmake')
    if (!fs.existsSync(conanToolchain)) {
        logStep('build/ or conan_toolchain.cmake missing, running conan install...')
        conanInstall()
    }
    process.chdir(buildDir)

    const gitVersion = getGitVersion()
    const cmakeEnv = getCmakeEnv()
    const clangFlags = getCmakeClangFlags()
    const ccacheFlags = getCmakeCcacheFlags()
    const BUILD_FOOTPRINT = pinnedFootprint(
        config,
        `${cmakeOptionFlags} ${clangFlags} ${ccacheFlags} ${process.env.CAMEL_USE_CCACHE || ''}`,
        gitVersion
    )

    logStep(`Building ${config}... (${BUILD_FOOTPRINT})`)
    runCommand(
        `cmake .. -G "Ninja Multi-Config" -DBUILD_FOOTPRINT="${BUILD_FOOTPRINT}" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake ${clangFlags} ${ccacheFlags} ${cmakeOptionFlags}`,
        cmakeEnv ? { env: cmakeEnv } : {}
    )
    // CAMEL_BUILD_KEEP_GOING=1: do not stop at the first file that fails to compile, so that one
    // run shows every error (continuous integration on a platform nobody builds on locally).
    const keepGoing = process.env.CAMEL_BUILD_KEEP_GOING === '1' ? ' -- -k 0' : ''
    runCommand(`cmake --build . --config ${config}${keepGoing}`, cmakeEnv ? { env: cmakeEnv } : {})
}

/**
 * 删掉本次 configure 不构建的模块产物：`build/{libs,modules}/<目录>/<config>/*.cmo` 里不在清单上
 * 的那些。CMake 不会删除已被改名或移除的 target 留下的文件，而那个文件正是旧实现“扫描 build/
 *  并复制到 stdlib/”时把旧模块名带回来的来源。只动当前配置的输出目录；`build/` 其余部分
 * 是 CMake 的缓存，不属于这里管。
 *
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @param {string[]} moduleFiles - `moduleCmoFiles(config)` 的结果
 */
function removeStaleModuleArtifacts(config, moduleFiles) {
    const built = new Set(moduleFiles.map((file) => path.resolve(file)))
    const baseDirs = [path.join(BASEDIR, 'build', 'libs'), path.join(BASEDIR, 'build', 'modules')]
    for (const baseDir of baseDirs) {
        if (!fs.existsSync(baseDir)) continue
        for (const ent of fs.readdirSync(baseDir, { withFileTypes: true })) {
            if (!ent.isDirectory()) continue
            const configDir = path.join(baseDir, ent.name, config)
            if (!fs.existsSync(configDir)) continue
            for (const f of fs.readdirSync(configDir)) {
                if (!f.endsWith('.cmo')) continue
                const file = path.resolve(configDir, f)
                if (built.has(file)) continue
                fs.rmSync(file)
                logWarn(
                    `Removed stale build artifact ${path.relative(BASEDIR, file)}: ` +
                        'no module of that name is built'
                )
            }
        }
    }
}

/**
 * 把可执行文件放到 libcamel 旁边（调试用），清掉源码树里旧构建留下的 `.cmo`，并从 `build/`
 *  删除本次 configure 不构建的模块产物。
 *
 *  `.cmo` 不再复制进 `stdlib/`：源码树不是安装。模块查找本来就会看可执行文件旁与
 * `CAMEL_HOME` 下（`include/camel/utils/install_layout.h`），安装由 `collect-out` 按清单写进
 * `out/<tag>/stdlib`；在源码树里再放一份没有消费者，只会把构建产物留在源码树。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 */
export function copyBuildArtifacts(config) {
    const exeSrcDir = path.join(BASEDIR, 'build', 'tools', 'camel-cli', config)
    const stdlibDir = path.join(BASEDIR, 'stdlib')

    const libSrc = findBuiltLibcamel(config)
    if (fs.existsSync(libSrc)) {
        const libSrcDir = path.dirname(libSrc)
        // 把exe复制到libSrc的同级目录以便调试
        copyFile(path.join(exeSrcDir, executableName), path.join(libSrcDir, executableName))
        // debug模式下额外复制.pdb文件
        if (config === 'Debug') {
            const pdbName = `${executableName.replace('.exe', '')}.pdb`
            const pdbSrc = path.join(exeSrcDir, pdbName)
            if (fs.existsSync(pdbSrc)) {
                copyFile(pdbSrc, path.join(libSrcDir, pdbName))
            }
        }
    } else {
        logWarn(`Built ${libName} not found under build/ for ${config}`)
    }

    //  源码树不是安装：`.cmo` 不再复制进 `stdlib/`。模块查找会看可执行文件旁与
    // `CAMEL_HOME` 下的 `stdlib`，`collect-out` 把清单里的 `.cmo` 写进 `out/<tag>/stdlib`，
    // 所以在源码树里再放一份没有消费者。旧构建留下的在这里删掉：一个 `.cmo` 也不能比它的
    //  模块活得久（这些文件不能比它们的模块活得久，删除已经放进构建）。
    if (fs.existsSync(stdlibDir)) {
        for (const f of fs.readdirSync(stdlibDir)) {
            if (!f.endsWith('.cmo')) continue
            fs.rmSync(path.join(stdlibDir, f))
            logWarn(`Removed stdlib/${f}: a .cmo belongs in the install, not in the source tree`)
        }
    }
    removeStaleModuleArtifacts(config, moduleCmoFiles(config))

    //  A step that starts writing build output next to the sources again fails the build,
    // with the file named, instead of leaving it for the next person to find.
    const guard = spawnSync(
        process.execPath,
        [path.join(BASEDIR, 'scripts', 'check-source-artifacts.mjs')],
        { cwd: BASEDIR, stdio: 'inherit' }
    )
    if (guard.status !== 0) {
        logFail('Build output was found in the source tree; a build must not write it there (H191)')
        process.exit(guard.status || 1)
    }
    logDone(`Copied all build artifacts`)
}

//  --- the build stamp ---------------------------------------------------
//
// `out/<tag>/build-info.json` records the sources an install was produced from. Reusing an install
// without rebuilding (`camel-remote.ps1 -NoBuild`, or a test run against `out/latest` by hand) is
//  only honest while the install still matches the tree: lost 9 tests to a stale binary,
//  lost one (`test::saturate_props` was not registered in the old one), and recorded the other
// direction -- a green that could not have moved, because `-NoBuild` reads `out/latest/stdlib` and
// that copy is made by the build.
//
// The stamp is written here, by the build itself (collect-out at the end of `npm run build`), so a
// local build stamps too. `scripts/build-info.js check` compares it against the tree a run is about
// to verify and refuses when they differ; that is the guard, and collect-out knows nothing about
// the remote helper.
//
// What is compared is the **content** of the paths a build reads or copies into the install, not the
// commit: a commit that only touches `docs/`, `test/` or `scripts/` leaves the compiled bytes and the
// installed stdlib unchanged, and refusing there would train people to pass the override. A commit
// that moves one byte of `src/` changes the fingerprint. `git rev-parse HEAD` and the dirty flag are
// recorded beside the fingerprint so the refusal can name both revisions; they are not the test.
//
// The roots below are exactly what the build consumes: the C++ sources and headers, every module,
// the tools, `third_party` (globbed into libcamel), `stdlib` (copied verbatim into
//  `out/<tag>/stdlib`, so a comment there *can* move a test --), the CMake glue and the Conan
// recipe. Generated files (`*.cmo`, objects, libraries, DLLs) are excluded: they are products, not
//  inputs, and no build writes them into `stdlib/` any more. `docs/`, `test/`, `scripts/`,
// `agents/`, `editors/`, `pypi/`, `assets/`, `examples/`, `benchmarks/` and the markdown files are
// not read by the build and are not fingerprinted.
export const BUILD_INFO_FILE = 'build-info.json'

/** Bumped when the recorded shape changes so an old stamp is refused instead of misread. */
//  2: the build footprint the configure compiled in is recorded beside the fingerprint, so
// `camel --version` can be compared with what the build stamped.
export const BUILD_INFO_SCHEMA = 2

/** Bumped when the fingerprint algorithm or `BUILD_INFO_ROOTS` changes. */
export const FINGERPRINT_VERSION = 1

/** Stamped file by tag (`latest`, `debug`, or a release mirror tag). */
export const BUILD_INFO_ROOTS = ['src', 'include', 'modules', 'tools', 'stdlib', 'third_party', 'cmake']

/** Single files at the repository root that the build reads. */
export const BUILD_INFO_ROOT_FILES = ['CMakeLists.txt', 'conanfile.txt']

const FINGERPRINT_SKIP_DIRS = new Set(['.git', 'node_modules', '__pycache__', 'build', 'out'])
const FINGERPRINT_SKIP_FILE = /\.(o|obj|a|lib|so|dll|dylib|pdb|ilk|cmo|pyc|pyo|pyd|exe)$/i

/** Longest list of paths recorded in the stamp; the count is kept in full. */
const BUILD_INFO_MAX_DIRTY_PATHS = 40

function fingerprintFileList(root) {
    const files = []
    const walk = (dir) => {
        let entries
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            return
        }
        for (const ent of entries) {
            const full = path.join(dir, ent.name)
            if (ent.isDirectory()) {
                if (!FINGERPRINT_SKIP_DIRS.has(ent.name)) {
                    walk(full)
                }
                continue
            }
            // Symlinks are not followed: a link's target is hashed where it lives, if it is source.
            if (!ent.isFile() || FINGERPRINT_SKIP_FILE.test(ent.name)) {
                continue
            }
            files.push(full)
        }
    }
    for (const rel of BUILD_INFO_ROOTS) {
        const dir = path.join(root, rel)
        if (fs.existsSync(dir)) {
            walk(dir)
        }
    }
    for (const rel of BUILD_INFO_ROOT_FILES) {
        const file = path.join(root, rel)
        if (fs.existsSync(file)) {
            files.push(file)
        }
    }
    return files
}

/**
 * The footprint the last configure compiled in (`build/footprint.json`). `writeBuildInfo` records
 * it in the install's stamp so a run can compare what `camel --version` prints with what the
 * build actually configured; it is null when the build tree has no pin (a direct cmake configure,
 * or a checkout a build has not run in yet).
 *
 * @param {string} [root] - repository root; defaults to this checkout
 * @returns {string|null}
 */
export function readFootprintPin(root = BASEDIR) {
    try {
        const pin = JSON.parse(fs.readFileSync(path.join(root, 'build', 'footprint.json'), 'utf-8'))
        return typeof pin.footprint === 'string' && pin.footprint ? pin.footprint : null
    } catch {
        return null
    }
}

/**
 * SHA-256 over `path\0content-hash\n` of every file the build reads, sorted by path. Paths are
 * relative and `/`-separated, so the same content gives the same fingerprint regardless of how the
 * checkout is rooted.
 *
 * @param {string} [root] - repository root; defaults to this checkout
 * @returns {{version: number, roots: string[], files: number, hash: string, fileHashes: Record<string, string>}}
 */
export function sourceFingerprint(root = BASEDIR) {
    const entries = fingerprintFileList(root)
        .map((full) => ({
            full,
            rel: path.relative(root, full).split(path.sep).join('/')
        }))
        .sort((a, b) => (a.rel < b.rel ? -1 : a.rel > b.rel ? 1 : 0))

    const aggregate = createHash('sha256')
    const fileHashes = {}
    for (const { full, rel } of entries) {
        const content = fs.readFileSync(full)
        const hex = createHash('sha256').update(content).digest('hex')
        aggregate.update(rel).update('\0').update(hex).update('\n')
        // The per-file map is for the refusal message ("these files changed"); 64 bits is plenty to
        // point at a file, and the aggregate above is the authority.
        fileHashes[rel] = hex.slice(0, 16)
    }
    return {
        version: FINGERPRINT_VERSION,
        roots: [...BUILD_INFO_ROOTS, ...BUILD_INFO_ROOT_FILES],
        files: entries.length,
        hash: aggregate.digest('hex'),
        fileHashes
    }
}

/**
 * HEAD and "is the working tree dirty" for the stamp and for the refusal message. `git` being
 * unavailable is not fatal: the fingerprint is filesystem content and still guards the run, so this
 * warns once and records `available: false` (the message then says the revision is unknown).
 *
 * @param {string} [root]
 * @returns {{available: boolean, revision: string|null, dirty: boolean|null, dirtyCount: number, dirtyPaths: string[]}}
 */
export function gitState(root = BASEDIR) {
    const run = (args) => spawnSync('git', ['-C', root, ...args], { encoding: 'utf-8', stdio: ['ignore', 'pipe', 'ignore'] })
    const head = run(['rev-parse', 'HEAD'])
    if (head.error || head.status !== 0 || !head.stdout || !head.stdout.trim()) {
        return { available: false, revision: null, dirty: null, dirtyCount: 0, dirtyPaths: [] }
    }
    const revision = head.stdout.trim()
    const status = run(['status', '--porcelain'])
    if (status.error || status.status !== 0) {
        return { available: true, revision, dirty: null, dirtyCount: 0, dirtyPaths: [] }
    }
    const paths = (status.stdout || '')
        .split(/\r?\n/)
        .filter(Boolean)
        .map((line) => line.slice(3).replace(/^.* -> /, ''))
    return {
        available: true,
        revision,
        dirty: paths.length > 0,
        dirtyCount: paths.length,
        dirtyPaths: paths.slice(0, BUILD_INFO_MAX_DIRTY_PATHS)
    }
}

/** `out/<tag>/build-info.json` */
export function buildInfoPath(tag = 'latest', root = BASEDIR) {
    return path.join(root, 'out', tag, BUILD_INFO_FILE)
}

/** The stamp of `out/<tag>/`, or null when it does not exist / is not JSON. */
export function readBuildInfo(tag = 'latest', root = BASEDIR) {
    try {
        return JSON.parse(fs.readFileSync(buildInfoPath(tag, root), 'utf-8'))
    } catch {
        return null
    }
}

function shortRevision(info) {
    return info.revision ? info.revision.slice(0, 10) : 'unknown'
}

/**
 * Write `out/<tag>/build-info.json` for the tree as it is now. Called by collect-out at the end of
 * every build (before the tag mirrors are made, and again for each mirror so its `tag` is its own).
 *
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @param {string} tag - `latest`, `debug`, or a release mirror tag
 * @param {string} [root] - repository root; defaults to this checkout
 */
export function writeBuildInfo(config, tag = 'latest', root = BASEDIR) {
    const git = gitState(root)
    if (!git.available) {
        logWarn('git is unavailable: out/' + tag + '/' + BUILD_INFO_FILE + ' records no revision. ' +
            'The source fingerprint is still recorded, so -NoBuild can still tell whether the install matches this tree.')
    }
    const fingerprint = sourceFingerprint(root)
    const info = {
        schema: BUILD_INFO_SCHEMA,
        config,
        tag,
        // The value compiled into the binary; the version-stamp guard compares it with
        // `camel --version`. Null only when no configure pinned one.
        footprint: readFootprintPin(root),
        builtAt: new Date().toISOString(),
        revision: git.revision,
        dirty: git.dirty,
        dirtyCount: git.dirtyCount,
        dirtyPaths: git.dirtyPaths,
        git: git.available,
        fingerprint
    }
    // collect-out has already created out/<tag>/; `node scripts/build-info.js stamp` may not have.
    fs.mkdirSync(path.dirname(buildInfoPath(tag, root)), { recursive: true })
    fs.writeFileSync(buildInfoPath(tag, root), JSON.stringify(info, null, 2) + '\n')
    const dirty = git.dirty === null ? 'no git' : git.dirty ? `dirty: ${git.dirtyCount} file(s)` : 'clean'
    logStep(
        `Stamped out/${tag}/${BUILD_INFO_FILE}: ${shortRevision(info)} (${dirty}), ` +
            `${fingerprint.files} source files, fingerprint ${fingerprint.hash.slice(0, 12)}`
    )
    return info
}
