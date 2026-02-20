import { runCmakeBuild, copyBuildArtifacts, logDone } from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

export default function main() {
    const config = 'Release'
    const cmakeOpts = getCmakeOptionFlags()
    runCmakeBuild(config, cmakeOpts)
    copyBuildArtifacts(config)
    logDone('Release build completed')
}

main()
