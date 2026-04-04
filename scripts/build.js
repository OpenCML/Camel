import { execSync } from 'child_process'
import path from 'path'
import { fileURLToPath } from 'url'
import { runCmakeBuild, copyBuildArtifacts, logDone, BASEDIR } from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

export default function main() {
    const config = 'Release'
    const cmakeOpts = getCmakeOptionFlags()
    runCmakeBuild(config, cmakeOpts)
    copyBuildArtifacts(config)
    execSync('node scripts/collect-out.js build', { cwd: BASEDIR, stdio: 'inherit' })
    logDone('Release build completed')
}

const thisFile = fileURLToPath(import.meta.url)
if (path.resolve(process.argv[1] || '') === thisFile) {
    main()
}
