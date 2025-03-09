import path from 'path'
import { runCommand, copyFile, BASEDIR, logDone } from './common.js'

export default function main() {
    process.chdir(BASEDIR)
    process.chdir(path.join(BASEDIR, 'build'))

    runCommand(
        'cmake .. -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=./build/conan_toolchain.cmake'
    )
    runCommand('cmake --build . --config Release')

    const debugDir = path.join(BASEDIR, 'build', 'Release')
    copyFile(path.join(debugDir, 'camel.exe'), path.join(BASEDIR, 'camel.exe'))

    logDone('Release build completed')
}

main()
