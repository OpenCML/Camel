// Fixture of test/tools/selftest/run.mjs, the plans parallel.plan.toml, serial.plan.toml and
// tier.plan.toml: prints its name and, when SELFTEST_PROBE_LOG names a file, appends the time it
// started and the time it ended. The self-test reads those intervals to show that tests run
// together in a parallel run and that a serial-marked test overlaps nothing.
//
//   node selftest_probe.mjs <mode> <name> [delay_ms]
//
// mode: "sleep" waits delay_ms between the two lines; "echo" writes both at once.
import fs from 'fs'

const [mode = 'echo', name = 'probe', delay = '400'] = process.argv.slice(2)
const log = process.env.SELFTEST_PROBE_LOG
const stamp = () => Date.now()
const append = (line) => {
    if (!log) return
    fs.appendFileSync(log, `${line}\n`, 'utf8')
}

append(`${name} start ${stamp()}`)
if (mode === 'sleep') {
    await new Promise((resolve) => setTimeout(resolve, Number(delay) || 0))
}
append(`${name} end ${stamp()}`)
console.log(name)