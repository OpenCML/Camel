import { execSync, spawnSync } from 'child_process'
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
 * 执行 Conan 检查、CMake 配置与构建。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 * @param {string} cmakeOptionFlags - 来自 getCmakeOptionFlags() 的 -D 参数
 */
export function runCmakeBuild(config, cmakeOptionFlags) {
    process.chdir(BASEDIR)
    const buildDir = path.join(BASEDIR, 'build')
    const conanToolchain = path.join(buildDir, 'conan_toolchain.cmake')
    if (!fs.existsSync(conanToolchain)) {
        logStep('build/ or conan_toolchain.cmake missing, running conan install...')
        conanInstall()
    }
    process.chdir(buildDir)

    const gitVersion = getGitVersion()
    const BUILD_FOOTPRINT =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        (gitVersion ? `_${gitVersion}` : '')

    logStep(`Building ${config}... (${BUILD_FOOTPRINT})`)
    const cmakeEnv = getCmakeEnv()
    const clangFlags = getCmakeClangFlags()
    const ccacheFlags = getCmakeCcacheFlags()
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
 * 将构建产物复制到项目根目录与 stdlib：camel 可执行文件、libcamel、以及 libs/* 下的所有 .cmo 到 stdlib/。
 * @param {'Release'|'Debug'|'RelWithDebInfo'} config
 */
export function copyBuildArtifacts(config) {
    const exeSrcDir = path.join(BASEDIR, 'build', 'tools', 'camel-cli', config)
    const libsBuildDir = path.join(BASEDIR, 'build', 'libs')
    const modulesBuildDir = path.join(BASEDIR, 'build', 'modules')
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

    const cmoDirs = [libsBuildDir, modulesBuildDir]
    for (const baseDir of cmoDirs) {
        if (!fs.existsSync(baseDir)) continue
        if (!fs.existsSync(stdlibDir)) {
            fs.mkdirSync(stdlibDir, { recursive: true })
        }
        const subdirs = fs.readdirSync(baseDir, { withFileTypes: true })
        for (const ent of subdirs) {
            if (!ent.isDirectory()) continue
            const configDir = path.join(baseDir, ent.name, config)
            if (!fs.existsSync(configDir)) continue
            const files = fs.readdirSync(configDir)
            for (const f of files) {
                if (f.endsWith('.cmo')) {
                    copyFile(path.join(configDir, f), path.join(stdlibDir, f))
                }
            }
        }
    }
    logDone(`Copied all build artifacts`)
}
