import path from 'path'
import { removeDir, conanInstall, BASEDIR } from './common.js'

function main() {
    process.chdir(BASEDIR)
    removeDir(path.join(BASEDIR, 'build'))
    // Which build types, see conanBuildTypes(): a job that builds only Release sets
    // CAMEL_CONAN_BUILD_TYPES=Release and skips the other two.
    conanInstall()
}

main()