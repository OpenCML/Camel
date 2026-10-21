// The REPL of camel-db (docs/debugging.en.md, section 2), on both debuggable back ends: a user
// types commands on its standard input. The same presentation as over DAP (design principles
// 12.1): the arm of a branch is a region of the frame of its function; a line step that goes
// past a line nothing uses the result of says so; functions are named and found by their full names.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const FOLDED = fixture('folded.cml')
const NAMES = fixture('names.cml')

await runTest(async (t) => {
    const L = (mark) => lineOf(FOLDED, `mark:${mark}`)
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const has = (out, re, what) => check(re.test(out || ''), on(what), out)

        const repl = await t.repl()
        has(await repl.command(`file ${FOLDED}`), /Loaded: .*folded\.cml \(task \d+\)\./, 'the file is loaded as a task')
        has(await repl.command(`break ${L('call')}`), new RegExp(`^Breakpoint \\d+ at folded\\.cml:${L('call')}$`, 'm'), 'a line breakpoint')
        has(
            await repl.command(`break ${L('unused')}`),
            new RegExp(`at folded\\.cml:${L('unused')} \\(not verified: Line ${L('unused')} is not executed: nothing uses the value of \`unused\`\\.`),
            'a breakpoint on a line nothing uses the value of, with the reason',
        )
        has(await repl.command('break geo::scale'), /^Breakpoint \d+ at geo::scale$/m, 'a function breakpoint by the full name')
        has(await repl.command('break nothere'), /not verified: No function named `nothere` in the program\./, 'a function that does not exist')

        has(
            await repl.command(`run ${backend}`),
            new RegExp(`^Stopped \\(breakpoint\\) in main  folded\\.cml:${L('call')}:`, 'm'),
            'run stops at the breakpoint',
        )
        has(await repl.command('step'), new RegExp(`^Stopped \\(step\\) in pick  folded\\.cml:${L('base')}:`, 'm'), 'step enters pick')
        const next = await repl.command('next')
        has(next, new RegExp(`^Stopped \\(step\\) in pick  folded\\.cml:${lineOf(FOLDED, 'let r = if')}:`, 'm'), 'next: the next line that executes')
        has(next, new RegExp(`^  line ${L('unused')} was not executed: nothing uses the value of \`unused\`$`, 'm'), 'and the line it went past, with the reason')
        has(await repl.command('next'), new RegExp(`in pick  folded\\.cml:${L('big')}:`), 'next: the line of the arm that is taken')
        has(await repl.command('next'), /^Stopped \(breakpoint\) in geo::scale  folded\.cml:/m, 'the function breakpoint ends the step, in the full name')
        has(await repl.command('finish'), new RegExp(`^Stopped \\(step\\) in pick  folded\\.cml:${L('bigret')}:`, 'm'), 'finish: back in the arm, shown as pick')

        const bt = await repl.command('bt')
        has(bt, new RegExp(`^#0  pick  folded\\.cml:${L('bigret')}:\\d+\\n#1  main  folded\\.cml:${L('call')}:\\d+\\n#2  __root__`, 'm'), 'bt: the arm is part of the frame of pick')
        const locals = await repl.command('locals')
        has(locals, /^  base: int = 4$/m, 'locals: the names of pick')
        has(locals, /^  big: int = 12$/m, 'locals: and those of the arm that runs')
        check(!/small/.test(locals || ''), on('locals: not those of the arm that does not run'), locals)
        has(await repl.command('print big'), /^big: int = 12$/m, 'print a name of the arm')

        has(await repl.command('frames subgraphs'), /^Frames: subgraphs/m, 'frames subgraphs')
        has(await repl.command('frames'), /^Frames: subgraphs$/m, 'frames tells the setting')
        has(await repl.command('frames functions'), /^Frames: functions/m, 'frames functions')
        has(await repl.command('bt'), /^#0  pick  folded/m, 'bt: the arm is part of the frame of pick')

        const end = await repl.command('continue')
        has(end, /^14$/m, 'the program prints its result')
        has(
            end,
            new RegExp(`^Breakpoint at folded\\.cml:${L('unused')} was never hit: line ${L('unused')} was not executed: nothing uses the value of \`unused\`\\.$`, 'm'),
            'the breakpoint that was never hit is explained',
        )
        has(end, /^Program finished: completed \(exit code 0\)\.$/m, 'the end of the run')
        checkEq((await repl.quit())?.code, 0, on('quit ends the REPL with exit code 0'))
    }

    // Full names across modules.
    {
        const repl = await t.repl()
        await repl.command(`file ${NAMES}`)
        has(await repl.command('break twice'), /not verified: `twice` is ambiguous: names_lib::twice, names_other::twice\. Write the full name\./, 'a name two modules have')
        has(await repl.command('break Config::clamp'), /^Breakpoint \d+ at names_lib::Config::clamp$/m, 'the module may be left out when one module has the name')
        has(await repl.command('run std::fvm'), /^Stopped \(breakpoint\) in names_lib::Config::clamp  names_lib\.cml:/m, 'the frame has the full name')
        has(await repl.command('locals'), /^\[Captured\]\n  names_lib::Config::limit: int = 10$/m, 'the captured value of the module, by its full name')
        has(await repl.command('print limit'), /^limit: int = 10$/m, 'found by the name the source writes')
        has(await repl.command('continue'), /^Program finished: completed \(exit code 0\)\.$/m, 'the end of the run')
        checkEq((await repl.quit())?.code, 0, 'quit ends the REPL')
    }

    function has(out, re, what) {
        return check(re.test(out || ''), what, out)
    }
})
