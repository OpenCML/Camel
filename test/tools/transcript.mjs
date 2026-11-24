// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The run's transcript -- one case line per verdict -- and the check that says whether it can be
 * read at all.
 *
 * A verdict is printed and counted in one act inside the process (`printResult` in
 * test/tools/run-tests.mjs: `printStatusLine` then `summary[outcome.status]++`), so a transcript
 * that holds fewer case lines than `[SUMMARY] total:` is not an arithmetic defect in the runner:
 * it is output that left the process and never arrived. The measurement that made this file:
 * three runs of one base printed `total: 9159` and **all 9159 of their `[PASS]`/`[FAIL]` lines**,
 * while the `--list-known` block -- printed in the last tight loop, immediately before
 * `process.exit` -- arrived with 9159, 5265 and 1239 rows, fewest from the slowest run: the shape of
 * a tail still queued in the process when the process ends.
 *
 * Two readings, one rule. The runner records every case line here as it prints it, so a run leaves
 * a record that does not pass through a pipe; at the end of the run it reads that record back and
 * refuses to be read when the file does not hold what the summary counted. The same rule then
 * applies to text that was *retrieved* -- a capture, a redirection, a pipeline -- because the loss
 * can happen after the process has decided it was fine: `--check` reads such a text and refuses a
 * capture whose case lines and `[SUMMARY]` line disagree. A count is never adjusted to agree with
 * the record, and the record is never written from the count: the two are taken from two different
 * places on purpose, because a check that compares a number with itself reports agreement whatever
 * happened.
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

/** The transcript's file name inside the run directory `test/results/log/<timestamp>_<pid>/`. */
export const TRANSCRIPT_NAME = 'transcript.txt'

/**
 * One case line. The four verdicts the runner prints, and nothing else: a failure's detail lines
 * are indented under their case line and a plan header is `[PLAN]`, so a line that starts with one
 * of these labels is exactly one counted verdict.
 */
export const CASE_LINE = /^\[(PASS|FAIL|SKIP|XFAIL)\] /

/**
 * How many case lines `text` holds. The count the check is made of, as a pure function.
 *
 * The colours are stripped first, and that is not tidiness: a captured console carries
 * `\e[32m[PASS]\e[0m`, so `^\[PASS\]` matches nothing and a complete green capture counts zero
 * rows. Measured here on 2026-11-24: a 25-line capture of a 6-case run counted 0 case lines raw
 * and 6 after the strip, and the same mistake was made twice in one night while reading the seed
 * transcripts (0 of 9159 rows). A reader that can report 0 for a complete transcript is a reader
 * whose "the rows are missing" cannot be believed.
 */
export function countCaseLines(text) {
    return plainText(text)
        .split(/\r?\n/)
        .filter((line) => CASE_LINE.test(line)).length
}

/**
 * Open the transcript of the run in `dir` and hand back its writer.
 *
 * One line per verdict, appended as it is printed, through one open descriptor: the record has to
 * survive the process (that is what it is for), so a line is a write and not a buffer, and a write
 * that fails is remembered rather than thrown -- the run must reach its summary and say what it
 * could not record, instead of disappearing behind an `ENOSPC`.
 *
 * `recorded()` is what the run handed to the file; the judge below counts the file itself, and the
 * two are compared as well: a line accepted and then absent is its own defect.
 */
export function openTranscript(dir) {
    const file = path.join(dir, TRANSCRIPT_NAME)
    const fd = fs.openSync(file, 'a')
    let recorded = 0
    let failure = null
    return {
        file,
        caseLine(text) {
            try {
                fs.writeSync(fd, text.endsWith('\n') ? text : `${text}\n`)
                recorded += 1
            } catch (error) {
                if (failure === null) {
                    failure = `${(error && (error.code || error.message)) || String(error)}`
                }
            }
        },
        close() {
            try {
                fs.closeSync(fd)
            } catch {
                /* a descriptor already gone is not a reading of the record */
            }
        },
        recorded: () => recorded,
        failure: () => failure,
    }
}

/**
 * The end of the run, read off the record it wrote: how many case lines the transcript holds, and
 * whether that is the number the summary counted.
 *
 * `total` is the summary's own total -- pass + fail + skip + xfail, the number `printRunSummary`
 * prints. Nothing here writes to it, and nothing here writes to the transcript; the question is
 * only whether the file that exists holds one line per counted verdict.
 */
