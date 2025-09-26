import path from 'path'
import { removeDir, runCommand, BASEDIR } from './common.js'

function main() {
    process.chdir(BASEDIR)
    removeDir(path.join(BASEDIR, 'build'))
    runCommand('conan install . --output-folder=build --build=missing')
    runCommand('conan install . --output-folder=build --build=missing --settings=build_type=Debug')
    runCommand('conan install . --output-folder=build --build=missing --settings=build_type=RelWithDebInfo')
}

main()
