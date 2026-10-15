// A loop in the debugger, on both debuggable back ends: its body is part of the frame of the
// function the loop is written in (design principles 2.11: like the arms of a branch), a
// breakpoint in the body stops once per round with the names of the round, and `frames
// subgraphs` shows the body graph as the subgraph it is.

import { runTest, check, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const LOOPS = fixture('loops.cml')

await runTest(async (t) => {
    const L = (mark) => lineOf(LOOPS, `mark:${mark}`)
    for (const backend of ['std::fvm', 'std::nvm']) {
        const on = (what) => `${backend}: ${what}`
        const has = (out, re, what) => check(re.test(out || ''), on(what), out)

        const repl = await t.repl()
        has(await repl.command(`file ${LOOPS}`), /Loaded: .*loops\.cml \(task \d+\)\./, 'the file is loaded as a task')
        has(await repl.command(`break ${L('body')}`), new RegExp(`^Breakpoint \\d+ at loops\\.cml:${L('body')}$`, 'm'), 'a breakpoint in the loop body')
        has(
            await repl.command(`run ${backend}`),
            new RegExp(`^Stopped \\(breakpoint\\) in squares  loops\\.cml:${L('body')}:`, 'm'),
            'the first round stops in the body, shown as squares',
        )
        const bt = await repl.command('bt')
        has(bt, new RegExp(`^#0  squares  loops\\.cml:${L('body')}:\\d+\\n#1  main  loops\\.cml:${L('call')}:`, 'm'), 'bt: the body is part of the frame of squares')
        has(await repl.command('print x'), /^x: int = 1$/m, 'the element of the first round')
        has(await repl.command('print acc'), /^acc: int = 0$/m, 'the first state')

        has(await repl.command('frames subgraphs'), /^Frames: subgraphs/m, 'frames subgraphs')
        has(
            await repl.command('bt'),
            new RegExp(`^#0  squares \\[loop body at line ${L('body')}\\]  loops\\.cml:${L('body')}:`, 'm'),
            'bt: the body as the subgraph frame it is',
        )
        await repl.command('frames functions')

        has(await repl.command('continue'), new RegExp(`^Stopped \\(breakpoint\\) in squares  loops\\.cml:${L('body')}:`, 'm'), 'the second round stops again')
        has(await repl.command('print x'), /^x: int = 2$/m, 'the element of the second round')
        has(await repl.command('print acc'), /^acc: int = 1$/m, 'the state after the first round')
        await repl.command('continue')
        const end = await repl.command('continue')
        has(end, /^14$/m, 'the program prints its result')
        has(end, /^Program finished: completed \(exit code 0\)\.$/m, 'the end of the run')
        await repl.quit()
    }
})
