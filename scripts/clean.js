import path from 'path'
import { removeDir, logStep, logDone, BASEDIR } from './common.js'

;(async () => {
    logStep('Cleaning build directory...')
    removeDir(path.join(BASEDIR, 'build'))
    logDone('Cleaned build directory')
})()
