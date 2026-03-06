import { execSync } from 'child_process'
import { runCmakeBuild, copyBuildArtifacts, logDone, BASEDIR } from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

function main() {
    const config = 'Debug'
    const cmakeOpts = getCmakeOptionFlags()
    runCmakeBuild(config, cmakeOpts)
    copyBuildArtifacts(config)
    execSync('node scripts/collect-out.js debug', { cwd: BASEDIR, stdio: 'inherit' })
    logDone('Debug build completed')
}

main()
