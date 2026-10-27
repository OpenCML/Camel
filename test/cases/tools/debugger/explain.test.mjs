// The two attribution commands of camel-db (rewrite design 8.7): `explain` reads the record a run
// left and shows the rule and the two node positions of a chain, `why_not` names the rule and the
// reason it did not fire. The program prints the library's own answers before it stops, so the
// test compares the debugger with the library on one record instead of pinning numbers that a
// change of layout would make stale.
//
// The last part asks for a value that has no value yet and for a run of the wrong type: the
// command reports a located diagnostic and the session stays stopped and usable.

import { runTest, check, checkEq, fixture, lineOf } from '../../../tools/debugger/client.mjs'

const FILE = fixture('explain.cml')

await runTest(async (t) => {
    const stop = lineOf(FILE, 'mark:stop')
    const repl = await t.repl()

    check(/^Loaded: .*explain\.cml \(task \d+\)\.$/m.test(await repl.command(`file ${FILE}`)), 'the file is loaded')
    check(new RegExp(`^Breakpoint \\d+ at explain\\.cml:${stop}$`, 'm').test(await repl.command(`break ${stop}`)), 'a breakpoint where the run has a record')

    // Running the program prints the library's proof and its why_not answer before it stops.
    const runOut = await repl.command('run std::nvm')
    const libChain = (runOut || '').match(/^chain (%\d+).*?= (%\d+).*?: 1 rewrite$/m)
    check(libChain, 'the library printed the chain of the record', runOut)
    check(/^\s+= .*  by zero_to_one  \(round 1, graph f\)$/m.test(runOut || ''), 'the library named the rule that fired', runOut)
    const libReason = (runOut || '').match(/^`mul_zero` did not fire at (.*)$/m)
    check(libReason, 'the library answered why_not with the rule and the reason', runOut)
    check(new RegExp(`^Stopped \\(breakpoint\\) in show.*explain\\.cml:${stop}:`, 'm').test(runOut || ''), 'the program stopped where the run can be read', runOut)

    // explain: the same rule and the same two positions as the library's own print.
    const chain = await repl.command('explain r before after')
    const gotChain = (chain || '').match(/chain (%\d+).*?= (%\d+).*?: 1 rewrite/)
    check(gotChain, 'explain printed a chain', chain)
    checkEq(gotChain?.slice(1, 3), libChain?.slice(1, 3), 'explain names the same two values the library named')
    check(/by zero_to_one/.test(chain || ''), 'explain names the rule that fired', chain)
    check(/\(%\d+ -> %\d+\)/.test(chain || ''), 'explain names the matched and produced node positions', chain)

    // explain_all: every rewrite of the record, with the rule and the positions.
    const all = await repl.command('explain r')
    check(/^explain_all: 1 rewrite in \d+ rounds?, \d+ nodes visited$/m.test(all || ''), 'explain without two values reads the whole record', all)
    check(/by zero_to_one/.test(all || ''), 'the whole record names the rule', all)
    check(/\(%\d+ -> %\d+\)/.test(all || ''), 'the whole record names the positions', all)

    // why_not: the same answer, naming the rule and the reason.
    const why = await repl.command('why_not r failing other')
    check(/why_not: `mul_zero` did not fire at /.test(why || ''), 'why_not names the rule', why)
    check((why || '').includes((libReason?.[1] || '').trim()), 'why_not gives the reason the library gave', why)

    // A value that has no value yet: a located diagnostic, not a crash and not a silent stop.
    const noValue = await repl.command('explain r before nothere')
    check(new RegExp(`explain\\.cml:${stop}:\\d+: 'nothere' is not a name of this frame\\.$`, 'm').test(noValue || ''), 'a name outside the frame is reported at the stop', noValue)

    // A run of the wrong type: the operator's own type rule refuses it, still located.
    const wrongType = await repl.command('explain before before after')
    check(/arguments of 'rewrite:explain' do not fit its type rule/.test(wrongType || ''), 'an argument of the wrong type is reported by the type rule', wrongType)
    check(new RegExp(`explain\\.cml:${stop}:\\d+:`).test(wrongType || ''), 'the refusal is located', wrongType)

    // The session is still stopped and usable after both failures.
    check(/^before: Node = /.test(await repl.command('print before')), 'the session still reads the frame')
    check(/^Program finished: completed \(exit code 0\)\.$/m.test(await repl.command('continue')), 'and the program still finishes')

    checkEq((await repl.quit())?.code, 0, 'quit ends the REPL with exit code 0')
})