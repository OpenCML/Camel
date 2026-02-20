import {
    runCmakeBuild,
    copyBuildArtifacts,
    logDone
} from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

function main() {
    const config = 'Debug'
    const cmakeOpts = getCmakeOptionFlags()
    runCmakeBuild(config, cmakeOpts)
    copyBuildArtifacts(config)
    logDone('Debug build completed')
}

main()
