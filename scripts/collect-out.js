/**
 * 将 build/debug/profile 构建产物收集到 out/tag/{bin,include,libs,stdlib}
 * 所有 tools 下的 exe 放到 bin/，libcamel.dll 放在 libs/（exe 会从 ./libs 查找）
 * tag: 当前 commit 若有 git tag 则使用该 tag，否则使用 "latest"
 *
 * 用法: node scripts/collect-out.js [build|debug|profile]
 *   build  -> Release, debug -> Debug, profile -> RelWithDebInfo
 *   默认 build (Release)
 */
import { execSync } from 'child_process'
import fs from 'fs'
import path from 'path'
import { BASEDIR, libName, getTag, logStep, logDone, logWarn } from './common.js'

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

function collect(config) {
    const tag = getTag()
    const outRoot = path.join(BASEDIR, 'out', tag)
    const binDir = path.join(outRoot, 'bin')
    const includeDir = path.join(outRoot, 'include')
    const libsDir = path.join(outRoot, 'libs')
    const stdlibDir = path.join(outRoot, 'stdlib')

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

    // Python DLL：python/pyplot 模块共享，复制到 libs 便于 .cmo 加载
    if (process.platform === 'win32') {
        try {
            const pyDllPath = execSync(
                'python -c "import sys, pathlib; p=pathlib.Path(sys.base_prefix); print(p / (\'python\' + str(sys.version_info.major) + str(sys.version_info.minor) + \'.dll\'))"',
                { encoding: 'utf-8', cwd: BASEDIR }
            ).trim()
            if (pyDllPath && fs.existsSync(pyDllPath)) {
                const pyDllName = path.basename(pyDllPath)
                fs.copyFileSync(pyDllPath, path.join(libsDir, pyDllName))
            }
        } catch {
            // 忽略，Python 可能未安装或不在 PATH
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

    logDone(`Artifacts collected to out/${tag}/`)
}

const arg = process.argv[2] || 'build'
const config = CONFIG_MAP[arg] ?? CONFIG_MAP.build
if (!CONFIG_MAP[arg]) {
    logWarn(`Unknown config "${arg}", using Release. Valid: build, debug, profile`)
}
collect(config)
