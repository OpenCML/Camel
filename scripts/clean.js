import path from 'path'
import { removeDir, logStep, logDone, logWarn, BASEDIR } from './common.js'

;(async () => {
    logWarn('This script is not yet implemented')
    // logStep('Cleaning build directory...')
    // removeDir(path.join(BASEDIR, 'build'))
    // logDone('Cleaned build directory')

    // logStep('Cleaning pypi directory...')
    // const PYPI_BASE = path.join(BASEDIR, 'pypi', 'camel-lang')
    // removeDir(path.join(PYPI_BASE, 'bin'))
    // removeDir(path.join(PYPI_BASE, 'dist'))
    // removeDir(path.join(PYPI_BASE, 'build'))
    // removeDir(path.join(PYPI_BASE, 'stdlib'))
    // removeDir(path.join(PYPI_BASE, 'src', 'camel_lang.egg-info'))
    // logDone('Cleaned build directory')
    // conan remove *
})()
