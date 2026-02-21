import {
    runCmakeBuild,
    copyBuildArtifacts,
    logDone
} from './common.js'
import { getCmakeOptionFlags } from './cmake-opts.js'

export default function main() {
    const config = 'RelWithDebInfo'
    const cmakeOpts = getCmakeOptionFlags()
    runCmakeBuild(config, cmakeOpts)
    copyBuildArtifacts(config)
    logDone('Profile build completed')
}

main()
