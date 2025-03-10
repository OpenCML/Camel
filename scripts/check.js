import { spawn } from 'child_process'
import ora from 'ora'
import { logDone, logFail } from './common.js'

async function formatCode() {
    const spinner = ora('Initializing code format...').start()
    const proc = spawn('npm', ['run', 'format'], { shell: true })

    const success = await new Promise((resolve) => {
        proc.stdout.on('data', (data) => {
            const output = data.toString()
            const lines = output.split('\n')
            lines.forEach((line) => {
                let l = line.trim()
                if (l) {
                    spinner.text = l
                }
            })
        })

        proc.stderr.on('data', (data) => {
            spinner.fail(`Error: ${data.toString().trim()}`)
        })

        proc.on('close', (code) => {
            if (code === 0) {
                spinner.succeed('Code formatted successfully!')
                resolve(true)
            } else {
                spinner.fail('Code format failed!')
                resolve(false)
            }
        })
    })

    return success
}

async function updateCode() {
    const spinner = ora('Updating code info...').start()
    const proc = spawn('npm', ['run', 'upd'], { shell: true })

    const success = await new Promise((resolve) => {
        proc.stdout.on('data', (data) => {
            spinner.text = data.toString().trim()
        })

        proc.stderr.on('data', (data) => {
            spinner.fail(`Error: ${data.toString().trim()}`)
        })

        proc.on('close', (code) => {
            if (code === 0) {
                spinner.succeed('Code info updated successfully!')
                resolve(true)
            } else {
                spinner.fail('Code info update failed!')
                resolve(false)
            }
        })
    })

    return success
}

// npm run cnt
async function countCode() {
    const spinner = ora('Counting code lines...').start()
    const proc = spawn('npm', ['run', 'cnt'], { shell: true })
    let output = ''
    let startRecording = false

    const success = await new Promise((resolve) => {
        proc.stdout.on('data', (data) => {
            if (data.toString().startsWith('====')) {
                startRecording = true
            }
            if (startRecording) {
                output += data.toString()
            }
        })

        proc.stderr.on('data', (data) => {
            spinner.fail(`Error: ${data.toString().trim()}`)
        })

        proc.on('close', (code) => {
            if (code === 0) {
                spinner.succeed('Code lines result is listed below:')
                console.log(output)
                resolve(true)
            } else {
                spinner.fail('Code lines count failed!')
                resolve(false)
            }
        })
    })

    return success
}

;(async () => {
    let success = await formatCode()
    if (!success) {
        logFail('Formatting task failed!')
        return
    }
    success = await updateCode()
    if (!success) {
        logFail('Updating task failed!')
        return
    }
    success = await countCode()
    if (!success) {
        logFail('Counting task failed!')
        return
    }
    logDone('You are all set! Now you can push your code.')
})()
