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
        return `${output}`
    } catch (e) {
        return ''
    }
}

export default function main() {
    process.chdir(BASEDIR)
    process.chdir(path.join(BASEDIR, 'build'))

    const gitVersion = getGitVersion()

    const BUILD_FOOTPRINT =
        new Date().toISOString().replace(/[-:]/g, '').replace(/[T]/g, '_').slice(2, 15) +
        (gitVersion ? `_${gitVersion}` : '')

    logStep(`Building Release... (${BUILD_FOOTPRINT})`)
    runCommand(
        `cmake .. -G "Ninja Multi-Config" -DBUILD_FOOTPRINT="${BUILD_FOOTPRINT}" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake`
    )
    runCommand('cmake --build . --config Release')

    const debugDir = path.join(BASEDIR, 'build', 'Release')
    const isWindows = process.platform === 'win32'
    const executableName = `camel${isWindows ? '.exe' : ''}`

    copyFile(path.join(debugDir, executableName), path.join(BASEDIR, executableName))

    logDone('Release build completed')
}

main()
