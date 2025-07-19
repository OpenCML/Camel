import path from 'path'
import { runCommand, copyFile, BASEDIR, logDone, logStep } from './common.js'
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
        return `Git/${output}`
    } catch (e) {
        return 'nogit'
    }
}

function main() {
    process.chdir(BASEDIR)
    process.chdir(path.join(BASEDIR, 'build'))

    const gitVersion = getGitVersion()

    const BUILD_FOOTPRINT =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        `_${gitVersion}`

    logStep(`Building Debug... (${BUILD_FOOTPRINT})`)
    runCommand(
        `cmake .. -G "Ninja Multi-Config" -DBUILD_FOOTPRINT="${BUILD_FOOTPRINT}" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake`
    )
    runCommand('cmake --build . --config Debug')

    const debugDir = path.join(BASEDIR, 'build', 'Debug')
    copyFile(path.join(debugDir, 'camel.exe'), path.join(BASEDIR, 'camel.exe'))

    logDone('Debug build completed')
}

main()
