import { execSync } from 'child_process'
import glob from 'glob'

const files = glob.sync('src/**/*.{cpp,h}')

files.forEach((file) => {
    try {
        execSync(`clang-format -i "${file}"`, { stdio: 'inherit' })
        console.log(`Formatted: ${file}`)
    } catch (error) {
        console.error(`Failed to format: ${file}`)
    }
})

