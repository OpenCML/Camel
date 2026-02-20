import path from 'path'
import fs from 'fs'
import { runCommand, copyFile, BASEDIR, logDone, logStep, getCmakeEnv, getCmakeClangFlags } from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'
import { execSync } from 'child_process'

function getGitVersion() {
    try {
        const output = execSync('git describe --tags --always --dirty', {
            cwd: BASEDIR,
            stdio: ['ignore', 'pipe', 'ignore'],
            encoding: 'utf-8'
        })
            .trim()
            .replace(/-/g, '_')
        return `${output}`
    } catch (e) {
        return ''
    }
}

function main() {
    process.chdir(BASEDIR)
    const buildDir = path.join(BASEDIR, 'build')
    const conanToolchain = path.join(buildDir, 'conan_toolchain.cmake')
    if (!fs.existsSync(conanToolchain)) {
        logStep('build/ or conan_toolchain.cmake missing, running conan install...')
        runCommand('conan install . --output-folder=build --build=missing')
        runCommand('conan install . --output-folder=build --build=missing --settings=build_type=Debug')
        runCommand('conan install . --output-folder=build --build=missing --settings=build_type=RelWithDebInfo')
    }
    process.chdir(buildDir)

    const gitVersion = getGitVersion()

    const BUILD_FOOTPRINT =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        (gitVersion ? `_${gitVersion}` : '')

    logStep(`Building Debug... (${BUILD_FOOTPRINT})`)
    const cmakeOpts = getCmakeOptionFlags()
    const cmakeEnv = getCmakeEnv()
    const clangFlags = getCmakeClangFlags()
    runCommand(
        `cmake .. -G "Ninja Multi-Config" -DBUILD_FOOTPRINT="${BUILD_FOOTPRINT}" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake ${clangFlags} ${cmakeOpts}`,
        cmakeEnv ? { env: cmakeEnv } : {}
    )
    runCommand('cmake --build . --config Debug', cmakeEnv ? { env: cmakeEnv } : {})

    const config = 'Debug'
    const exeSrcDir = path.join(BASEDIR, 'build', 'tools', 'camel-cli', config)
    const libSrcDir = path.join(BASEDIR, 'build', config)
    const isWindows = process.platform === 'win32'
    const executableName = `camel${isWindows ? '.exe' : ''}`
    const libName = isWindows ? 'libcamel.dll' : process.platform === 'darwin' ? 'libcamel.dylib' : 'libcamel.so'

    copyFile(path.join(exeSrcDir, executableName), path.join(BASEDIR, executableName))
    const libSrc = path.join(libSrcDir, libName)
    if (fs.existsSync(libSrc)) {
        copyFile(libSrc, path.join(BASEDIR, libName))
    }

    logDone('Debug build completed')
}

main()