export function judgeTranscript(transcript, total) {
    transcript.close()
    let lines = 0
    let failure = transcript.failure()
    try {
        lines = countCaseLines(fs.readFileSync(transcript.file, 'utf8'))
    } catch (error) {
        // A transcript that cannot be read back is not a transcript with zero lines: it is a
        // record that was never written, and the two are said apart.
        failure = failure || `the transcript could not be read back (${error.code || error.message})`
    }
    const recorded = transcript.recorded()
    if (failure === null && recorded !== lines) {
        failure = `${recorded} case line(s) were accepted by the writer and ${lines} are in the file`
    }
    return { ok: failure === null && lines === total, lines, recorded, total, failure, file: transcript.file }
}

/**
 * The one sentence a run that cannot be read prints, with both numbers in it: the transcript's own
 * count and the summary's. A single number would leave a reader unable to say which side moved,
 * and "the summary counted M" is the half that must never be the one that was adjusted.
 */
export function transcriptRefusal(verdict) {
    const why = verdict.failure ? ` (${verdict.failure})` : ''
    return (
        `the transcript holds ${verdict.lines} case lines but the summary counted ` +
        `${verdict.total}: this run cannot be read${why}`
    )
}

/** A captured console line without its colour: a file, or a pipe consumed by a tool. */
export function plainText(text) {
    return String(text || '').replace(/\x1b\[[0-9;]*[A-Za-z]/g, '')
}

/**
 * The total a captured transcript states, or null when it states none.
 *
 * Matched at the start of a line, and the LAST match wins: a capture can hold a nested run's
 * failure output inside a `[stdout]` block, and the run this capture is of is the one whose
 * `[SUMMARY]` is printed last -- immediately before `[TREE]`/`[SLOT]`, which are the lines after it.
 */
export function statedTotal(text) {
    let total = null
    for (const line of plainText(text).split(/\r?\n/)) {
        const match = /^\[SUMMARY\] total: (\d+),/.exec(line)
        if (match) total = Number(match[1])
    }
    return total
}

/**
 * The rows of a `--list-known` capture: one `{"name":..., "verdict":...}` per case, printed at the
 * very end of the run and bracketed by the markers `camel-remote.ps1` adds.
 *
 * They are the other per-case channel, and the one a host run actually lost: of three runs of one
 * base on 2026-11-24, all three held their 9159 case lines and their `[SUMMARY] total: 9159`, and
 * the row block arrived with 9159, 5265 and 1239 lines. A row is defined the way the consumer
 * defines it (`scripts/merge-readings.mjs#readRows`): a line at column 0 that is JSON with a string
 * `name` and a string `verdict`. Column 0 matters -- a failed test's captured output is indented
 * under its case line, and a nested run's rows must not be counted as this run's.
 */
export function countRows(text) {
    let rows = 0
    for (const line of plainText(text).split(/\r?\n/)) {
        if (!line.startsWith('{"name"')) continue
        try {
            const row = JSON.parse(line)
            if (typeof row.name === 'string' && typeof row.verdict === 'string') rows += 1
        } catch {
            /* a line that starts like a row and is not one is not a row */
        }
    }
    return rows
}

/**
 * The same rule as `judgeTranscript`, applied to text that was **retrieved** rather than written:
 * a captured console, a `cmd /c ... > file` redirect, a pipeline, a log someone pasted.
 *
 * This is the reading the in-process check cannot take. A run that exits cleanly has already
 * decided its own record is complete; if the block carrying the case lines and the `[SUMMARY]`
 * line is lost between the process and the file the next person reads -- and a whole block can go,
 * not only its tail -- then the only place left to notice is here, on the bytes that arrived. A
 * capture with no `[SUMMARY]` states no total, so nothing about it can be read, and that is a
 * refusal and not a zero.
 *
 * Both per-case channels are counted, because a run has one and a `--list-known` run has two: the
 * case lines, and the rows the block at the end carries. A capture whose rows are short of its
 * total is refused even when every case line arrived.
 */
export function judgeCapturedText(text) {
    const lines = countCaseLines(text)
    const rows = countRows(text)
    const total = statedTotal(text)
    if (total === null) {
        return {
            ok: false,
            lines,
            rows: rows > 0 ? rows : null,
            total: null,
            failure:
                `no [SUMMARY] total: line in this capture${lines > 0 ? ` (its ${lines} case line(s) ` +
                    'have no count to be checked against)' : ' and no case line either'}`,
        }
    }
    const saidRows = rows > 0 ? rows : null
    return {
        ok: lines === total && (saidRows === null || saidRows === total),
        lines,
        rows: saidRows,
        total,
        failure: null,
    }
}

/** `judgeCapturedText`'s refusal, in the words the run uses for the same disagreement. */
export function capturedRefusal(verdict) {
    if (verdict.total === null) {
        return `the transcript states no summary total: this capture cannot be read (${verdict.failure})`
    }
    if (verdict.lines !== verdict.total && verdict.rows !== null && verdict.rows !== verdict.total) {
        return (
            `the transcript holds ${verdict.lines} case lines and ${verdict.rows} case rows but the ` +
            `summary counted ${verdict.total}: this run cannot be read`
        )
    }
    if (verdict.rows !== null && verdict.rows !== verdict.total) {
        return (
            `the transcript holds ${verdict.rows} case rows but the summary counted ${verdict.total}: ` +
            'this run cannot be read (its case lines are whole, so the row block was cut)'
        )
    }
    return transcriptRefusal(verdict)
}

/**
 * Both channels, each as its own number, printed whether the capture is readable or not.
 *
 * "The case lines are whole" and "the row block arrived" are two questions, and a reader who is
 * told only that they disagree cannot say which of the two lost lines -- nor whether the rows are
 * absent because the run never asked for them or because the block was cut. `rows: N of M` is the
 * number the merge position compares (`scripts/merge-readings.mjs` reads exactly these rows), so it
 * is printed on its own and never inferred from a refusal.
 */
export function readingLine(verdict) {
    const total = verdict.total === null ? null : String(verdict.total)
    const caseLines = total === null ? `${verdict.lines}` : `${verdict.lines} of ${total}`
    let rows
    if (verdict.rows === null) rows = 'none in this capture'
    else rows = total === null ? `${verdict.rows}` : `${verdict.rows} of ${total}`
    const summary = total === null ? ', summary: absent' : ''
    return `transcript: case lines: ${caseLines}, rows: ${rows}${summary}${verdict.ok ? ' -- readable' : ''}`
}

const USAGE = 'usage: node test/tools/transcript.mjs --check <capture.txt|->'

/**
 * The instrument a retrieval step can put between a run and its reader:
 *
 *   node test/tools/transcript.mjs --check capture.txt
 *
 * Exit 0 only when the capture's own `[SUMMARY] total:` equals the number of case lines it holds
 * and the number of rows of a `--list-known` block it carries; exit 2 when either disagrees, and
 * when it states no total at all. It prints both counts either way -- a checker that says nothing
 * on success and nothing on failure is not a checker, and one that reports a single number cannot
 * say which channel was cut.
 */
async function checkCli(argv) {
    if (argv[0] !== '--check' || argv.length !== 2) {
        process.stderr.write(`${USAGE}\n`)
        process.exit(2)
    }
    const source = argv[1]
    let text
    try {
        text = source === '-' ? await readStdin() : fs.readFileSync(source, 'utf8')
    } catch (error) {
        process.stderr.write(`transcript: ${source}: ${error.code || error.message}\n`)
        process.exit(2)
    }
    const verdict = judgeCapturedText(text)
    if (!verdict.ok) process.stdout.write(`[PLAN ERROR] ${capturedRefusal(verdict)}\n`)
    process.stdout.write(`${readingLine(verdict)}\n`)
    process.exit(verdict.ok ? 0 : 2)
}

function readStdin() {
    return new Promise((resolve, reject) => {
        let text = ''
        process.stdin.setEncoding('utf8')
        process.stdin.on('data', (chunk) => {
            text += chunk
        })
        process.stdin.on('end', () => resolve(text))
        process.stdin.on('error', reject)
    })
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    checkCli(process.argv.slice(2))
}